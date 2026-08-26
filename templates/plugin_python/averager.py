"""A platform module written in Python, loaded through the C path of the plugin ABI.

Nothing here is compiled and nothing links the SDK: the bridge next to atp_app reads this class at
load time and builds real platform ports from it, so the module below connects to the C++ modules of
atp_demo_plugin with no adapter in the config.

Heavy dependencies belong in initialize rather than at module level: an import at module level runs
when the plugin loads and is paid by every pipeline, including those that never create this module.

Three kinds of thing reach this class, and the difference is worth learning once:

    value  = atp.Input(atp.i32)     the data, read every pass
    window = atp.Property(atp.i32, 4)   one scalar, edited while the pipeline runs
    self.config                     a structure, given once at creation

The config is a structure the pipeline's author wrote down, turned into ordinary Python: an object
becomes a dict, an array a list, a scalar itself. It is bound before __init__ so that a constructor
can read it, and the pipeline below hands this one:

    "config": { "weights": [1, 2, 3, 4] }

This module **declares** what it accepts, with the atp.Config class below. Declaring is worth it for
three things it buys at once: atp_studio edits the config as typed rows instead of raw JSON, a document
that does not fit is refused before the pipeline starts — naming the file and the field — and every
declared key arrives at its own default, so the constructor needs no fallbacks at all.

Declaring is optional. A module whose config is a format the platform does not parse declares nothing,
reads self.config_text itself, and checks self.config_opaque to know that is all there is; packer.py's
docstring says more about when that is the right call.
"""

import atp


class AveragerConfig(atp.Config):
    """What this module accepts under "config", declared once and checked by the host.

    Declaration order is a contract: it is the order atp_studio draws the rows in, so the class body is
    read top to bottom and never sorted.

    `weights` has no default because an empty list is a perfectly good answer here — a module with no
    weights takes a plain mean. A field that must be written instead is declared with no default at all,
    `atp.Field(atp.f64)`, and then a document that omits it is a problem naming the file rather than a
    surprise at run time.
    """

    weights = atp.List(atp.f64)


class Averager(atp.Module):
    name = "py_averager"
    version = (1, 0)

    config_type = AveragerConfig

    value = atp.Input(atp.i32)
    report = atp.Output(atp.text)
    mean = atp.Output(atp.f64)

    window = atp.Property(atp.i32, 4)
    mode = atp.Property(atp.text, "plain", options=("plain", "verbose"))
    fail = atp.Property(atp.bool_, False, persistent=False)

    def __init__(self):
        """Read the config. This is the only place it may be read; everything else is ordinary Python.

        No fallback and no .get(): every key AveragerConfig declared is already here, at its own default
        when the document said nothing about it. That is what declaring buys, and a module that declares
        nothing has to write the fallbacks instead.

        Ports are NOT reachable here — they are bound after __init__, which is what initialize is for.
        """
        self.weights = list(self.config["weights"])

    def initialize(self):
        self.recent = []
        self.log("averaging over a window of {}".format(self.window.get()))

    def iterate(self):
        value = self.value.take()
        if value is None:
            return atp.IDLE
        if self.fail.get():
            raise RuntimeError("the fail property was set")
        self.recent.append(value)
        del self.recent[: -self.window.get()]
        average = self._weighted() if self.weights else sum(self.recent) / len(self.recent)
        if self.mode.get() == "verbose":
            self.report.write(
                "py_averager: {} -> mean {:.3f} over {} of {}".format(
                    value, average, len(self.recent), self.window.get()
                )
            )
        else:
            self.report.write("py_averager: {:.3f}".format(average))
        self.mean.write(average)
        return atp.BUSY

    def _weighted(self):
        tail = self.recent[-len(self.weights) :]
        weights = self.weights[-len(tail) :]
        total = sum(weights)
        if total == 0:
            return 0.0
        return sum(v * w for v, w in zip(tail, weights)) / total

    def stop(self):
        self.recent = []

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

The config is whatever this module's "config" says in the pipeline, turned into ordinary Python: an
object becomes a dict, an array a list, a scalar itself, and a node that named no config becomes
None. There is nothing else to learn — no accessor type, no schema, no declaration in the class body.
It is bound before __init__ so that a constructor can read it, and the pipeline below hands this one:

    "config": { "weights": [1, 2, 3, 4] }
"""

import atp


class Averager(atp.Module):
    name = "py_averager"
    version = (1, 0)

    value = atp.Input(atp.i32)
    report = atp.Output(atp.text)
    mean = atp.Output(atp.f64)

    window = atp.Property(atp.i32, 4)
    mode = atp.Property(atp.text, "plain", options=("plain", "verbose"))
    fail = atp.Property(atp.bool_, False, persistent=False)

    def __init__(self):
        """Read the config. This is the only place it may be read; everything else is ordinary Python.

        `self.config or {}` rather than `self.config` because a module's node may name no config at
        all, and `.get(key, default)` rather than `config[key]` for the same reason one key down. A
        constructor that raises on a missing key cannot be placed in atp_studio: the palette probes
        every module with an empty config to learn its ports, and refuses one it could not describe.

        Ports are NOT reachable here — they are bound after __init__, which is what initialize is for.
        """
        config = self.config or {}
        self.weights = [float(w) for w in config.get("weights", ())]

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

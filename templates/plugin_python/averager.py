"""A platform module written in Python, loaded through the C path of the plugin ABI.

Nothing here is compiled and nothing links the SDK: the bridge next to atp_app reads this class at
load time and builds real platform ports from it, so the module below connects to the C++ modules of
atp_demo_plugin with no adapter in the config.

Heavy dependencies belong in initialize rather than at module level: an import at module level runs
when the plugin loads and is paid by every pipeline, including those that never create this module.
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
        average = sum(self.recent) / len(self.recent)
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

    def stop(self):
        self.recent = []

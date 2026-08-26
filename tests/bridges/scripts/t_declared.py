"""Declares its config and reports what arrived, so the declaration channel is exercised end to end.

Every key below is read without a fallback on purpose: that is the whole point of declaring them. Only
`rate` appears in the document the test writes, so the rest arriving at all is what proves the host
filled the declaration before the constructor ran.
"""

import atp


class Voice(atp.Config):
    note = atp.Field(atp.i64, 60)


class SynthConfig(atp.Config):
    rate = atp.Field(atp.i64)
    engine = atp.Field(atp.text, "fm", options=("fm", "additive"))
    master = atp.Group(Voice)
    voices = atp.List(Voice)
    taps = atp.List(atp.f64)


class Declared(atp.Module):
    name = "py_declared"
    config_type = SynthConfig

    out_report = atp.Output(atp.text)

    def __init__(self):
        cfg = self.config
        self.report = "rate={} engine={} note={} voices={} taps={} keys={}".format(
            cfg["rate"],
            cfg["engine"],
            cfg["master"]["note"],
            len(cfg["voices"]),
            len(cfg["taps"]),
            ",".join(cfg.keys()),
        )

    def iterate(self):
        if self.report is None:
            return atp.IDLE
        self.out_report.write(self.report)
        self.report = None
        return atp.BUSY

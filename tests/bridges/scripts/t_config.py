"""Reads its config in the constructor and reports it, so the whole channel is exercised from a script.

The constructor is where self.config has to be readable: that is the point of a channel that arrives
before initialize, and reading it here is what proves the binding order.
"""

import atp


class Configured(atp.Module):
    name = "py_config"

    out_report = atp.Output(atp.text)

    def __init__(self):
        cfg = self.config
        if cfg is None:
            self.report = "config=None"
            return
        self.report = "channels={} name={} nested={} keys={}".format(
            cfg["channels"][2],
            cfg["name"],
            cfg["nested"]["deep"],
            ",".join(cfg.keys()),
        )

    def iterate(self):
        if self.report is None:
            return atp.IDLE
        self.out_report.write(self.report)
        self.report = None
        return atp.BUSY

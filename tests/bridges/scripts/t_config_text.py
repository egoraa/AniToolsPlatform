"""Reads the raw text of a config the host did not parse, which is the point of an opaque format.

The three attributes are read in the constructor for the same reason self.config is: a module decides
what it is before initialize, and a format the platform does not know is decided here or nowhere.
"""

import atp


class ConfiguredByText(atp.Module):
    name = "py_config_text"

    out_report = atp.Output(atp.text)

    def __init__(self):
        self.report = "text-len={} origin={} opaque={} rate={}".format(
            len(self.config_text),
            self.config_origin or "none",
            self.config_opaque,
            (self.config or {}).get("audio", {}).get("rate", "none"),
        )

    def iterate(self):
        if self.report is None:
            return atp.IDLE
        self.out_report.write(self.report)
        self.report = None
        return atp.BUSY

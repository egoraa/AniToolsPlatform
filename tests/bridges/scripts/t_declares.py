"""Declares one port of every shape the bridge can describe, and does nothing with them."""

import atp


class Declares(atp.Module):
    name = "py_declares"
    version = (2, 1)

    state_i32 = atp.Input(atp.i32)
    queue_i32 = atp.Input(atp.i32, queue=True, capacity=8, overflow=atp.DROP_INCOMING)
    in_text = atp.Input(atp.text)

    out_i64 = atp.Output(atp.i64)
    out_f64 = atp.Output(atp.f64)
    out_blob = atp.Output(atp.blob)

    gain = atp.Property(atp.f64, 1.5)
    mode = atp.Property(atp.text, "plain", options=("plain", "verbose"))
    transient = atp.Property(atp.bool_, False, persistent=False)

    def iterate(self):
        return atp.IDLE

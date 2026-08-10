"""Echoes every payload kind back out, so one pass exercises the whole conversion table."""

import atp


class Echo(atp.Module):
    name = "py_echo"

    in_i32 = atp.Input(atp.i32)
    in_i64 = atp.Input(atp.i64)
    in_f64 = atp.Input(atp.f64)
    in_bool = atp.Input(atp.bool_)
    in_text = atp.Input(atp.text)
    in_blob = atp.Input(atp.blob)

    out_i32 = atp.Output(atp.i32)
    out_i64 = atp.Output(atp.i64)
    out_f64 = atp.Output(atp.f64)
    out_bool = atp.Output(atp.bool_)
    out_text = atp.Output(atp.text)
    out_blob = atp.Output(atp.blob)
    out_gain = atp.Output(atp.f64)

    gain = atp.Property(atp.f64, 1.5)
    overflow = atp.Property(atp.bool_, False, persistent=False)

    def initialize(self):
        self.passes = 0
        self.log("py_echo ready")

    def _echo(self, source, sink, transform=None):
        value = source.get()
        if value is None:
            return
        sink.write(value if transform is None else transform(value))

    def iterate(self):
        value = self.in_i32.get()
        if value is None:
            return atp.IDLE
        self.passes += 1
        if self.overflow.get():
            self.out_i32.write(2**31)
            return atp.BUSY
        self.out_i32.write(value)
        self._echo(self.in_i64, self.out_i64)
        self._echo(self.in_f64, self.out_f64)
        self._echo(self.in_bool, self.out_bool)
        self._echo(self.in_text, self.out_text, lambda seen: seen + "!")
        self._echo(self.in_blob, self.out_blob, lambda seen: bytes(reversed(seen)))
        self.out_gain.write(self.gain.get())
        return atp.BUSY

    def stop(self):
        self.passes = 0

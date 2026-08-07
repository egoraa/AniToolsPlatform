"""A second module, showing the two declarations averager.py does not use.

A queueing input, because this module answers about a group of values rather than about the latest
one: a state input would drop everything that arrived between two iterations, and the frame would be
built from samples chosen by timing. And a blob output, which is the escape hatch of the C path — the
payload type set there is closed, so anything the platform does not name travels as bytes.

The struct import sits at module level on purpose: it is stdlib and costs nothing. A heavy dependency
belongs in initialize instead, because an import at module level runs when the bridge reads this file
and is paid by every pipeline, including those that never create this module.
"""

import struct

import atp

_FORMATS = {"little": "<{}d", "big": ">{}d"}


class Packer(atp.Module):
    name = "py_packer"
    version = (1, 0)

    value = atp.Input(atp.f64, queue=True, capacity=64)
    frame = atp.Output(atp.blob)
    packed = atp.Output(atp.i32)

    size = atp.Property(atp.i32, 4)
    byte_order = atp.Property(atp.text, "little", options=("little", "big"))

    def initialize(self):
        self.pending = []
        self.log("packing frames of {} values, {}-endian".format(self.size.get(), self.byte_order.get()))

    def iterate(self):
        value = self.value.take()
        if value is None:
            return atp.IDLE
        self.pending.append(value)
        if len(self.pending) < self.size.get():
            return atp.BUSY
        layout = _FORMATS[self.byte_order.get()].format(len(self.pending))
        self.frame.write(struct.pack(layout, *self.pending))
        self.packed.write(len(self.pending))
        self.pending = []
        return atp.BUSY

    def stop(self):
        self.pending = []

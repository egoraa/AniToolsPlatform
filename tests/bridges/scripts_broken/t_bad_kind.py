"""Declares a port type the C ABI does not name: the bridge must skip it, not fail the whole load."""

import atp
from atp._kinds import Kind


class BadKind(atp.Module):
    name = "py_bad_kind"

    value = atp.Input(atp.i32)

    def iterate(self):
        return atp.IDLE


BadKind._inputs[0].kind = Kind(99, "nope")

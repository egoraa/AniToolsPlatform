"""Sits next to a broken script and must still be registered."""

import atp


class Neighbour(atp.Module):
    name = "py_neighbour"

    def iterate(self):
        return atp.IDLE

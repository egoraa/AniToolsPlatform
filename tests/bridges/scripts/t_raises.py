"""Raises on the first pass, which is how the error path is exercised end to end."""

import atp


class Raises(atp.Module):
    name = "py_raises"

    def iterate(self):
        raise ValueError("the script said no")

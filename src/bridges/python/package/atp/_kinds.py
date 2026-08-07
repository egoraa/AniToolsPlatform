"""Port payload types, mirroring atp_kind of include/atp/plugin_c.h."""

from ._errors import DeclarationError


class Kind:
    """One payload type: the code the C ABI uses and the name an error message shows."""

    __slots__ = ("code", "name")

    def __init__(self, code, name):
        self.code = code
        self.name = name

    def __repr__(self):
        return "atp." + self.name


i32 = Kind(1, "i32")
i64 = Kind(2, "i64")
f64 = Kind(3, "f64")
bool_ = Kind(4, "bool_")
text = Kind(5, "text")
blob = Kind(6, "blob")

# Sugar for prototyping. int means i64 because that is the wider of the two, but a port meant to meet
# a C++ std::int32_t must say atp.i32: the platform connects ports by exact payload type, and i32 and
# i64 are two different types rather than two sizes of one.
_ALIASES = {int: i64, float: f64, bool: bool_, str: text, bytes: blob}


def resolve(kind):
    """Return the Kind for a canonical spelling or for a Python alias."""
    if isinstance(kind, Kind):
        return kind
    try:
        return _ALIASES[kind]
    except (KeyError, TypeError):
        raise DeclarationError("unknown port type {!r}".format(kind)) from None


def canonical(kind, value):
    """Render a property value the way the platform's own string codec parses it."""
    if kind is bool_:
        return "true" if value else "false"
    if kind is f64:
        return repr(float(value))
    if kind is i32 or kind is i64:
        return str(int(value))
    if kind is text:
        return str(value)
    raise DeclarationError("a blob property is not allowed")

"""Config field declarations, read from the class body at import."""

from ._errors import DeclarationError
from ._kinds import blob, canonical, resolve

BOOL = 0
INT = 1
REAL = 2
STRING = 3
OBJECT = 4
ARRAY = 5

_KIND_CODES = {"bool_": BOOL, "i32": INT, "i64": INT, "f64": REAL, "text": STRING}

_REQUIRED = object()


def _field_kind(kind):
    resolved = resolve(kind)
    if resolved is blob:
        raise DeclarationError("a blob config field is not allowed")
    return resolved, _KIND_CODES[resolved.name]


class Field:
    """One scalar setting of a config.

    Omitting the default declares the field required: its absence from a document is then a problem
    naming the file, rather than a fallback. None is not a way to spell that — it is a value an author
    writes by mistake, and it is refused.

    A non-empty options set makes an enumeration, the same rule a Property keeps: every write is checked
    against it, the default included.
    """

    __slots__ = ("kind", "code", "default", "options", "attribute")

    def __init__(self, kind, default=_REQUIRED, *, options=None):
        self.kind, self.code = _field_kind(kind)
        if default is None:
            raise DeclarationError("None is not a config default; omit it to declare the field required")
        self.default = default
        self.options = tuple(options) if options else ()
        self.attribute = None

    def row(self):
        """The tuple the bridge reads this declaration from."""
        default = None if self.default is _REQUIRED else canonical(self.kind, self.default)
        return (
            self.attribute,
            self.code,
            default,
            [canonical(self.kind, option) for option in self.options],
            None,
            None,
        )


class Group:
    """A nested object, whose shape is another Config class."""

    __slots__ = ("schema", "attribute")

    def __init__(self, schema):
        if not (isinstance(schema, type) and issubclass(schema, Config)):
            raise DeclarationError("a group takes an atp.Config subclass")
        self.schema = schema
        self.attribute = None

    def row(self):
        """The tuple the bridge reads this declaration from."""
        return (self.attribute, OBJECT, None, [], None, self.schema._rows())


class List:
    """An array — of scalars when given a payload type, of objects when given a Config class."""

    __slots__ = ("schema", "kind", "code", "options", "attribute")

    def __init__(self, of, *, options=None):
        self.attribute = None
        if isinstance(of, type) and issubclass(of, Config):
            self.schema = of
            self.kind = None
            self.code = OBJECT
            self.options = ()
            return
        self.schema = None
        self.kind, self.code = _field_kind(of)
        self.options = tuple(options) if options else ()

    def row(self):
        """The tuple the bridge reads this declaration from."""
        children = None if self.schema is None else self.schema._rows()
        options = [canonical(self.kind, option) for option in self.options] if self.kind else []
        return (self.attribute, ARRAY, None, options, self.code, children)


class Config:
    """A module's config, declared as a class body.

    The body is read at import, before anything is created, because the host describes and validates a
    config without ever building the module that reads it — that is what fills the editor in studio and
    the catalog in MCP. Declaration order is a contract: it is the order of the rows a host shows.

    A module names its schema with `config_type`, and then reads `self.config` as an ordinary dictionary
    in which every declared key is already present at its own default::

        class SynthConfig(atp.Config):
            rate = atp.Field(atp.i64)
            engine = atp.Field(atp.text, "fm", options=("fm", "additive"))

        class Synth(atp.Module):
            config_type = SynthConfig

            def __init__(self):
                self.rate = self.config["rate"]
    """

    def __init_subclass__(cls, **kwargs):
        """Collect the declarations of the class body in the order it wrote them.

        Bases first, so a subclass extends its parent's fields rather than reordering them.
        """
        super().__init_subclass__(**kwargs)
        fields = []
        for base in reversed(cls.__mro__[1:]):
            _collect(vars(base), fields)
        _collect(vars(cls), fields)
        cls._fields = fields

    @classmethod
    def _rows(cls):
        return [field.row() for field in cls._fields]


def _collect(namespace, fields):
    for attribute, value in namespace.items():
        if isinstance(value, (Field, Group, List)):
            value.attribute = attribute
            fields.append(value)

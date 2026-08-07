"""Port and property declarations, read from the class body at import."""

from ._errors import DeclarationError
from ._kinds import blob, canonical, resolve

STATE = 0
QUEUE = 1

DROP_OLDEST = 0
DROP_INCOMING = 1


class Declaration:
    """Common part of a declaration: the payload type, and the name the class body gave it."""

    __slots__ = ("kind", "attribute")

    def __init__(self, kind):
        self.kind = resolve(kind)
        self.attribute = None


class Input(Declaration):
    """An input port.

    A queueing input keeps values until they are taken. Capacity 0 asks for the platform's own default
    limit rather than for no limit — an unbounded queue between threads of different pacing is how a
    pipeline runs out of memory with no error along the way — and then the overflow policy is unread.
    """

    __slots__ = ("flavor", "capacity", "overflow")

    def __init__(self, kind, *, queue=False, capacity=0, overflow=DROP_OLDEST):
        super().__init__(kind)
        self.flavor = QUEUE if queue else STATE
        self.capacity = int(capacity)
        self.overflow = int(overflow)

    def row(self):
        """The tuple the bridge reads this declaration from."""
        return (self.attribute, self.kind.code, self.flavor, self.capacity, self.overflow)


class Output(Declaration):
    """An output port."""

    __slots__ = ()

    def row(self):
        """The tuple the bridge reads this declaration from."""
        return (self.attribute, self.kind.code)


class BoundInput:
    """An input as the running module sees it."""

    __slots__ = ("_ctx", "_index")

    def __init__(self, ctx, index):
        self._ctx = ctx
        self._index = index

    def get(self):
        """Value the input holds, or None when it holds none."""
        return self._ctx.get(self._index)

    def take(self):
        """Next value, removed from the input, or None when there is none."""
        return self._ctx.take(self._index)


class BoundOutput:
    """An output as the running module sees it."""

    __slots__ = ("_ctx", "_index")

    def __init__(self, ctx, index):
        self._ctx = ctx
        self._index = index

    def write(self, value):
        """Deliver a value to every connected input."""
        self._ctx.write(self._index, value)


class BoundProperty:
    """A property as the running module sees it."""

    __slots__ = ("_ctx", "_index")

    def __init__(self, ctx, index):
        self._ctx = ctx
        self._index = index

    def get(self):
        """Current value."""
        return self._ctx.prop_get(self._index)

    def take(self):
        """Value if it was written since the last take, otherwise None."""
        return self._ctx.prop_take(self._index)

    def set(self, value):
        """Edit the setting from inside the module."""
        self._ctx.prop_set(self._index, value)


class Property(Declaration):
    """A setting with a default, edited live and read pull-only.

    A non-empty options set makes an enumeration: every write is checked against it, the default
    included, so a value outside the set cannot be reached from a config, from the CLI or from studio.
    """

    __slots__ = ("default", "options", "persistent")

    def __init__(self, kind, default, *, options=None, persistent=True):
        super().__init__(kind)
        if self.kind is blob:
            raise DeclarationError("a blob property is not allowed")
        self.default = default
        self.options = tuple(options) if options else ()
        self.persistent = bool(persistent)

    def row(self):
        """The tuple the bridge reads this declaration from."""
        return (
            self.attribute,
            self.kind.code,
            canonical(self.kind, self.default),
            [canonical(self.kind, option) for option in self.options],
            self.persistent,
        )

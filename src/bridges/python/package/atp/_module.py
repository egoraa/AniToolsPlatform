"""The base class an author subclasses."""

from ._ports import Input, Output, Property

BUSY = 0
IDLE = 1

ERROR = 0
WARNING = 1
INFO = 2
DEBUG = 3


class Module:
    """A platform module written in Python.

    Declare ports and properties in the class body: the body is read at import, before anything is
    created, because the host connects ports before it initialises anything.

    Override iterate; initialize, start and stop are optional. stop must be correct after initialize
    without start — a pipeline whose start cascade fails rolls back by stopping everything it had
    already initialised.

    Import a heavy dependency inside initialize rather than at module level: an import at module level
    runs when the plugin loads and is paid by every pipeline, including those that never create this
    module.
    """

    name = None
    version = (1, 0)

    def __init_subclass__(cls, **kwargs):
        """Collect the declarations of the class body in the order it wrote them.

        Bases first, so a subclass extends its parent's ports rather than reordering them: the index
        of a port is what the C ABI addresses it by, and it must not move under an existing config.
        """
        super().__init_subclass__(**kwargs)
        inputs, outputs, properties = [], [], []
        for base in reversed(cls.__mro__[1:]):
            _collect(vars(base), inputs, outputs, properties)
        _collect(vars(cls), inputs, outputs, properties)
        cls._inputs = inputs
        cls._outputs = outputs
        cls._properties = properties

    def log(self, message, level=INFO):
        """Write one line to the platform's log."""
        self._ctx.log(level, str(message))

    def wake(self):
        """Ask this module's thread to iterate now. Callable from any thread."""
        self._ctx.wake()

    def stop_requested(self):
        """Whether the pipeline is stopping."""
        return self._ctx.stop_requested()

    def iterate(self):
        """One pass of the hot path. Return BUSY or IDLE."""
        raise NotImplementedError


def _collect(namespace, inputs, outputs, properties):
    for attribute, value in namespace.items():
        if isinstance(value, Input):
            value.attribute = attribute
            inputs.append(value)
        elif isinstance(value, Output):
            value.attribute = attribute
            outputs.append(value)
        elif isinstance(value, Property):
            value.attribute = attribute
            properties.append(value)

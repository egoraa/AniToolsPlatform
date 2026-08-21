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

    A setting reaches a module by one of two channels, and which one it belongs in is decided by the
    setting rather than by taste. A Property is one scalar with a default, edited while the pipeline
    runs and read pull-only. `config` is the other: a structure — a list, a table, a nested object —
    the pipeline's author wrote down, handed over once at creation and never edited afterwards.
    """

    name = None
    version = (1, 0)

    config = None
    """This module's config: a dict, a list, a scalar or None, mirroring the JSON it was written as.

    Bound **before** __init__, which is what makes the constructor the place to read it — a config is
    what a module is allowed to know while deciding what it is, and ports are not reachable there yet.
    Nothing else is done to it: keys are looked up with ordinary dict calls, and an entry a document
    did not write is simply absent.

    It is None when the module's node named no config, so read it with a fallback rather than
    assuming a dict::

        def __init__(self):
            config = self.config or {}
            self.weights = [float(w) for w in config.get("weights", ())]

    Raising here instead makes the module unusable in a host that has no config to give it yet —
    placing it is how one would give it the config it was missing, so a constructor that refuses an
    absent config refuses the only path to a present one.

    The class attribute is this default, so the class stays usable outside a pipeline as well: an
    import-time check or a unit test of your own module can instantiate it without the bridge.
    """

    config_text = ""
    """The bytes of the file the config came from, decoded as text; empty when it came from no file.

    A config written as ``"file:rig.yaml"`` in the document reaches a module this way and this way only:
    the host parses ``.json`` and nothing else, so any other format arrives verbatim and the module
    parses it itself. That is the point — the platform learns no new formats, and a module that speaks
    one is not waiting for it to.

    Bound before __init__ next to `config`. Non-empty text and a `config` of None together mean an
    opaque file, but do not infer that from the pair — `config_opaque` answers it, because a .json
    holding literally ``null`` looks the same from here.
    """

    config_origin = ""
    """Path of that file; empty when the config came from no file.

    Worth reporting in your own errors (``f"{self.config_origin}: line 12: ..."``) and worth resolving
    paths written inside the config against, since they were written relative to it rather than to
    wherever the host happens to run.
    """

    config_opaque = False
    """Whether the host handed the file over without parsing it — that is, whether `config_text` is all
    there is.

    True exactly when the format was not one the host reads, which is what a module checks before
    parsing the text itself::

        def __init__(self):
            if self.config_opaque:
                self.rules = my_format.loads(self.config_text)
            else:
                self.rules = (self.config or {}).get("rules", ())
    """

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

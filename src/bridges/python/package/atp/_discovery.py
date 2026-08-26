"""Walking the script directories and describing what was found."""

import importlib.util
import pathlib
import sys
import traceback

from ._errors import DeclarationError
from ._module import Module
from ._ports import BoundInput, BoundOutput, BoundProperty

_REGISTRY = []


def _import_file(path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[path.stem] = module
    spec.loader.exec_module(module)
    return module


def _classes_in(module):
    for value in vars(module).values():
        if isinstance(value, type) and issubclass(value, Module) and value is not Module:
            yield value


def describe(cls):
    """Return the description table row for one module class."""
    if not isinstance(cls.name, str) or not cls.name:
        raise DeclarationError("{}: a module needs a non-empty name".format(cls.__name__))
    if cls.iterate is Module.iterate:
        raise DeclarationError("{}: a module needs an iterate method".format(cls.__name__))
    version = tuple(int(part) for part in cls.version)
    if not 1 <= len(version) <= 4:
        raise DeclarationError("{}: version takes one to four numbers".format(cls.__name__))
    return {
        "name": cls.name,
        "version": version,
        "index": -1,
        "inputs": [declaration.row() for declaration in cls._inputs],
        "outputs": [declaration.row() for declaration in cls._outputs],
        "properties": [declaration.row() for declaration in cls._properties],
        "config": None if cls.config_type is None else cls.config_type._rows(),
    }


def _discover(paths):
    """Import every *.py of every path and return the rows found by this call alone.

    A batch and not the whole registry: the host asks once per load and addresses the answer by
    position, so a second load must not see the first one's modules shifted under it. The registry
    keeps growing, and each row carries the index its class ended up at.

    A directory named twice is walked once. The two spellings arrive by different routes — one from
    ATP_PYTHON_PATH, one appended by the bridge as python/ beside itself — and they can be the same
    place, which happens whenever a module folder carries its own bridge. Walking it twice imports
    every script twice and the host then rejects the whole plugin as a duplicate registration, so the
    only symptom is a bridge that does not load at all.
    """
    table = []
    seen = set()
    for directory in paths:
        root = pathlib.Path(directory)
        try:
            key = root.resolve()
        except OSError:
            key = root
        if key in seen:
            continue
        seen.add(key)
        if directory not in sys.path:
            sys.path.insert(0, directory)
        if not root.is_dir():
            continue
        for script in sorted(root.glob("*.py")):
            try:
                module = _import_file(script)
            except Exception:
                print("atp: {} was not imported and is skipped".format(script), file=sys.stderr)
                traceback.print_exc()
                continue
            for cls in _classes_in(module):
                try:
                    row = describe(cls)
                except DeclarationError as error:
                    print("atp: {} is skipped: {}".format(script, error), file=sys.stderr)
                    continue
                _REGISTRY.append(cls)
                row["index"] = len(_REGISTRY) - 1
                row["source"] = str(script)
                table.append(row)
    return table


def _create(index, ctx, config=None, config_text="", config_origin="", config_opaque=False):
    """Instantiate the class the descriptor at `index` came from and bind its ports.

    The ports are bound after __init__ has run, so a constructor cannot reach them; that is what
    initialize is for, and the class docstring says so. Everything about the config is bound before
    __init__ for the opposite reason: it is what a module is allowed to know while deciding what it is,
    so the constructor has to be able to read self.config and, for a format the host does not parse,
    self.config_text.
    """
    cls = _REGISTRY[index]
    instance = cls.__new__(cls)
    instance.config = config
    instance.config_text = config_text
    instance.config_origin = config_origin
    instance.config_opaque = config_opaque
    instance.__init__()
    instance._ctx = ctx
    for position, declaration in enumerate(cls._inputs):
        setattr(instance, declaration.attribute, BoundInput(ctx, position))
    for position, declaration in enumerate(cls._outputs):
        setattr(instance, declaration.attribute, BoundOutput(ctx, position))
    for position, declaration in enumerate(cls._properties):
        setattr(instance, declaration.attribute, BoundProperty(ctx, position))
    return instance

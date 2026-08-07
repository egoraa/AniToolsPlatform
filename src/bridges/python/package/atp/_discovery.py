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
    }


def _discover(paths):
    """Import every *.py of every path and return the rows found by this call alone.

    A batch and not the whole registry: the host asks once per load and addresses the answer by
    position, so a second load must not see the first one's modules shifted under it. The registry
    keeps growing, and each row carries the index its class ended up at.
    """
    table = []
    for directory in paths:
        if directory not in sys.path:
            sys.path.insert(0, directory)
        root = pathlib.Path(directory)
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
                table.append(row)
    return table


def _create(index, ctx):
    """Instantiate the class the descriptor at `index` came from and bind its ports.

    The ports are bound after __init__ has run, so a constructor cannot reach them; that is what
    initialize is for, and the class docstring says so.
    """
    cls = _REGISTRY[index]
    instance = cls()
    instance._ctx = ctx
    for position, declaration in enumerate(cls._inputs):
        setattr(instance, declaration.attribute, BoundInput(ctx, position))
    for position, declaration in enumerate(cls._outputs):
        setattr(instance, declaration.attribute, BoundOutput(ctx, position))
    for position, declaration in enumerate(cls._properties):
        setattr(instance, declaration.attribute, BoundProperty(ctx, position))
    return instance

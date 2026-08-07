# Python module template

A platform module written in Python and loaded through the **C path** of the ABI
(`include/atp/plugin_c.h`) by the `atp_python_bridge` bridge. There is nothing to compile: two
scripts, `averager.py` and `packer.py`, and a config to go with them.

```bash
cp averager.py packer.py <next to atp_app>/plugins/python/
cp pipeline.json <next to atp_app>/config/
atp_app config/pipeline.json
```

The bridge and the `atp` package already sit next to `atp_app` — they ship with the platform. The
scripts need nothing but their own directory.

The two are deliberately unlike each other. `averager.py` is the short one to start from: a state
input, two outputs, three properties. `packer.py` shows what the first one leaves out — a **queueing**
input, and a **blob** output, which is the escape hatch for a payload the closed type set of the C
path does not name.

## The `atp` package beside the script

In the release, next to this README, there is an `atp/` directory — the same package that sits next
to `atp_app`, placed here for the **editor**. Without it `import atp` does not resolve and the module
is written blind: no completion and no type checking. The package is pure Python, so
`python -c "import averager"` works with no host at all, which makes a typo in a port declaration
visible before the pipeline is ever run.

Do not copy this directory over to `atp_app`: `plugins/python/` already holds the platform's own
package, and the copy from the template may well be the older of the two. What goes there is still
`averager.py` and nothing else.

## Where the bridge looks for scripts

Two places, and both are scanned for top-level `*.py`:

- `python/` next to the bridge library itself — `plugins/python/` in a release, next to the bridge
  and not next to the config that names it;
- the directories listed in `ATP_PYTHON_PATH` (separator is platform-native: `;` on Windows, `:`
  elsewhere). This is how you work on a script inside your own repository without copying it into the
  build directory.

The `atp` package lives in that same `python/`, but it is a directory rather than a `.py`, so the
scan leaves it alone.

## What the class declares

```python
import atp


class Averager(atp.Module):
    name = "py_averager"
    version = (1, 0)

    value  = atp.Input(atp.i32)
    report = atp.Output(atp.text)
    window = atp.Property(atp.i32, 4)

    def iterate(self):
        ...
        return atp.BUSY
```

The class body is read **at import time**, before anything has been created, and that is an ABI
requirement rather than a matter of style: the host connects ports before it initializes modules, so
a port that does not exist until the first connection cannot be connected. Declaring a port in
`__init__` will not work.

`name` and `iterate` are required; `version` defaults to `(1, 0)`. `initialize`, `start` and `stop`
are optional — no method, no call. Ports are bound to the instance **after** `__init__`, so they
cannot be read in the constructor; `initialize` is there for that.

One platform contract that does not follow from the signatures: **`stop` has to be correct after
`initialize` without `start`**. A pipeline whose start failed rolls back by calling `stop` on
everything that managed to initialize.

## Port types

The canonical spellings are `atp.i32`, `atp.i64`, `atp.f64`, `atp.bool_`, `atp.text` and `atp.blob`.
The Python `int`, `float`, `bool`, `str` and `bytes` are aliases, in which `int` means `i64`.

The difference is not cosmetic. A C++ neighbour declares its port as a specific `std::int32_t` or
`std::int64_t`, and the platform connects ports by exact payload type, so when connecting to an
existing module, spell the type out. And `int` in Python has no width: writing 2³¹ into an `i32` port
is an error naming the port, not a silent truncation.

`blob` accepts `bytes`, `bytearray` and in fact any object with a buffer protocol, so an ndarray
travels as a single copy while the bridge itself knows nothing about numpy.

## An exception is a stopped pipeline

An exception out of `iterate`, `initialize`, `start` or `stop` is caught by the bridge at the
boundary, formatted together with its traceback and handed to the host as an ordinary module error.
The pipeline stops the normal way, and the message shows the file and the line of the script:

```
atp_app: module 'py_averager': iterate failed: iterate raised: Traceback (most recent call last):
  File ".../plugins/python/averager.py", line 35, in iterate
    raise RuntimeError("the fail property was set")
RuntimeError: the fail property was set
```

You can watch it on a live pipeline: the module has a transient `fail` property, and
`atp_app -p mean.fail=true config/pipeline.json` shows the whole path.

## Worth knowing in advance

- **There is no hot reload.** There is one interpreter per process and it is never finalized; to see
  an edit to the script, restart `atp_app`.
- **The GIL is shared.** Two Python modules on two threads serialize on it. Numpy and torch release
  the GIL inside their own loops, so they compute in parallel and compete only for the Python
  wrapping.
- **Heavy imports belong in `initialize`.** An `import torch` at module level runs when the bridge
  imports the script — that is, while the plugin is being loaded, before any module exists — and is
  paid for by every pipeline, including one that never creates this module.
- **A broken script is skipped.** A file that fails to import or declares itself wrongly prints a
  traceback to stderr and drops out; its neighbours still register. If a config then names its
  module, the error will read "unknown module" — look further up the log, at stderr.
- **`service_directory` and groups are out of reach** — the C path does not have them at all.

## License

The template is part of AniToolsPlatform and is covered by the Apache License 2.0: a copy of the text
sits in `LICENSE` beside it, copyright 2026 The AniToolsPlatform Authors.

License your own module however you like, up to entirely closed. Once you have copied the directory,
replace `LICENSE` with your own — or your code ends up published under someone else's copyright. The
one thing Apache-2.0 asks in return is that you keep the notices in whichever template files you
leave as they are (section 4(c)).

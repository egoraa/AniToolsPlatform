# The Python bridge

`src/bridges/python/` is a plugin of the C path that embeds CPython, so a module can be a script.
`ATP_BUILD_PYTHON_BRIDGE` is `ON` by default but the subdirectory returns early when `find_package(Python3
3.11 COMPONENTS Development.Embed)` fails, so a machine without the development files configures unchanged and
the CI job `python plugin` fails loudly rather than passing empty. It is built with `Py_LIMITED_API` against
the **version-independent** library — whatever the platform names the file `find_library(NAMES python3)` turns
up — which is what lets one binary serve any CPython 3.11+. **Linking the versioned library instead silently
undoes that**, and that is exactly the fallback when no version-independent one is found: the bridge is then
pinned to the interpreter it was built against, so read the configure line, which says which of the two was
used. The floor is 3.11 because the buffer protocol entered the stable ABI there. The author-facing half is a
pure Python package under `package/atp`, copied to `python/atp` **next to the built library** — that path is
not a convention but where the bridge looks, and `ATP_PYTHON_PATH` adds further scan directories. Two traps
worth knowing before touching it: the GIL must be released with `PyEval_SaveThread` right after
`Py_Initialize` or the runner deadlocks on its first `iterate` (no single-threaded test can see this), and the
library pins itself into the process because the interpreter is never finalised while `module_loader` would
otherwise unload the code Python still calls. A script may **declare** its config — `atp.Config` in the class
body, named by `config_type` — and then `self.config` arrives with every declared key at its own value,
defaults included, so no fallback is needed. The declaration crosses as `atp_config_field_desc`, and its
storage is a **deque** of vectors in `module_slot`: a nested field points into a sibling vector and a vector
of vectors would rehome them. Without a `config_type` a module's config arrives as an ordinary `dict` bound
before `__init__`, and beside it three fields for a config the document attached as a file: `config_text` (the
bytes, **decoded as UTF-8 strictly**, so a file in another encoding fails the module's creation with the file
named rather than handing the script mojibake), `config_origin` and `config_opaque`. The last one is not
redundant — a `.json` holding literally `null` also leaves `config` at None beside a non-empty text. Rationale
in full: `docs/architecture.md`, section «Мост для Python».
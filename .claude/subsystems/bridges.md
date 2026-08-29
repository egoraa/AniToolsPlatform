# The script bridges

`src/bridges/python/` and `src/bridges/lua/` are the second and third consumers of the C path, each
embedding an interpreter so that a module can be a script. Each has its own build switch and its own
author-facing package; what follows first holds for both.

## Both bridges

**Both bridges carry paths as UTF-8 and never as `path::string()`.** On Windows that conversion goes
through the process code page and **throws** for anything it cannot represent, and the throw escapes
`atp_module_count` — an `extern "C"` entry point `plugin_c.h` requires to be exception-free — so a
module folder named in Cyrillic takes the whole bridge down instead of failing to list one script. The
scan variables are read wide (`_wdupenv_s`) for the same reason: a narrow read replaces what the page
cannot encode and the directory is then silently absent, with no error anywhere. `plugin_c.h` states
the UTF-8 rule; the regression tests (`tests/bridges/*_bridge_tests.cpp`, helper `unicode_name`) build
their non-ASCII names from numeric code points rather than from a literal, so the name does not depend
on how the source was read — a literal is right only for as long as `/utf-8` is in force, and a test
that goes quietly vacuous when a build setting slips is the very failure
`tests/support/source_encoding_tests.cpp` exists to catch.

**Each bridge materialises the config tree once per instance** (`dict`/`list` in Python, a table in
Lua) and carries three more fields beside it — `config_text`, `config_origin`, `config_opaque` — bound
where `config` is: before `__init__` in Python, at `atp._instantiate` in Lua. `config_opaque` is not
redundant, since a `.json` holding literally `null` also leaves the tree empty beside a non-empty text.
They **differ on encoding on purpose**: Python decodes the text as UTF-8 strictly, so a file in another
encoding fails the module's creation naming the file rather than handing the script mojibake, while Lua
hands the bytes over as they are, a Lua string being bytes. A Lua table also loses key order, which
nothing addresses by position. `docs/architecture/config.md`, "The bridges".

## The Python bridge

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
before `__init__`. Rationale in full: `docs/architecture/bridges.md`, "The Python bridge".

## The Lua bridge

`src/bridges/lua/` **vendors** its interpreter: `cmake/BuildLua.cmake` fetches PUC-Lua and links it
statically, so unlike the Python bridge it has no runtime that can be missing — `ATP_BUILD_LUA_BRIDGE` (ON)
only says whether this build wants it, and OFF skips the download too. Three things about it are
load-bearing. It is **compiled as C++**, because Lua built as C raises errors with `longjmp` and every
callback here is a frame with C++ destructors in it — hence also the rule that **no frame between
`lua_pcall` and `luaL_error` may own an object with a non-trivial destructor**, and hence `lua_api.hpp`
including the headers without `extern "C"`. Each module instance owns its **own `lua_State`**, so instances
run in parallel and there is no `PyEval_SaveThread` trap, no `pin_self`, and no "one bridge per process" —
the price is that the script's top level runs per instance and its values are not shared. And since the
script is therefore read twice, `atp._instantiate` is handed the port counts the host was promised and
refuses a file edited in between. The author package is one file, `lua/atp.lua` next to the library
(`ATP_LUA_PATH` adds scan directories); a module may not be named `atp`, and the directory walk skips that
file. The declaration order that the C ABI addresses ports by is kept by an `__newindex` proxy, because
`pairs` has no order — do not "simplify" it to a plain table. A script declares its config with
`atp.config(...)`; **`atp.group` and `atp.list` of objects take a function, never a table** — `pairs` has no
order and field order is a contract, so a nested object is collected by the same `__newindex` proxy the
declarations use. A module's config is an ordinary table on the instance. This bridge also **reads scripts
itself** and loads them with `luaL_loadbuffer` rather than `luaL_loadfile`, which would `fopen` a narrow
name, and preloads its `atp` package into `package.loaded` so `require` never depends on that path either.
Rationale in full: `docs/architecture/bridges.md`, "The Lua bridge".

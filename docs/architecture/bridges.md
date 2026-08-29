# Script-language bridges (`src/bridges/`)

The second and third consumers of the C path: each embeds an interpreter, so that a module can be a
script. The platform knows nothing about either Python or Lua — both bridges are ordinary C-path
plugins, declared with the same `atp_add_plugin(... C_ABI ...)`, over the path described in `sdk.md`.
The document map and the tree layout are in `../architecture.md`.

## The Python bridge (`src/bridges/python/`, target `atp_python_bridge`)

The first consumer of the C path, and one that path foresaw: `plugin_c.h` names "a bridge embedding an
interpreter" outright and provides `user_data` for it ("one `create` serves many descriptors") and
allows long work in `atp_module_count` ("a bridge scanning a directory of scripts").

Why at all: prototyping and glue. Writing a filter or a stub and trying it without rebuilding C++ —
here the edit cycle matters more than execution speed.

**The bridge lives in the platform's tree, not in a template the author builds themselves.**
Considered and rejected: a template bridge would tie the binary to whatever Python stood on the
author's machine at build time, and the script would stop working the moment the platform was
installed — which is exactly what prototyping is expected to survive. The price of the decision is
that the bridge ships whenever CMake found CPython.

**Three parts, and the boundary between them.** C++ registers a built-in module `_atp` with a single
type, `Ctx`, which is nine wrappers over `atp_api`. The `atp` package, in pure Python on top of it,
provides the class API (`Module`, `Input`, `Output`, `Property`), walks directories, collects
subclasses and checks declarations. The `templates/plugin_python/` template is only a script and a
config. The boundary is drawn so that everything that will change often — sugar, error texts, checks —
changes without a rebuild, while what is unavoidable stays in C++: the interpreter's lifetime, the
GIL, ownership of descriptor memory, conversion of the six `atp_kind`s, and turning an exception into
`set_error`.

**Stable ABI, floor 3.11.** The bridge compiles with `Py_LIMITED_API` and links the version-independent
`python3.lib`/`libpython3.so`, so one shipped binary works with any CPython 3.11+; the versioned
library remains a fallback, and configure reports which was chosen — a difference invisible until the
day somebody upgrades Python. The floor is 3.11 precisely because the buffer protocol entered the
stable ABI there: without it `blob` would accept neither a `memoryview` nor an ndarray without making
numpy a dependency of the bridge.

**Discovery is per-load.** `atp_module_count` scans `python/` beside the library itself (in a shipment
that is `bin/plugins/python/`, next to the bridge rather than next to the config that references it)
plus the `ATP_PYTHON_PATH` directories, and returns the batch of **this** load. Not a shared registry:
the host asks for the count once and then addresses the answer by position, so a second load must not
see the first one's modules shift underneath it. What travels in `user_data` is a pointer to a slot
rather than an index, because there are two numbers and they differ — the descriptor's position in the
bridge's storage and the class's position in the package's registry. The storage is never freed: the
host holds pointers for as long as a registration lives.

**The bridge pins its own library into the process** (`GET_MODULE_HANDLE_EX_FLAG_PIN`,
`RTLD_NODELETE`) — not out of caution, but as a consequence of the decision not to call `Py_Finalize`,
which is unsafe with extensions in the process. The `Ctx` type and every function pointer Python can
see live in that library, while the interpreter outlives any individual load; `module_loader` unloads
the library when it is destroyed, and the next call from Python would go to a freed address.
Withdrawing factories still works as usual — only the code memory stays pinned.

**The GIL is released immediately after `Py_Initialize`.** Initialization leaves the GIL held by
whichever thread happened to load the plugin, while every entry point takes it through
`PyGILState_Ensure` from the module's own thread — without `PyEval_SaveThread` the runner deadlocks on
the first `iterate`. A single-threaded test does not see this: the defect showed up only in a live
`atp_app` run, which is why the bridge has a CI check rather than unit tests alone.

**Errors.** An exception is caught at every entry point, formatted by the `traceback` module (with the
script's file and line) and passed to `set_error` — the boundary is exception-free in both directions,
and an exception left pending would break the next call into the interpreter in an invisible way. A
script that will not import prints its traceback to stderr and drops out of the table while its
neighbours register. One acknowledged limitation: a C-path plugin has no "did not load, and here is
why" channel — with the package missing, the bridge returns zero modules and prints a line naming
where it looked.

**Value conversion is chosen by the port's declared `kind`, not by the Python argument's type**, and
three rules follow that no correspondence table shows. Overflow is **an error naming the port, not a
truncation**: Python's `int` is unbounded, and writing 2³¹ into an `i32` port is exactly the typo
nobody else would catch. The Python-ward direction copies into a fresh object immediately, so the
host's scratch-buffer lifetime rule never reaches the script — the `bytes` you receive can be held as
long as you like; in the other direction the reference is held only for the duration of the call,
which is both required and sufficient. Property defaults and `options` are rendered into canonical
strings by the Python side (`True` → `"true"`), because the same code parses them as parses values
from the config; a `blob` property is forbidden by the ABI, and the package refuses it at the
declaration.

**Port indices are handed out in body-of-class declaration order** and coincide with the ABI's
indices, so `self.value.take()` is `take(ctx, 0)` with no lookup by name on the hot path. That is also
why a port cannot be declared in `__init__`: the class body is read at import time, and the host
connects ports before it creates a single instance.

**Reloading the bridge in the same process** (a second studio session, for instance) runs discovery
again and **re-reads the files from disk**: scripts are loaded through
`spec_from_file_location`/`exec_module`, which execute the module regardless of `sys.modules`. An edit
to a script is therefore visible from the next load of the plugin — but not inside a running pipeline,
so this is not hot reload. The `sys.modules` cache affects only what a script imports itself, and names
there are global: **two scripts with the same name in different directories will collide**, which is
also why the test fixtures must be named differently.

**The studio reloads the whole plugin rather than teaching the bridge incremental discovery.** The
`File → New Python module…` item writes a script and must show the module in the palette at once, but
the bridge can only see a new file on its next load. A naive second load into the same registry fails
as a duplicate: `_discover` re-reads the files and returns **all** the classes, not only the new ones.
Considered and rejected — returning only previously unknown `(name, version)` pairs on a repeat load:
that would break the contract that `atp_module_count` returns the batch of this load, and would make
discovery global to the process, so a second registry in the same process would get nothing.
`module_manager::reload_plugin` instead kills the previous loader — which withdraws exactly its own
registrations — and reads the file anew; the library stays in the process because the bridge pinned
itself, so the interpreter and the descriptor storage survive the reload. The price is that a reload
withdraws and re-registers **all** of the bridge's modules, which is why it is forbidden while the
pipeline runs: the tree holds modules whose factories would otherwise vanish from under it. The
function is not Python-specific — the same call picks up a rebuilt C++ plugin.

**The bridge's descriptors live forever, and a reload appends a batch rather than replacing one.** A
module built from a descriptor holds a pointer to it for its whole life and dereferences it once more
when destroyed — that is the `plugin_c.h` contract ("the pointers must live until the library is
unloaded"), and since the bridge is pinned in the process, "until unloaded" means "always". Keeping
the batch in one reusable vector is invisible while discovery happens once per process; with a reload
button it becomes a use of freed memory, because a stopped pipeline **still owns its modules**
(`session::stop` stops the runner while the tree lives until the next `start`) and a reload with the
pipeline stopped is exactly what is allowed. So `discover()` starts a new batch in a deque that is
never cleared, the same discipline as the string and port storage beside it. The price is memory
growing with the number of reloads in a session.

**The plugins dock's refresh button re-reads what is loaded, not only looks for new files.** By its own
contract `rescan` leaves a loaded plugin alone — that is what it was written for, so that walking a
directory again breaks nothing — while the bridge reads scripts only at load time. The intersection of
those two rules meant an edit to a `.py` never reached the palette however often you pressed: the
version and the ports stayed as they registered on the first load. So `module_manager` has
`reload_all` — re-read everything loaded — and the button calls it first, and only then `rescan` for
new files. The paths are taken before the first reload, because a reload rebuilds the list itself;
files that failed earlier are left to `rescan`, which repeats them anyway. While the pipeline runs,
re-reading is skipped with a line in the log: factories the live tree holds cannot be withdrawn,
whereas loading a file that is new to the session only adds.

**`ATP_PYTHON_PATH` is derived from the search directories rather than stored separately.** A module
directory *is* a plugin directory — that is the point of it carrying its own bridge — so a second list
saying "also look here for scripts" would show the same directory twice and diverge from the first on
any hand edit. The studio keeps one list, the search directories, and tells the bridge about the
`python/` of each one that exists (`derive_script_dirs`). A directory of scripts with no bridge is
added to that list too: it means "where the studio looks for modules", not "where plugins lie".

**It appends rather than overwrites.** The derived directories go first and the value the studio was
started with stays as the tail: an author working on scripts in their own repository launches it with
the variable already set (the README advises exactly that), and overwriting would remove their modules
for no visible reason. The inherited value is taken once at startup, before the first assignment, or
each recomputation would grow the tail onto itself.

**A module directory repeats the shape of a shipment, not the shape of "just a folder with scripts".**
`File → New Python module…` puts the bridge and the package into the chosen directory
(`atp_python_bridge.dll` beside it, `python/atp` within) and writes the script into `python/`, next to
the package. The layout is not a matter of taste: the bridge looks for the package strictly in
`python/` beside itself and scans that same `python/`, so the single copy of the package lying there
settles three things at once — the bridge starts, the scripts are found with no environment variable,
and an editor opened on a script resolves `import atp` without any project setup. The directory is
portable with it: `atp_app`, shown that directory, needs nothing else.

**The package is updated, the bridge is not.** The package is platform code rather than the author's,
and a copy one release behind breaks in a way that points nowhere near it, since directory walking
lives precisely there. So the package is overwritten when the source is newer; freshness is measured
over `.py` files and `__pycache__` is ignored — otherwise a `.pyc` compiled on the very first import
makes a stale copy look new and the update never fires. The bridge file is only created: a copy already
loaded into the process cannot be overwritten at all, so a stale one is named in the log rather than
rewritten halfway. The copy source is the **studio's installation** (`plugins/` beside the executable),
and only as a last resort the loaded bridge: what is loaded very often turns out to be the copy inside
the module directory, because the studio makes such a directory a search directory — and then the
directory would become a source for itself.

**One directory named twice is walked once.** The two spellings arrive by different routes — one from
`ATP_PYTHON_PATH`, the other appended by the bridge as `python/` beside itself — and they can be the
same place; which is exactly what happens when the directory carries its own bridge. Walking twice
imported every script twice, and the host rejected the whole plugin as a duplicate registration, so
the only symptom was a bridge that "did not load". Deduplication lives in `_discovery`, keyed by the
resolved path, rather than in `scan_paths`: every path from both sides passes through `_discover`.

**Two copies of the bridge in one process is a limitation, not a supported mode.** Each library builds
the `Ctx` type in a static of its own, and only the copy that managed to register `_atp` in the inittab
fills it; the rest register their modules and then refuse to create them with `the _atp module was
never initialised`. Since every module directory carries a copy and is itself a search directory, a
scan reaches several — so after every scan `keep_one_python_bridge` withdraws all but the first loaded
one (which is also the one that initialized the interpreter, so this is not an arbitrary choice) and
says so in the log. Withdrawal is possible because `module_manager` has `unload_plugin`, the pair of
`load_plugin`: the loader dies and revokes exactly its own factories, while modules already created
live on, each pinning the library through its own deleter.

**Any** surplus copy is withdrawn, not only the ones that loaded, and failures are the ordinary case
rather than the exception: the host has one registry, so a second copy tries to register names the
first already holds, `module_registrar::add` refuses the duplicate and the file is revoked whole. If
such a row remained, the dock would show a permanently red "failed" about a file the session
deliberately does not use. The one exception is a failed bridge with none loaded beside it: there the
error text is all a person has, and it usually speaks not about the bridge but about the CPython
environment.

**A stale bridge is named aloud at every scan, not only when a module is created.** The studio does
not replace a folder's copy and cannot — a loaded library is locked — so a folder provisioned before a
platform update keeps loading its old bridge indefinitely, and everything the new version added is
simply absent. That absence has no error of its own: the modules load and work, they merely have
nothing to tell the host about what the old bridge could not do — and it reads as the studio being
broken rather than as a stale file. Hence `stale_loaded_bridge` is asked both at startup and on the ↻
button, and the warning names both files and the way out: delete the copy and scan again. One freshness
rule serves both places (`bridge_copy_is_stale`): older, yes; merely different, no; and an unreadable
timestamp does not count as proof of age, because the advice that follows is to delete the file.

**A module may not be named `atp`.** The script would land as `python/atp.py` beside the `python/atp/`
package, the directory walk goes in sorted order and hands the file to `_import_file`, which
unconditionally writes `sys.modules["atp"]` — after which `import atp` in every later script leads to
that file, `atp.Module` is not found, and the folder stops loading entirely. Shadowing works this way
for any importable name, as in any directory on `sys.path`; it is called out separately because this
one takes the bridge's own package with it.

**The GIL's price is stated outright, because it is not visible from the API:** two Python modules on
two runner threads serialize on it. Heavy numerical libraries release the GIL inside their own loops,
so they compute in parallel and contend only for the Python wrapping between calls.

What a Python module does not have: hot reload (one interpreter per process), zero-copy for `blob`,
and access to `service_directory` and groups. The first follows from the second decision; the other
two are inherited from the C path entirely.

**Considered and deferred.** *An interpreter in a separate process* gives an independent GIL, crash
isolation and its own environment for torch, at the cost of serialization on every value, latency,
handling the process's death, and twice the code. It is deferred precisely because from outside — the
config, the ports, the module names — it is indistinguishable from the current variant, so the move
remains possible later without touching scripts or configs. *Choosing a venv through
`Py_InitializeFromConfig`* — `PyConfig` is not part of the stable ABI, so a venv is attached by adding
its `site-packages` to `sys.path`; that works as long as the venv was created on the same minor CPython
as the interpreter being run, which binary packages require anyway. *Testing the package through
pytest* — a new CI dependency to duplicate what the bridge's C++ test already covers.

A script can **declare** its config rather than only read it: `atp.Config` with fields in the class
body (`atp.Field`, `atp.Group`, `atp.List`), named by the module through `config_type` — the mirror of
`using config_type = ...`. Class-body order is a contract, since the inspector's rows follow it, and it
is collected by the same `__mro__` walk as the ports, so that a subclass extends its parent rather than
reordering it. Required means the **absence** of a default, and `None` is refused separately: that is
what an author writes by mistake, and silently turning it into "a required field" would hide the typo.
`atp.blob` is refused as it is in properties — a binary value has no canonical string form, and the
whole string layer rests on one. Having declared a schema, the module reads `self.config` with no
fallbacks: every declared key is in place, with defaults filled in by the host.

The field descriptors live in `module_slot::config_fields` — a **`std::deque` of vectors, not a
vector**: a nested field holds a sibling vector's `data()`, and rehoming on growth would leave every
such pointer dangling.

## The Lua bridge (`src/bridges/lua/`, target `atp_lua_bridge`)

The third consumer of the C path, after the Rust template and the Python bridge, and written after
both — so what is interesting is not what it has in common with them but how it differs from the Python
one. The host, `module_loader`, `c_module.hpp`, `plugin_c.h` and the config schema did not change by a
line: the ability to load modules in a third language fitted entirely into a new plugin.

**The interpreter is vendored rather than found.** Lua has no stable ABI between its 5.x versions and
no convention for shipping one shared interpreter, so a bridge built against whatever happened to be on
the build machine would be a bridge that works only there. The sources are 30k lines under MIT, which
makes the other direction cheap: `cmake/BuildLua.cmake` fetches the archive with `FetchContent` and
links a static `atp_lua` into the plugin. That removes the entire class of failures for which the Python
bridge needed `reads_as_missing_dependency` and a separate CI check for "did it even build": the Lua
bridge has no missing dependency, and if `atp_app` starts then a Lua module works.

**The interpreter is built as C++, and that is not taste.** Built as C, Lua unwinds errors through
`longjmp`, and a `longjmp` out of a C function whose caller's frame holds a C++ object with a
destructor skips that destructor — and every callback of the bridge is exactly such a frame. Built as
C++, the same machinery becomes `throw`/`catch` and unwinds correctly. Hence a rule to remember for any
edit: **there must be no frame owning an object with a non-trivial destructor between `lua_pcall` and
`luaL_error`**. Lua's headers carry no `extern "C"`, so `lua_api.hpp` includes them without it — and
includes them in exactly one place, so that this connection stays checkable.

**State per instance, not per process.** Lua has neither a global interpreter nor a lock around one, so
every module instance owns its own `lua_State` and runs on the runtime's thread serializing with
nobody — a property the Python bridge cannot have. Three consequences. The `PyEval_SaveThread` trap
right after `Py_Initialize` does not exist here at all. Neither does "one bridge per process": `Ctx`
does not live in a static, there is no inittab race, and a second copy of the library does not obstruct
the first — so the bridge has no analogue of `pin_self`, and the studio needs no `keep_one_bridge` for
it. The price is that the top level of the script executes per instance: top-level values are not
shared between instances, and expensive work belongs in `initialize`. The template says so.

**The script is read twice**, and that is the price of the same decision: once at load time to describe
the module, and again when an instance is created. A file edited between those moments would declare
ports the host never connected — so `atp._instantiate` receives three promised sizes and refuses with
"the script declares different ports than when it was loaded". The Python bridge cannot drift this way:
it holds the class object.

**Declaration order rests on a proxy.** The C ABI addresses a port by index, and `pairs` in Lua is
unordered, so `atp.module` returns not a table but a proxy with `__newindex`: assignments to
declarations land in a list in written order, and everything else becomes an instance method. This is
the only place where order is guaranteed, and it cannot be "simplified" into an ordinary table.

Small things, each of which would already have cost a debugging session. `text` and `blob` both map
onto `lua_pushlstring`/`lua_tolstring`, because Lua strings are bytes — the reason the Python bridge
requires 3.11, the buffer protocol, simply does not exist here. Reading a string is checked with
`lua_type(...) == LUA_TSTRING` rather than trusted to `lua_tolstring`, which silently **rewrites a
number on the stack** into a string. A boolean port demands a real boolean, because Lua's truthiness
would accept any string and any number as `true`. A module cannot be called `atp`: the package is
`atp.lua` in the same directory, and the directory walk skips a file with that name.

A script declares its config through `atp.config(...)` assigned into the module's table, with fields
`atp.field`, `atp.group` and `atp.list`. **`atp.group` and `atp.list` of objects take a function rather
than a table**, and this is the one divergence from Python: `pairs` has no order while field order is a
contract, so a nested object is collected by the same `__newindex` proxy as the module's declarations.
A table literal would lose the order. The declaration rows are six-place in both bridges and match
field for field.

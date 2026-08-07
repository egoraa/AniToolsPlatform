# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Machine-specific setup — the exact generator, toolchain paths, Qt kit location and build directory of
the current machine — belongs in `CLAUDE.local.md` at the repo root, which is gitignored and per-developer.
Keep **this** file portable: anything naming an absolute path or a single operating system goes there, not here.

## Build and test

CMake ≥ 4.1, C++23. There is **no CMakePresets.json** — it was dropped in `295e533`; configure the build
directory directly. Pick the build directory and generator per machine:

```bash
cmake -S . -B <build-dir> [-G <generator>] [-DCMAKE_PREFIX_PATH=<qt-kit>]   # first configure; -G and the prefix path stick in the cache
cmake -S . -B <build-dir>                        # reconfigure after touching any CMakeLists
cmake --build <build-dir> [--config Debug]       # build everything
cmake --build <build-dir> [--config Debug] --target atp_tests
ctest --test-dir <build-dir> [-C Debug]          # or run the test exe directly
```

**Multi-config vs single-config matters.** With a multi-config generator (Visual Studio, Xcode, Ninja
Multi-Config) the configuration is chosen at **build** time with `--config`, and the test binary lands in a
per-config subdirectory; with a single-config generator (plain Ninja, Unix Makefiles) it is chosen at
**configure** time with `-DCMAKE_BUILD_TYPE` and there is no extra subdirectory. The `--config`/`-C` flags
above are no-ops in the single-config case.

**Quality knobs**, both off by default so an ordinary build is unaffected. `-DATP_WERROR=ON` turns warnings
into errors — CI sets it on every job, a local build should not, because a new compiler version routinely
finds new ones. `-DATP_SANITIZER=<value>` takes a `-fsanitize=` value (`thread`, `address`,
`address,undefined`) and instruments the **whole** build tree, vendored dependencies included, since mixing
instrumented and uninstrumented code costs ASan false positives and blinds TSan. MSVC implements
AddressSanitizer only and the configure step says so for the rest, so `thread` — the one that checks the io
layer's threading contracts — means clang or gcc. Its findings are what `.clang-tidy` and the compiler cannot
see; the two CI jobs are `sanitizer / thread` and `sanitizer / address,undefined`.

Static analysis (`.clang-tidy`, the `tidy` target), formatting (`.clang-format`, the `format` target, **pinned
version**) and the Doxygen API reference are covered by the `repo-tooling` skill — invoke it before running
any of them. All three are enforced by their own CI jobs.

`atp_studio` needs Qt 6 Widgets: pass the kit as `-DCMAKE_PREFIX_PATH` on the first configure (the cache keeps
it afterwards). It is built when `ATP_BUILD_STUDIO` is `ON` (the default) and Qt6 is found, so a machine
without Qt configures unchanged. The option gates **the GUI only** — the headless studio core
(`atp_studio_lib`) and `atp_mcp` are built either way, since they never mention Qt and `atp_tests` covers
them; `OFF` additionally skips looking for Qt at all. The `ubuntu / clang` CI job builds with `OFF` so that
configuration keeps being exercised.

Plugin file names are **toolchain-agnostic** (`PREFIX ""` + `OUTPUT_NAME`, e.g. `atp_demo_plugin.dll` /
`atp_demo_plugin.so` from the same target name), and plugin paths in configs may omit the extension —
`module_loader` appends the platform one (`atp::plugin_extension`: `.dll`/`.so`/`.dylib`). Declaring a
plugin is `atp_add_plugin(<name> [C_ABI] SOURCES ...)` from `cmake/AniToolsPlatformPluginHelpers.cmake`,
which sets that naming plus hidden visibility, the `MODULE` type and a link to `atp::platform` alone.
`C_ABI` declares a plugin of the pure C path instead: it gets the SDK's include directory and no link,
because linking `atp::platform` would impose `cxx_std_23` on a target that may have no C++ compiler
behind it — which is the independence that path exists for. The
file is installed verbatim into the package and included both by the package config and by the root
`CMakeLists.txt`, so the in-tree plugins are declared by the very function an out-of-tree author calls
— **do not hand-roll a plugin target**, and when changing the helper remember both callers.

`atp_warnings` carries a C branch next to the C++ one, guarded by `$<COMPILE_LANGUAGE:C>` — without it the
tree's C sources would compile with no diagnostics at all. It is the GNU set minus the C++-only options,
since gcc and clang complain about being handed one for a C unit and `ATP_WERROR` turns that complaint
into the failure. **`atp_runtime` carries `/bigobj` as an MSVC INTERFACE option**, and that is where it
belongs rather than in `atp_warnings`: it is a technical consequence of a header living there, not a
policy. `module_loader.hpp` includes `c_module.hpp`, which instantiates the ports of every `atp_kind`, and
MSVC emits those COMDATs into every unit that includes the loader whether it calls anything or not
(measured: 25 sections → 488) — both `atp_tests` and `src/mcp/main.cpp` crossed the object format's
section limit. Riding on `atp_runtime` reaches exactly the targets that include the loader and no
further, since that target is deliberately not exported.

`templates/plugin/` is a plugin project **outside** this build — it is not `add_subdirectory`'d and
reaches the SDK only through `find_package`. It doubles as the fixture of the `out-of-tree plugin` CI
job, the one place where both ends of a connection come from different libraries. It names the ABI it
targets (`atp_require_plugin_abi(10)`), so **bumping `plugin_abi` means editing that file too** or the
job stops configuring — which is the intended feedback, not breakage.

`src/bridges/python/` is a plugin of the C path that embeds CPython, so a module can be a script.
`ATP_BUILD_PYTHON_BRIDGE` is `ON` by default but the subdirectory returns early when
`find_package(Python3 3.11 COMPONENTS Development.Embed)` fails, so a machine without the development
files configures unchanged and the CI job `python plugin` fails loudly rather than passing empty. It
is built with `Py_LIMITED_API` against the version-independent import library, which is what lets one
binary serve any CPython 3.11+ — **linking the versioned library instead silently undoes that**, and
the configure line says which one was used. The floor is 3.11 because the buffer protocol entered the
stable ABI there. The author-facing half is a pure Python package under `package/atp`, copied to
`python/atp` **next to the built library** — that path is not a convention but where the bridge looks,
and `ATP_PYTHON_PATH` adds further scan directories. Two traps worth knowing before touching it: the
GIL must be released with `PyEval_SaveThread` right after `Py_Initialize` or the runner deadlocks on
its first `iterate` (no single-threaded test can see this), and the library pins itself into the
process because the interpreter is never finalised while `module_loader` would otherwise unload the
code Python still calls. Rationale in full: `docs/architecture.md`, section «Мост для Python».

`templates/plugin_rust/` is the second out-of-tree project and the fixture of the `rust plugin` CI job: a
`cdylib` built by `cargo build` alone, with no dependencies and no CMake, whose `src/abi.rs` is a
**hand-written mirror** of `include/atp/plugin_c.h`. Two consequences. Changing the layout of anything in
that header — reordering or retyping a field, not just adding one — silently breaks every foreign mirror,
which is why `tests/platform/plugin_c_layout_tests.cpp` pins the sizes and offsets with `static_assert`;
update both together, and bump `ATP_C_ABI` if the meaning changed. And cargo cannot drop the `lib` prefix
from a `cdylib`, so the job renames the artifact to the prefix-free name the platform expects — the
template's README says the same. The `c header is C` job compiles `plugin_c.h` as strict C99 and C11 under
gcc and clang, which is the only place that would notice it drifting into C++.

## Targets and layout

Everything is header-only, split into two INTERFACE targets: `atp_platform` exposes `include/` — the **module
author SDK**; `atp_runtime` adds `src/runtime/include/` on top — **host machinery** (`group`, `pipeline`,
`pipeline_runner`, `module_loader`, `thread_name`). Plugins link only `atp_platform`; tests and hosts link
`atp_runtime`. Canonical include style is `<atp/...>` everywhere, from both `include/` and `src/`. Headers are
picked up by `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` for IDE visibility — no need to touch `CMakeLists.txt`
when adding one, but new io-layer headers go into the umbrella `include/atp/io.hpp` (host headers and
`version.hpp` are not part of it).

Executables: `atp_demo` (pipeline demo), `atp_host_static`/`atp_host_dynamic` + `atp_demo_plugin` (plugin
demo), `atp_c_demo_plugin` (the same idea in **pure C** over `plugin_c.h` — the only place in the tree that
checks that path really needs no C++), `atp_app` (JSON-config-driven host). `atp_app` also takes `--metrics` (per-module timing, printed as a table on shutdown), `--run-for <ms>` (a bounded run, which is what makes a measurement repeatable and scriptable) and `--control <port>` (an MCP control channel on `127.0.0.1`, off unless asked for, `0` = pick a free port and print it; unauthenticated by decision, and `stop` over it ends the process). `atp_app` is deliberately a **thin `main.cpp`** over `atp_runtime`
(`src/app`, own CMakeLists) — the config machinery itself (`config_loader`, `config_model`,
`config_validator`, `pipeline_builder`) lives in `atp_runtime` under `<atp/runtime/...>`, which is how
`atp_tests` covers it without linking anything app-specific. Sample configs in `src/app/config/` are copied
into `config/` next to the binary and both demo plugins into `plugins/` beside it, so `atp_app config/demo.json`
runs straight from the build directory (`config/c_demo.json` is the same for the C path: a C module wired
between two C++ ones). **A config addresses a plugin as `../plugins/<name>`** — plugin paths resolve against the
config's own directory — and that layout is identical in the build tree and in an installation, the directory
name coming from `ATP_PLUGIN_DIRNAME` in the root `CMakeLists.txt`. `atp_studio_lib` is the headless studio core (`src/studio`, included as `<atp/studio/...>`), also
linked into `atp_tests`. The GUI is `atp_studio`: Qt 6 Widgets, panels as private hpp/cpp pairs in
`src/studio/ui/` (namespace `atp::studio::ui`, no Q_OBJECT/moc), custom QGraphicsScene canvas.

`atp_mcp` is a headless MCP server over stdio on top of the studio core — details in `src/mcp/CLAUDE.md`.
Install, packaging and CPack rules are in `cmake/CLAUDE.md`.

## Conventions

Full style spec: `docs/code_style.md`. Essentials:

- All code comments are written in English, live in headers only, and are Doxygen `///` blocks attached to a declaration (brief first sentence, then `@param`/`@return`/`@throws` as needed). **`.cpp` files carry no comments at all** — test files included; a `}  // namespace x` closer is layout and a `// NOLINT(...)` marker is a directive to a tool, neither of them a comment. A header has no floating `//` block over a namespace or a group of functions either: the explanation belongs to the declaration it is about. Comments explain design rationale ("why"), not mechanics; a "why" with no declaration to sit on goes into `docs/architecture.md` — commit messages are one line and hold no prose.
- clang-format: Chromium base, 4-space indent, 120-column limit, mandatory braces on `if`/loops (`.clang-format`).
- Naming (STL-style, unified across the codebase):
  - Files: snake_case, one class per header, file name = class name (`queued_input.hpp` → `queued_input`).
  - Types, functions, variables: snake_case; data members get a trailing underscore (`value_`).
  - Type-erased/abstract bases: `_base` suffix (`io_base`, `input_base`, `module_base`).
  - Tag types: `name_t` plus a lowercase `inline constexpr` instance (`safe`/`unsafe`).
  - Template parameters: PascalCase with `T` prefix (`TBase`, `TItem`, `TInputs`).
  - Method prefixes: `try_` for non-throwing variants (`try_pop`), `do_` for private virtuals behind an NVI wrapper (`do_connect`).
  - gtest suite/test names stay PascalCase — that is googletest's own convention.

## Git

- **A commit message is one line.** No body, no trailer, no bullet list — the subject says what the change does and nothing else. Anything that needs a paragraph is documentation and belongs in `docs/` (`architecture.md` for design rationale, `code_style.md` for conventions), where it is read and kept up to date; a commit body is neither.
- **Every change goes on a branch.** `master` is never committed to directly: branch first, work there, merge when it is done and green.

## Architecture

Full architecture digest with rationale: `docs/architecture.md`. Layer map:

**IO layer** (`include/atp/io/`): `io_base` → `input_base`/`output_base` → `input<T>` → `queued_input<T>`; `output<T>`. Thread safety is a **runtime property of the instance**, chosen at construction with the `safe`/`unsafe` tag. Delivery avoids `dynamic_cast`: the input answers `accepts(type_index)` (checked once, at connect) and receives via `deliver(const void*, erased_type)`. Reading is **pull-only** — `get()` for state, `take()` for events, both returning a **copy into the caller's frame**. An output keeps **no cache**: it delivers and counts writes (`write_count()`), nothing else, and there is no `peek()` and no replay connect. The write path materialises nothing when the type matches — the consistent snapshot is the caller's own object — spills to the heap above `io::heap_copy_threshold` (4 KiB) when a conversion needs a `T`, and hands an owned rvalue to the **last** subscriber through `deliver_move` (copying default, so an input kind unaware of it stays correct). Hence a value of any size is a legal port type. No user code ever runs on the writer's thread, so the callback ergonomics live in `io::watcher` instead: rules are registered in `initialize`, and `poll()` runs them from `iterate` on the module's own thread, returning the `work_status` to hand back to the runner. The one thing `deliver()` does fire on the writer's thread is the optional `notifier_base` the executor attaches to wake a sleeping consumer — it carries no value and is contractually forbidden from throwing or running user code, so pull-only reading is preserved. Gotchas:

- `erased_of<T>()` statics live **per-DLL** — use their contents only, never compare addresses.
- `input<std::any>` accepts anything; the reverse (`output<std::any>` → typed input) is deliberately unsupported.
- Registries are **not thread-safe** — setup phase only.
- Outputs hold **raw pointers** to connected inputs — `disconnect()` before destroying an input.
- `input<T>::store` is a **pair** (`T&&` and `const T&`): an input kind overriding one half must override the other, or values the writer does not own land in the base's storage silently.

**Properties** (`include/atp/io/`, the third kind of declared entity alongside inputs and outputs; replaced the old `params`/`module_config`): typed setting values with a default, edited live and read pull-only, mirroring input reading (`get()` state, `take()` event «changed since last take»). Every write raises the changed flag — there is deliberately no comparison with the old value. **An enumeration is not a separate kind of property** — it is a non-empty `options()`, declarable either as a type-level name table (`enum_names<E>`) or as an instance-level set at the declaration, `make<property<int>>("channels", 2, allowed(1, 2, 6))`, which **replaces** the type table (that is how a module narrows an enum to the subset it supports). The invariant is that the value is always inside the set — default, typed write and `from_string` all pass one check against the canonical string, so `to_string()` never throws and the whole string layer (config, `-p`, studio) is safe. `property_kind` is **only** the JSON type of the value and is independent of the set: a numeric enumeration still reaches the config as a number.

**Module layer** (`include/atp/`): `atp::module<TPorts, Name, Version>` implements `module_base`; name and version are NTTPs, readable at compile time and at runtime. Concrete modules subclass `atp::io::inputs`/`outputs`/`properties` and declare ports as reference members bound by `make<>()`, gathered by the `io::ports<TIn, TOut, TProps>` node. Lifecycle: **only `initialize(module_context&)` receives the context** — `start()` and `stop()` take no arguments, so a module needing services later stores the reference from `initialize`. `iterate(std::stop_token)` returns **`work_status`** (busy/idle — drives the runner's pacing) and gets no context either, being the hot path. Contracts:

- **`stop` must be correct after `initialize` without `start`** — fail-fast rollbacks call it.
- `service_directory` is the only service: publish typed interfaces in `initialize`, look peers up in `start` — that split makes module init order irrelevant; remove publications in `stop`. Type safety without `dynamic_cast`: `void*` + `type_index` equality guard.
- Per-instance settings are **properties**, not creation arguments — factories bind constructor config at registration, so all instances of one factory are identical and different configs are separate registrations.
- A plugin-created module pins its DLL against unload via the `shared_ptr` in `module_deleter` (it may outlive the loader).
- Plugin contract (`plugin.hpp`): C symbols `atp_abi_version()` and `atp_register_modules(atp::module_registrar&)`; `plugin_abi` is currently **10** — bump it on any ABI-incompatible change to what a plugin sees (`module_base` virtuals, io types, factories). Host and plugins must share one toolchain and C++ runtime. `atp_build_id()` is an optional third symbol carrying the toolchain and standard-library identity, and the loader refuses a mismatch — it catches what the ABI number cannot (a Debug host with a Release plugin differs in `_ITERATOR_DEBUG_LEVEL`, i.e. container layout, i.e. memory corruption rather than a failed load). Its absence is tolerated silently, so adding it was not a bump; `ATP_PLUGIN_HANDSHAKE()` emits both.
- **Foreign-language plugins** (`include/atp/plugin_c.h`, host adapter in `src/runtime/include/atp/c_module.hpp`): a second registration path, purely additive, `ATP_C_ABI` versioned separately from `plugin_abi` and expected to stay at **1** because it grows through `struct_size` fields instead. Three pure C symbols (`atp_c_abi_version`/`atp_module_count`/`atp_module_desc_at`, pulled not pushed), POD descriptors declaring ports and properties, and function pointers for the lifecycle; the host builds real `input<T>`/`output<T>`/`property<T>` from an `atp_kind`, so a foreign module connects to a C++ one with no adapter in the config. **Every C++ template, allocation and exception stays host-side** — that is what lets the plugin be a Rust `cdylib` and why `atp_build_id` is not checked there. Constraints worth knowing before touching it: the payload type set is closed (`blob` = `io::blob` is the escape hatch and must stay a real C++ type), ports are declared statically because the builder connects before it initializes, no allocation crosses the boundary in either direction, the boundary is exception-free both ways (`set_error` + a return code becomes a C++ exception host-side, and a host-side failure inside a callback is stored and rethrown after `iterate` so the plugin cannot swallow it), and the adapter stores `module_host*` rather than `module_context*` because `group::initialize` builds each child's context on its own stack. Rationale in full: `docs/architecture.md`, section "C-путь".

**Execution platform** (`src/runtime/include/atp/`, target `atp_runtime`): `group : module_base` is an owning **composite** whose lifecycle cascades recursively in insertion order (`initialize` — local fail-fast with reverse-order stop of the initialized; `stop` — reverse order, continues on error, rethrows the first; `iterate` — busy-wins aggregation). Group ports are **aliases** to child ports (path form `"child.port"` only). A group is not a unit of execution and not thread-safe. `pipeline` is the aggregate root; `pipeline_runner` owns the named threads. Contracts:

- `start()` **validates** that no cross-thread connection lands in an `unsafe` input — the thread boundary is the criterion, and the error names the threads.
- Errors: first one wins and stops the whole pipeline; `wait()` blocks until the first error, shuts down and rethrows (also after a prior `stop()`; stored until the next `start()`). `stop()` is idempotent and never throws.
- All runner control is **owner-thread-only** — the stop/wait race is excluded by contract, not by synchronization.

**Config and hosts, property paths**: config schema is **3.0** (`runtime::config_schema_version`) — a group's children live under `"modules"`, and a module node carries `"properties": {"name": scalar}`. The pre-2.0 `children` and `params` keys and the pre-3.0 `replay` flag of a connection are now **rejected as unknown** (hence the major bumps); nesting under `properties` is a validator error. The model stores values as JSON nodes, not strings, so `encode` keeps `5` distinct from `"5"`. `runtime/property_override.hpp` implements the edit-by-path vocabulary — `parse_property_override` splits "path.prop=value" on the **first** `=`, then the **last** `.` to the left — used by `atp_app -p path.prop=value` (repeatable, applied before `runner.start`, so modules see the values already in `initialize`) and by studio's `session::set_property`. In the studio GUI only the project **structure** is read-only while running: property rows stay editable and saving is allowed, with `sync_persistent_properties` pulling live persistent values into the project on the fly and dropping those equal to the default. Naming trap: the edited object is `atp::studio::project` (`studio/project.hpp`, renamed from `document`; locals are `proj`), but the **MCP wire vocabulary stays "document"** — tools `new_document`/`open_document`/`save_document`/`get_document`, the `"document"` result key, the `atp://document` resource and `mcp/document_tools.hpp` all keep their names, while `workspace` exposes the object as `project()` with its path as `project_path()`/`project_dir()`.

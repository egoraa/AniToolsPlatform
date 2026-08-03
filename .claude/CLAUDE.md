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
cmake --build <build-dir> --target docs          # doxygen API reference
```

**Multi-config vs single-config matters.** With a multi-config generator (Visual Studio, Xcode, Ninja
Multi-Config) the configuration is chosen at **build** time with `--config`, and the test binary lands in a
per-config subdirectory; with a single-config generator (plain Ninja, Unix Makefiles) it is chosen at
**configure** time with `-DCMAKE_BUILD_TYPE` and there is no extra subdirectory. The `--config`/`-C` flags
above are no-ops in the single-config case.

Run a subset of the tests with `atp_tests --gtest_filter='Group.*'`; `--gtest_brief=1` keeps the output short.

**Quality knobs**, both off by default so an ordinary build is unaffected. `-DATP_WERROR=ON` turns warnings
into errors — CI sets it on every job, a local build should not, because a new compiler version routinely
finds new ones. `-DATP_SANITIZER=<value>` takes a `-fsanitize=` value (`thread`, `address`,
`address,undefined`) and instruments the **whole** build tree, vendored dependencies included, since mixing
instrumented and uninstrumented code costs ASan false positives and blinds TSan. MSVC implements
AddressSanitizer only and the configure step says so for the rest, so `thread` — the one that checks the io
layer's threading contracts — means clang or gcc. Its findings are what `.clang-tidy` and the compiler cannot
see; the two CI jobs are `sanitizer / thread` and `sanitizer / address,undefined`.

Static analysis is `.clang-tidy` at the repo root (naming rules from `docs/code_style.md` included), run by
the `clang-tidy` CI job over the compilation database. Its exclusion list has two halves and the file says
which is which: permanent contracts of the platform, and checks whose findings are still unfixed.

Locally the same analysis is `cmake --build <build-dir> --target tidy --parallel <n>`: one command per
translation unit, so the build system parallelises it and re-analyses only what changed. **It needs a
compilation database** (`CMAKE_EXPORT_COMPILE_COMMANDS`, which the Visual Studio generator does not
produce) and a `clang-tidy` on PATH or in `ATP_CLANG_TIDY`; without either the target is skipped with a
reason. The stamps depend on **every** header, not just the unit's own source — in a header-only library a
header reaches almost every unit, and a narrower dependency would report a clean tree that was never
analysed. The version is not pinned here, only compared against the major CI enforces and reported when it
differs: a newer analyser is useful locally as long as its verdict is not mistaken for the one that gates
the branch.

Formatting is `.clang-format`, enforced by the `clang-format` CI job. **The version is pinned** — the tool's
output moves between releases, so an unpinned one reformats files nobody touched — and the pin lives in two
places that must stay in step: `cmake/Format.cmake` and the workflow. Install exactly that version with
`pip install clang-format==22.1.8`; it works on any platform and needs no LLVM install. `cmake --build
<build-dir> --target format` reformats every tracked source in place (the target appears when a
clang-format is found, and configure reports it if its major differs from the pinned one).

**API reference**: `docs/Doxyfile` (run by hand from the repo root as `doxygen docs/Doxyfile`, or through the
optional `docs` CMake target, which appears only when `find_package(Doxygen)` succeeds — a machine without
doxygen configures unchanged). Output goes to `build/doxygen/html` (gitignored). `EXTRACT_ALL=NO`, so the run
doubles as a documentation-coverage check. Namespaces are documented in `docs/namespaces.dox` — doxygen drops
an undocumented namespace together with the free functions, enums and constants declared directly in it.

`atp_studio` needs Qt 6 Widgets: pass the kit as `-DCMAKE_PREFIX_PATH` on the first configure (the cache keeps
it afterwards). It is built when `ATP_BUILD_STUDIO` is `ON` (the default) and Qt6 is found, so a machine
without Qt configures unchanged. The option gates **the GUI only** — the headless studio core
(`atp_studio_lib`) and `atp_mcp` are built either way, since they never mention Qt and `atp_tests` covers
them; `OFF` additionally skips looking for Qt at all. The `ubuntu / clang` CI job builds with `OFF` so that
configuration keeps being exercised.

Plugin file names are **toolchain-agnostic** (`PREFIX ""` + `OUTPUT_NAME`, e.g. `atp_demo_plugin.dll` /
`atp_demo_plugin.so` from the same target name), and plugin paths in configs may omit the extension —
`module_loader` appends the platform one (`atp::plugin_extension`: `.dll`/`.so`/`.dylib`). Declaring a
plugin is `atp_add_plugin(<name> SOURCES ...)` from `cmake/AniToolsPlatformPluginHelpers.cmake`, which
sets that naming plus hidden visibility, the `MODULE` type and a link to `atp::platform` alone. The
file is installed verbatim into the package and included both by the package config and by the root
`CMakeLists.txt`, so the in-tree plugins are declared by the very function an out-of-tree author calls
— **do not hand-roll a plugin target**, and when changing the helper remember both callers.

`templates/plugin/` is a plugin project **outside** this build — it is not `add_subdirectory`'d and
reaches the SDK only through `find_package`. It doubles as the fixture of the `out-of-tree plugin` CI
job, the one place where both ends of a connection come from different libraries. It names the ABI it
targets (`atp_require_plugin_abi(8)`), so **bumping `plugin_abi` means editing that file too** or the
job stops configuring — which is the intended feedback, not breakage.

## Targets and layout

Everything is header-only, split into two INTERFACE targets: `atp_platform` exposes `include/` — the **module
author SDK**; `atp_runtime` adds `src/runtime/include/` on top — **host machinery** (`group`, `pipeline`,
`pipeline_runner`, `module_loader`, `thread_name`). Plugins link only `atp_platform`; tests and hosts link
`atp_runtime`. Canonical include style is `<atp/...>` everywhere, from both `include/` and `src/`. Headers are
picked up by `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` for IDE visibility — no need to touch `CMakeLists.txt`
when adding one, but new io-layer headers go into the umbrella `include/atp/io.hpp` (host headers and
`version.hpp` are not part of it).

Executables: `atp_demo` (pipeline demo), `atp_host_static`/`atp_host_dynamic` + `atp_demo_plugin` (plugin
demo), `atp_app` (JSON-config-driven host). `atp_app` also takes `--metrics` (per-module timing, printed as a table on shutdown), `--run-for <ms>` (a bounded run, which is what makes a measurement repeatable and scriptable) and `--control <port>` (an MCP control channel on `127.0.0.1`, off unless asked for, `0` = pick a free port and print it; unauthenticated by decision, and `stop` over it ends the process). `atp_app` is deliberately a **thin `main.cpp`** over `atp_runtime`
(`src/app`, own CMakeLists) — the config machinery itself (`config_loader`, `config_model`,
`config_validator`, `pipeline_builder`) lives in `atp_runtime` under `<atp/runtime/...>`, which is how
`atp_tests` covers it without linking anything app-specific. Sample configs in `src/app/config/` are copied
next to the binary together with the demo plugin, so `atp_app config/demo.json` runs straight from the build
directory. `atp_studio_lib` is the headless studio core (`src/studio`, included as `<atp/studio/...>`), also
linked into `atp_tests`. The GUI is `atp_studio`: Qt 6 Widgets, panels as private hpp/cpp pairs in
`src/studio/ui/` (namespace `atp::studio::ui`, no Q_OBJECT/moc), custom QGraphicsScene canvas.

**Installing the SDK**: `cmake --install <build-dir> --prefix <p>` gives a package that
`find_package(AniToolsPlatform)` consumes out of tree, exporting exactly one target, `atp::platform`
(also an alias in-tree, so both spellings name the same thing). Rules live in `cmake/Install.cmake`
plus the template `cmake/AniToolsPlatformConfig.cmake.in`; `ATP_INSTALL` defaults to
`PROJECT_IS_TOP_LEVEL`. The package carries `AniToolsPlatform_PLUGIN_ABI` and
`atp_require_plugin_abi(<n>)`, which turns an ABI mismatch into a **configure** error instead of a
runtime handshake failure; the number is parsed out of `include/atp/plugin.hpp` at configure time, so
the header stays the single source of truth. `atp_runtime` is deliberately **not** exported — a plugin
must not link the host runtime, and it would drag the FetchContent'd nlohmann/json along; the reasons
are written out in `cmake/Install.cmake` and `docs/architecture.md`.

**Packaging**: the same `ATP_INSTALL` switch also installs `atp_studio`/`atp_app`/`atp_mcp` into
`bin/` and the demo plugin plus the sample configs into `bin/config/`, with the Qt runtime placed in
the install tree by `qt_generate_deploy_app_script` (the build-tree `atp_deploy_qt()` knows nothing
about it). `cmake/Packaging.cmake` configures CPack — ZIP on Windows, DMG on macOS, TGZ on Linux — and
is included **last**, because `include(CPack)` freezes the rules and variables as they stand. Releases
are a separate workflow, `.github/workflows/release.yml`, on `v*` tags and on `workflow_dispatch`.

Third-party sources are vendored into `external/` by FetchContent rather than a system package: nlohmann/json
(`src/runtime/CMakeLists.txt`, reaches everything through `atp_runtime`) and googletest
(`tests/CMakeLists.txt`).

`atp_mcp` is a headless **MCP server over stdio** on top of the studio core (`src/mcp`, logic in the
header-only `atp_mcp_lib`, included as `<atp/mcp/...>`, also linked into `atp_tests`): it exposes the module
catalog, document editing and pipeline execution as MCP tools, so an agent can build a pipeline, run it and
read the values on the connections. Protocol revision `2025-11-25`, newline-delimited JSON — **stdout carries
the protocol and nothing else**, diagnostics go to stderr. Flags (parsed by `atp::mcp::parse_options` in
`options.hpp`, so the command line is testable without a process): `--root <dir>` (workspace root every config
path is confined to, default CWD), repeatable `--plugin-dir <dir>` (extra directories plugins may be loaded
from — loading a plugin runs foreign code, so that policy is separate from the config one) and repeatable
`--scan-dir <dir>` (directories scanned for plugins at startup, so the catalog is populated before the first
`list_modules`; a scan dir is trusted, hence also a plugin dir). Directory arguments are resolved against the
process CWD, not the root — they come from the operator, not the client. Unlike the Qt studio, the server keeps
no settings file, so search dirs added at runtime via `add_plugin_search_dir` do not survive a restart;
`--scan-dir` is the persistent form. `server::handle` is a pure `json → json` function, which is how the whole
protocol is tested without spawning a process.

## Conventions

Full style spec: `docs/code_style.md`. Essentials:

- All code comments are written in English, live in headers only, and are Doxygen `///` blocks attached to a declaration (brief first sentence, then `@param`/`@return`/`@throws` as needed). **`.cpp` files carry no comments at all** — test files included; a `}  // namespace x` closer is layout and a `// NOLINT(...)` marker is a directive to a tool, neither of them a comment. A header has no floating `//` block over a namespace or a group of functions either: the explanation belongs to the declaration it is about. Comments explain design rationale ("why"), not mechanics; a "why" with no declaration to sit on goes into `docs/architecture.md` — commit messages are one line and hold no prose.
- clang-format: Chromium base, 4-space indent, 120-column limit, mandatory braces on `if`/loops (`.clang-format`).
- Naming (STL-style, unified across the codebase):
  - Files: snake_case, one class per header, file name = class name (`queued_input.hpp` → `queued_input`).
  - Types, functions, variables: snake_case; data members get a trailing underscore (`value_`).
  - Type-erased/abstract bases: `_base` suffix (`io_base`, `input_base`, `module_base`).
  - Tag types: `name_t` plus a lowercase `inline constexpr` instance (`replay_t`/`replay`, `safe`/`unsafe`).
  - Template parameters: PascalCase with `T` prefix (`TBase`, `TItem`, `TInputs`).
  - Method prefixes: `try_` for non-throwing variants (`try_pop`), `do_` for private virtuals behind an NVI wrapper (`do_connect`).
  - gtest suite/test names stay PascalCase — that is googletest's own convention.

## Git

- **A commit message is one line.** No body, no trailer, no bullet list — the subject says what the change does and nothing else. Anything that needs a paragraph is documentation and belongs in `docs/` (`architecture.md` for design rationale, `code_style.md` for conventions), where it is read and kept up to date; a commit body is neither.
- **Every change goes on a branch.** `master` is never committed to directly: branch first, work there, merge when it is done and green.

## Architecture

Full architecture digest with rationale: `docs/architecture.md`. Layer map:

**IO layer** (`include/atp/io/`): `io_base` → `input_base`/`output_base` → `input<T>` → `queued_input<T>`; `output<T>`. Thread safety is a **runtime property of the instance**, chosen at construction with the `safe`/`unsafe` tag. Delivery avoids `dynamic_cast`: the input answers `accepts(type_index)` (checked once, at connect) and receives via `deliver(const void*, erased_type)`. Reading is **pull-only** — `get()` for state, `take()` for events. No user code ever runs on the writer's thread, so the callback ergonomics live in `io::watcher` instead: rules are registered in `initialize`, and `poll()` runs them from `iterate` on the module's own thread, returning the `work_status` to hand back to the runner. The one thing `deliver()` does fire on the writer's thread is the optional `notifier_base` the executor attaches to wake a sleeping consumer — it carries no value and is contractually forbidden from throwing or running user code, so pull-only reading is preserved. Gotchas:

- `erased_of<T>()` statics live **per-DLL** — use their contents only, never compare addresses.
- `input<std::any>` accepts anything; the reverse (`output<std::any>` → typed input) is deliberately unsupported.
- Registries are **not thread-safe** — setup phase only.
- Outputs hold **raw pointers** to connected inputs — `disconnect()` before destroying an input.

**Properties** (`include/atp/io/`, the third kind of declared entity alongside inputs and outputs; replaced the old `params`/`module_config`): typed setting values with a default, edited live and read pull-only, mirroring input reading (`get()` state, `take()` event «changed since last take»). Every write raises the changed flag — there is deliberately no comparison with the old value. **An enumeration is not a separate kind of property** — it is a non-empty `options()`, declarable either as a type-level name table (`enum_names<E>`) or as an instance-level set at the declaration, `make<property<int>>("channels", 2, allowed(1, 2, 6))`, which **replaces** the type table (that is how a module narrows an enum to the subset it supports). The invariant is that the value is always inside the set — default, typed write and `from_string` all pass one check against the canonical string, so `to_string()` never throws and the whole string layer (config, `-p`, studio) is safe. `property_kind` is **only** the JSON type of the value and is independent of the set: a numeric enumeration still reaches the config as a number.

**Module layer** (`include/atp/`): `atp::module<TPorts, Name, Version>` implements `module_base`; name and version are NTTPs, readable at compile time and at runtime. Concrete modules subclass `atp::io::inputs`/`outputs`/`properties` and declare ports as reference members bound by `make<>()`, gathered by the `io::ports<TIn, TOut, TProps>` node. Lifecycle: **only `initialize(module_context&)` receives the context** — `start()` and `stop()` take no arguments, so a module needing services later stores the reference from `initialize`. `iterate(std::stop_token)` returns **`work_status`** (busy/idle — drives the runner's pacing) and gets no context either, being the hot path. Contracts:

- **`stop` must be correct after `initialize` without `start`** — fail-fast rollbacks call it.
- `service_directory` is the only service: publish typed interfaces in `initialize`, look peers up in `start` — that split makes module init order irrelevant; remove publications in `stop`. Type safety without `dynamic_cast`: `void*` + `type_index` equality guard.
- Per-instance settings are **properties**, not creation arguments — factories bind constructor config at registration, so all instances of one factory are identical and different configs are separate registrations.
- A plugin-created module pins its DLL against unload via the `shared_ptr` in `module_deleter` (it may outlive the loader).
- Plugin contract (`plugin.hpp`): C symbols `atp_abi_version()` and `atp_register_modules(atp::module_registrar&)`; `plugin_abi` is currently **8** — bump it on any ABI-incompatible change to what a plugin sees (`module_base` virtuals, io types, factories). Host and plugins must share one toolchain and C++ runtime.

**Execution platform** (`src/runtime/include/atp/`, target `atp_runtime`): `group : module_base` is an owning **composite** whose lifecycle cascades recursively in insertion order (`initialize` — local fail-fast with reverse-order stop of the initialized; `stop` — reverse order, continues on error, rethrows the first; `iterate` — busy-wins aggregation). Group ports are **aliases** to child ports (path form `"child.port"` only). A group is not a unit of execution and not thread-safe. `pipeline` is the aggregate root; `pipeline_runner` owns the named threads. Contracts:

- `start()` **validates** that no cross-thread connection lands in an `unsafe` input — the thread boundary is the criterion, and the error names the threads.
- Errors: first one wins and stops the whole pipeline; `wait()` blocks until the first error, shuts down and rethrows (also after a prior `stop()`; stored until the next `start()`). `stop()` is idempotent and never throws.
- All runner control is **owner-thread-only** — the stop/wait race is excluded by contract, not by synchronization.

**Config and hosts, property paths**: config schema is **2.0** (`runtime::config_schema_version`) — a group's children live under `"modules"`, and a module node carries `"properties": {"name": scalar}`. The pre-2.0 `children` and `params` keys are now **rejected as unknown** (hence the major bump); nesting under `properties` is a validator error. The model stores values as JSON nodes, not strings, so `encode` keeps `5` distinct from `"5"`. `runtime/property_override.hpp` implements the edit-by-path vocabulary — `parse_property_override` splits "path.prop=value" on the **first** `=`, then the **last** `.` to the left — used by `atp_app -p path.prop=value` (repeatable, applied before `runner.start`, so modules see the values already in `initialize`) and by studio's `session::set_property`. In the studio GUI only the project **structure** is read-only while running: property rows stay editable and saving is allowed, with `sync_persistent_properties` pulling live persistent values into the project on the fly and dropping those equal to the default. Naming trap: the edited object is `atp::studio::project` (`studio/project.hpp`, renamed from `document`; locals are `proj`), but the **MCP wire vocabulary stays "document"** — tools `new_document`/`open_document`/`save_document`/`get_document`, the `"document"` result key, the `atp://document` resource and `mcp/document_tools.hpp` all keep their names, while `workspace` exposes the object as `project()` with its path as `project_path()`/`project_dir()`.

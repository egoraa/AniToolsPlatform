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
`module_loader` appends the platform one (`atp::runtime::plugin_extension`: `.dll`/`.so`/`.dylib`). Declaring a
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

The two script bridges are the second and third consumers of the C path, each with its own build
switch, its own author-facing package and its own traps: `src/bridges/python/CLAUDE.md` and
`src/bridges/lua/CLAUDE.md`. The out-of-tree template projects that double as CI fixtures — and that
must be edited whenever the ABI they name moves — are in `templates/CLAUDE.md`.

## Targets and layout

Almost everything is header-only, split into two INTERFACE targets: `atp_platform` exposes `include/` — the
**module author SDK**; `atp_runtime` adds `src/runtime/include/` on top — **host machinery**, all of it under
`<atp/runtime/...>` in `namespace atp::runtime` behind the umbrella `<atp/runtime.hpp>` (`group`,
`pipeline`, `pipeline_runner`, `module_loader`, `c_module`, `host_node`, `log_ring`, `log_pump`,
`thread_name`, `console_encoding`, `json_codec` and the config subsystem). **Two** runtime headers are
deliberately outside the umbrella, for the same class of reason: `runtime/socket_platform.hpp` exists to
include `<winsock2.h>`, and `runtime/config_value_json.hpp` names `nlohmann::json` in its signatures — a
header reaching every runtime consumer must drag in neither the socket stack nor a document library. The
three files that open a socket, and the MCP and studio-client files that speak the protocol, include the
one they need by name.

**`atp_json` is the one compiled target of the runtime** (`src/runtime/json_codec.cpp`), and it exists so
that the umbrella costs no document library: `json_parse`/`try_json_parse`/`json_dump` are declared over
`atp::config::node` in `runtime/json_codec.hpp`, which names nothing, and `nlohmann_json` is **PRIVATE** to
`atp_json`. Replacing the library means rewriting that one `.cpp`. `atp_runtime` links `atp_json` and no
longer propagates the library at all, so `atp_mcp_lib` and `atp_studio_lib` — which genuinely speak the
protocol — link it themselves. It is also the template for any further move of runtime bodies out of the
headers. Plugins link only `atp_platform`; tests and hosts link `atp_runtime`. Canonical include style is `<atp/...>` everywhere, from both `include/` and `src/`. Headers are
picked up by `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` for IDE visibility — no need to touch `CMakeLists.txt`
when adding one.

`include/atp/` is laid out as **subsystems: a folder plus an umbrella header** — `io/` (`io.hpp`), `module/`
(`module.hpp`), `hosting/` (`hosting.hpp`), `config/` (`config.hpp`), `plugin/` (`plugin.hpp`). Only those five
umbrellas and `plugin_c.h` sit at the root. **The folder is the box, the umbrella is the audience**, and a new
header is declared in the umbrella whose audience needs it: `module.hpp` is what a **module author** writes
against (the `module<>` class, the base, context, host, services, module config, `log_level`, the io layer and
the config vocabulary), `hosting.hpp` adds what only a **host or loader** needs on top (factories, registry,
registrar, `null_host`). That split is the rule — deciding where a header goes means deciding whether a module
author has any use for it. `support/` (`version`, `fixed_string`, `type_compare`) deliberately has **no**
umbrella: it is utilities with no common audience, included by name. The host runtime follows the same
shape one level out — `src/runtime/include/atp/runtime/` plus `<atp/runtime.hpp>` — and a new runtime
header is declared in that umbrella unless it drags a platform system header along.

A per-header CMake rule in `tests/CMakeLists.txt` generates one translation unit per `include/atp/**`
and `src/runtime/include/atp/**` header, each including exactly that header, so `atp_tests` fails to
build if any SDK or runtime header stops being self-contained. The studio and MCP headers are not in
the globs.

Executables: `atp_demo` (pipeline demo), `atp_host_static`/`atp_host_dynamic` + `atp_demo_plugin` (plugin
demo), `atp_c_demo_plugin` (the same idea in **pure C** over `plugin_c.h` — the only place in the tree that
checks that path really needs no C++), `atp_app` (JSON-config-driven host). `atp_app` also takes `--metrics` (per-module timing, printed as a table on shutdown), `--run-for <ms>` (a bounded run, which is what makes a measurement repeatable and scriptable) and `--control <port>` (an MCP control channel on `127.0.0.1`, off unless asked for, `0` = pick a free port and print it; unauthenticated by decision, and `stop` over it ends the process). `atp_app` is deliberately a **thin `main.cpp`** over `atp_runtime`
(`src/app`, own CMakeLists) — the config machinery itself (`config_loader`, `config_model`,
`config_validator`, `pipeline_builder`) lives in `atp_runtime` under `<atp/runtime/...>`, which is how
`atp_tests` covers it without linking anything app-specific. Sample configs in `src/app/config/` are copied
into `config/` next to the binary, so `atp_app config/demo.json`
runs straight from the build directory (`config/c_demo.json` is the same for the C path: a C module wired
between two C++ ones). **A config addresses a plugin as `../plugins/<name>`** — plugin paths resolve against the
config's own directory — and that layout is identical in the build tree and in an installation, the directory
name coming from `ATP_PLUGIN_DIRNAME` in the root `CMakeLists.txt`.

**The build tree has one address per kind of artifact**, set once in the root `CMakeLists.txt` before the
first `add_subdirectory` so the vendored dependencies inherit it too: `<build>/bin` for executables and the
Qt runtime beside them, `<build>/bin/plugins` for everything loadable, `<build>/lib` for static and import
libraries. **There is no compile PDB to place anywhere**, because MSVC debug information is embedded in the
objects — `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` is `Embedded` (`/Z7`), and that is a correctness setting
rather than a preference: CMake spells a compile-PDB name into `/Fd` **only for a static library**, so every
other target falls back to cl's default `vc140.pdb`, one shared type server for the whole tree whose age
advances on every write, and an object that no longer matches it is linked under `LNK4099` "as if no debug
information" — leaving a binary whose PDB has the public symbols but no types, no locals and no line tables,
so a line breakpoint binds **nowhere**, in any target. A static library's debug information now rides inside
the `.lib`, which is what that variable was reaching for. A plugin is the one target that does
**not** follow `CMAKE_LIBRARY_OUTPUT_DIRECTORY`: `atp_add_plugin` sends it to `ATP_PLUGIN_OUTPUT_DIRECTORY`,
which the root defines and an out-of-tree build does not, so the installed helper behaves as it always did.
That variable carries `$<CONFIG>` under a multi-config generator and must not under a single-config one —
CMake appends a per-configuration subdirectory only to a value with no generator expression in it, and the
plain spelling would put the plugins in `bin/plugins/Debug` while the hosts sat in `bin/Debug`. Two
consequences: the post-build steps that used to copy plugins and bridge packages next to `atp_app` and
`atp_studio` are **gone** — they would now copy a file onto itself — and `atp_deploy_qt` deploys **once per
output directory**, the first caller owning the deployment and later ones depending on it, because
`atp_studio` and `atp_ui_tests` share `bin/` and two concurrent `windeployqt` runs write the same DLLs. The
seven fixture plugins under `tests/test_plugin/` are the deliberate exception, kept in a directory of their
own: three of them are broken on purpose and several suites hand that directory to a scanner. `atp_studio_lib` is the headless studio core (`src/studio`, included as `<atp/studio/...>`), also
linked into `atp_tests`. The GUI is `atp_studio`: Qt 6 Widgets, panels as private hpp/cpp pairs in
`src/studio/ui/` (namespace `atp::studio::ui`, no Q_OBJECT/moc), custom QGraphicsScene canvas.
Studio can **author** script modules, not just host them, and it does so for **every language in
`studio/languages.hpp`** — that list is the one place a language is added. How a module folder is
provisioned, why a folder's own bridge is never replaced, and what `keep_one_bridge`,
`stale_loaded_bridge` and `script_environment` each guard against: `src/studio/CLAUDE.md`.

**The whole tree compiles as UTF-8**, source and execution charset alike: the root `CMakeLists.txt`
adds `/utf-8` for MSVC before the subdirectories, so the vendored dependencies get it too, and
`atp_platform` carries it as an INTERFACE option so an out-of-tree plugin is compiled the same way —
which is what makes `plugin_c.h`'s "every string crossing this boundary is UTF-8" true for someone
else's build. gcc and clang need nothing. It is a correctness switch, not tidiness: without it MSVC
reads a source through the process code page and encodes narrow literals back through it, so the bytes
of a literal depend on the machine, and the MCP tool descriptions — which contain em dashes and travel
as JSON, where nlohmann's `dump` is strict about UTF-8 and throws — would stop being serialisable on a
machine whose page does not round-trip them. Printing that UTF-8 is a second question, and `atp::runtime::console_utf8`
(`runtime/console_encoding.hpp`) answers it: both hosts raise one at the top of `main`, which puts a
Windows console into UTF-8 and **puts it back in the destructor** — the page belongs to the window and
outlives the process, so leaving it changed would reach every later program in that shell. A process
with no console, or one whose stream is redirected, is untouched, which is why `atp_mcp` may use it
without any risk to the protocol on its pipe. `tests/support/source_encoding_tests.cpp` guards the
setting with the one assertion that can: a universal character name (`"—"`) is one byte under a
code page and three under UTF-8. **A plain em dash literal guards nothing** — its UTF-8 bytes survive
a round trip through CP1252 — and a test written that way is green while the setting is wrong, which
is exactly how several non-ASCII tests here were vacuous before.

**Both bridges carry paths as UTF-8 and never as `path::string()`.** On Windows that conversion goes
through the process code page and **throws** for anything it cannot represent, and the throw escapes
`atp_module_count` — an `extern "C"` entry point `plugin_c.h` requires to be exception-free — so a
module folder named in Cyrillic used to take the whole bridge down instead of failing to list one
script. The scan variables are read wide (`_wdupenv_s`) for the same reason: a narrow read replaces
what the page cannot encode and the directory is then silently absent, with no error anywhere. The Lua
bridge additionally reads scripts itself and loads them with `luaL_loadbuffer` rather than
`luaL_loadfile`, which would `fopen` a narrow name, and preloads its `atp` package into
`package.loaded` so `require` never depends on that path either. `plugin_c.h` states the UTF-8 rule;
the regression tests build their non-ASCII names from numeric code points, because the build sets no
`/utf-8` and a literal in the source would be read as the code page and quietly test nothing.

`atp_mcp` is a headless MCP server over stdio on top of the studio core — details in `src/mcp/CLAUDE.md`.
Install, packaging and CPack rules are in `cmake/CLAUDE.md`.

## Conventions

Full style spec: `docs/code_style.md`. Essentials:

- All code comments are written in English, live in headers only, and are Doxygen `///` blocks attached to a declaration (brief first sentence, then `@param`/`@return`/`@throws` as needed). **`.cpp` files carry no comments at all** — test files included; a `}  // namespace x` closer is layout and a `// NOLINT(...)` marker is a directive to a tool, neither of them a comment. A header has no floating `//` block over a namespace or a group of functions either: the explanation belongs to the declaration it is about. Comments explain design rationale ("why"), not mechanics; a "why" with no declaration to sit on goes into `docs/architecture.md` — commit messages are one line and hold no prose.
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
- Registries enumerate in **declaration order**, and that is a contract: `list()`/`entries()`/`owned()` hand back what the author wrote, in the order they wrote it. The store is a `std::vector` with a linear search, and the order is **paid for**, not free: measured in Release, `find()` over four ports costs 10.7 ns against a hash map's 8.3 ns, and the gap widens with size. It is the right price only because lookups happen at setup and on description requests, never in `iterate()`. It used to be a hash map, so MCP and the studio inspector sorted by name to compensate while the canvas showed hash order; **those sorts are gone, do not reintroduce one**. The four remaining `ranges::sort` calls over ports or modules (`app/main.cpp`, `studio/ui/panels/runtime_widget.cpp`) rank metrics worst-first and are unrelated.
- Outputs hold **raw pointers** to connected inputs — `disconnect()` before destroying an input.
- `input<T>::store` is a **pair** (`T&&` and `const T&`): an input kind overriding one half must override the other, or values the writer does not own land in the base's storage silently.

**Properties** (`include/atp/io/`, the third kind of declared entity alongside inputs and outputs; replaced the old `params`/`module_config`): typed setting values with a default, edited live and read pull-only, mirroring input reading (`get()` state, `take()` event «changed since last take»). Every write raises the changed flag — there is deliberately no comparison with the old value. **An enumeration is not a separate kind of property** — it is a non-empty `options()`, declarable either as a type-level name table (`enum_names<E>`) or as an instance-level set at the declaration, `make<property<int>>("channels", 2, allowed(1, 2, 6))`, which **replaces** the type table (that is how a module narrows an enum to the subset it supports). The invariant is that the value is always inside the set — default, typed write and `from_string` all pass one check against the canonical string, so `to_string()` never throws and the whole string layer (config, `-p`, studio) is safe. `property_kind` is **only** the JSON type of the value and is independent of the set: a numeric enumeration still reaches the config as a number.

**Module layer** (`include/atp/`): `atp::module<TPorts, Name, Version>` implements `module_base`; name and version are NTTPs, readable at compile time and at runtime. Concrete modules subclass `atp::io::inputs`/`outputs`/`properties` and declare ports as reference members bound by `make<>()`; gathered by the `atp::ports<TIn, TOut, TProps>` node, which lives in the module layer (`module/ports.hpp`, concept `ports_list`) rather than in `io/`. `module<>` reaches its sections through the covariant `inputs()`/`outputs()`/`properties()` overrides — `inputs().count`, with the parentheses — and `module_base` declares those six as pure virtuals. `make<>` is short: `make<int>("count")` is an `input<int>`/`output<int>`, `make("gain", 0.5)` a `property<double>`; spelling the port or property type out still works and is what `c_module` does when the type comes from a runtime `atp_kind`. Lifecycle: **only `initialize(module_context&)` receives the context** — `start()` and `stop()` take no arguments, so a module needing services later stores the reference from `initialize`. `iterate(std::stop_token)` returns **`work_status`** (busy/idle — drives the runner's pacing) and gets no context either, being the hot path. Contracts:

- **`stop` must be correct after `initialize` without `start`** — fail-fast rollbacks call it.
- `service_directory` is the only service: publish typed interfaces in `initialize`, look peers up in `start` — that split makes module init order irrelevant; remove publications in `stop`. Type safety without `dynamic_cast`: `void*` + `type_index` equality guard.
- Per-instance settings are **properties**, not creation arguments — factories bind constructor config at registration, so all instances of one factory are identical and different configs are separate registrations.
- A plugin-created module pins its DLL against unload via the `shared_ptr` in `module_deleter` (it may outlive the loader).
- Plugin contract (`plugin.hpp`, split into `plugin/abi.hpp` + `plugin/build_id.hpp` + `plugin/handshake.hpp`; CMake reads the ABI number out of `plugin/abi.hpp`): C symbols `atp_abi_version()` and `atp_register_modules(atp::module_registrar&)`; `plugin_abi` is currently **14** — bump it on any ABI-incompatible change to what a plugin sees (`module_base` virtuals, io types, factories). The emphasis is on **ABI**: the subsystem re-layout renamed and moved plenty that a plugin names in its source (`atp::io::ports` → `atp::ports`, `atp::config_value` → `atp::config::node`, `plugin.hpp` split in three) and deliberately did not bump, because nothing already compiled stopped loading — a source break is answered by a clean break with no shims, not by a number whose job is to refuse a stale binary. Host and plugins must share one toolchain and C++ runtime. `atp_build_id()` is an optional third symbol carrying the toolchain and standard-library identity, and the loader refuses a mismatch — it catches what the ABI number cannot (a Debug host with a Release plugin differs in `_ITERATOR_DEBUG_LEVEL`, i.e. container layout, i.e. memory corruption rather than a failed load). Its absence is tolerated silently, so adding it was not a bump; `ATP_PLUGIN_HANDSHAKE()` emits both.
- **Describing a module costs no instance.** `module_factory_base::declaration()` is pure virtual and answers `atp::module_declaration` — the declared ports and properties — from the type: `module_factory` builds `TModule::ports_type` alone and never calls the constructor, `c_module_factory` reads the C descriptors, `detail::pinned_factory` forwards. A module written by hand from `module_base` names no `ports_type`, and only that one is still described by a probe instance, which is the one place a throwing constructor still makes a module `broken` in the palette. Two consequences worth knowing: tolerating an empty config is no longer something a module owes studio, and `studio::port_info`/`property_info` are now aliases of `atp::port_declaration`/`property_declaration` rather than their own structs.
- **Foreign-language plugins** (`include/atp/plugin_c.h`, host adapter in `src/runtime/include/atp/runtime/c_module.hpp`): a second registration path, purely additive, `ATP_C_ABI` versioned separately from `plugin_abi` and expected to stay at **1** because it grows through `struct_size` fields instead. Adding one means: append to the struct, read it only behind a size check (`detail::c_desc_source` is the pattern), update `tests/platform/plugin_c_layout_tests.cpp` **and** the hand-written Rust mirror — and never move the acceptance floor, which is the frozen `ATP_MODULE_DESC_SIZE_V1` and not the current `sizeof`. The first such field is `source`, the file a module is declared in (the Python bridge's script); it travels beside the registration in `registered_module` rather than in the factory, because a factory is a plugin ABI type, and reaches `module_info::source` in `load_plugin`, not in `describe`. Three pure C symbols (`atp_c_abi_version`/`atp_module_count`/`atp_module_desc_at`, pulled not pushed), POD descriptors declaring ports and properties, and function pointers for the lifecycle; the host builds real `input<T>`/`output<T>`/`property<T>` from an `atp_kind`, so a foreign module connects to a C++ one with no adapter in the config. **Every C++ template, allocation and exception stays host-side** — that is what lets the plugin be a Rust `cdylib` and why `atp_build_id` is not checked there. Constraints worth knowing before touching it: the payload type set is closed (`blob` = `io::blob` is the escape hatch and must stay a real C++ type), ports are declared statically because the builder connects before it initializes, no allocation crosses the boundary in either direction, the boundary is exception-free both ways (`set_error` + a return code becomes a C++ exception host-side, and a host-side failure inside a callback is stored and rethrown after `iterate` so the plugin cannot swallow it), and the adapter stores `module_host*` rather than `module_context*` because `group::initialize` builds each child's context on its own stack. Rationale in full: `docs/architecture.md`, section "C-путь".

**Execution platform** (`src/runtime/include/atp/runtime/`, `namespace atp::runtime`, target `atp_runtime`): `runtime::group : module_base` is an owning **composite** whose lifecycle cascades recursively in insertion order (`initialize` — local fail-fast with reverse-order stop of the initialized; `stop` — reverse order, continues on error, rethrows the first; `iterate` — busy-wins aggregation). Group ports are **aliases** to child ports (path form `"child.port"` only). A group is not a unit of execution and not thread-safe. `runtime::pipeline` is the aggregate root; `runtime::pipeline_runner` owns the named threads. Contracts:

- `start()` **validates** that no cross-thread connection lands in an `unsafe` input — the thread boundary is the criterion, and the error names the threads.
- Errors: first one wins and stops the whole pipeline; `wait()` blocks until the first error, shuts down and rethrows (also after a prior `stop()`; stored until the next `start()`). `stop()` is idempotent and never throws.
- All runner control is **owner-thread-only** — the stop/wait race is excluded by contract, not by synchronization.

**Config and hosts, property paths**: config schema is **3.3** (`runtime::config_schema_version`) — a group's children live under `"modules"`, and a module node carries `"properties": {"name": scalar}` and, since 3.2, `"config"`, whose `file:` source is what 3.3 added. An entry of `"plugins"` may name a **directory** (3.1, minor because the key's shape did not change): the plugins directly in it are loaded, non-recursively and sorted by file name, deduplicated by `weakly_canonical` against the rest of the list. A file there that exports neither entry point throws `atp::runtime::not_a_plugin` and is **skipped** when it came from a directory; every other failure — including a file that would not open at all — stops the host, and a file named explicitly is not forgiven anything. The pre-2.0 `children` and `params` keys and the pre-3.0 `replay` flag of a connection are now **rejected as unknown** (hence the major bumps); nesting under `properties` is a validator error. The model stores values as `config::node`s, not strings, so `encode` keeps `5` distinct from `"5"`. `runtime/property_override.hpp` implements the edit-by-path vocabulary — `parse_property_override` splits "path.prop=value" on the **first** `=`, then the **last** `.` to the left — used by `atp_app -p path.prop=value` (repeatable, applied before `runner.start`, so modules see the values already in `initialize`) and by studio's `session::set_property`. In the studio GUI only the project **structure** is read-only while running: property rows stay editable and saving is allowed, with `sync_persistent_properties` pulling live persistent values into the project on the fly and dropping those equal to the default.

**Declared config** (`include/atp/config/fields.hpp`, `plugin_abi` 13): a module may **declare** its config instead of parsing the tree by hand — an heir of `atp::config::fields` names its fields as reference members (`field` scalar, `group` nested object, `list` array), and the module names the type as `using config_type = ...`. Required means **no default** (`field<double>("gain")`); a `const char*` overload exists because a literal is not one of the four scalar forms. Four things to know before touching it. **Validation is run by `module_factory::create`, not by the object** — an unknown key is only knowable once every field is declared, which is after the heir'''s last member-initializer, where `using fields::fields;` leaves no code; so `fields` collects problems and never throws, and the factory builds one, calls `throw_if_invalid()` and only then builds the module (the config object is therefore built twice, on purpose). **A whole number widens into a real and never the reverse** — JSON writes `48000` for a real, `config::node` keeps the two apart, and without the widening a real field would silently take its default. **Every container is a `deque`**, including what `list<T>` returns: an heir binds references into the base'''s storage, so `fields` is neither copyable nor movable and `std::vector` would not even compile. **The schema is read without a module** and travels in `module_declaration::config_schema` (optional: no schema means "edit as text", an empty one means "takes no settings") and out through MCP as `"config"`. The imperative path stays legal and untouched, as do the C path and both bridges. In studio the schema does two jobs: it validates at creation, and it **materialises** — `studio::materialise` fills the stored config out with every declared field at its default, and `ui/kit/config_tree.hpp` edits that object as a tree, writing back `studio::strip_defaults` of it. Both are Qt-free functions over `atp::config::node` in `studio/config_shape.hpp`, pinned by `strip(materialise(x)) == strip(x)`. Only an **inline** config of a module that declared a schema gets the tree; a shared-block reference, a `"file:"` config and a module without a schema stay on the JSON editor, because a block belongs to the document and may be named by modules whose schemas differ.

**Module config** (`include/atp/module/module_config.hpp`, schema 3.3, `plugin_abi` 11 — the number it arrived at, not the current one): the third declared
entity beside ports and properties — a structured setting reaching the module in its **constructor**,
that is before both `connect` and `initialize`, for what a property cannot express (a list, a table, a
nested object) and what is not edited live. The type a constructor receives is `atp::module_config` —
the config **as a whole**: the root of the tree, path access, and, when it came from a file, the bytes
of that file and its name. `atp::config::node` describes **one node** and is deliberately left as that;
it stays a closed seven-form variant with **no parser and no `dump()`** (naming `nlohmann::json` in an
ABI signature would drag it into every plugin), and both live host-side — the text boundary in
`runtime/json_codec.hpp`, the conversion to and from the protocol's node in
`runtime/config_value_json.hpp`. It **may** be built and edited in place: `operator==`, an inserting
`operator[](key)`, `push_back` and `erase` were added so the pipeline document and studio's project could be
`config::node` too, and that is layout-neutral, so no ABI moved. What the type must not name is a document
library, not that it is read-only. There is no `set(key, value)` — it said exactly what `node[key] = value`
says. Walking a node is `entries()` (object) and `elements()` (array), each handing back the vector itself and
a shared empty one for any other form, so a traversal never asks the kind first; the index loops over
`key_at(i)` they replaced are gone and should not come back. **`elements()` is not called `items()` on
purpose** — `nlohmann::json::items()` means the opposite (an object's key/value pairs) and both types are
handled side by side here, `runtime/config_value_json.hpp` using each sense once.

**The config document is a `config::node` everywhere** — `load_config`, `validate`, `decode`/`encode`,
`module_node::properties`/`config`, `config::configs`, and studio's project with its undo stack. JSON is a
format at the file and protocol edges and no longer a type in the middle. Two consequences to know. An
object keeps insertion order for reproducible traversal, **not** the order written in the file, and
`json_dump` sorts keys on the way out, which is why a saved document is byte-identical to what the old
`std::map`-backed writer produced. And because `encode` writes keys in its own order while `node`'s
equality is order-sensitive, the round-trip invariant is pinned on the **dumped text**:
`json_dump(encode(decode(doc))) == json_dump(doc)`.

`module_factory_base::create` takes a `const module_config&`; `module_registry::create` keeps overloads
without one, `module_factory` deliberately does not, and its class constraint is a **disjunction**
because a module whose only constructor takes a config is constructible from no arguments at all. That
disjunction is the named concept `factory_constructible`, and **`module_registry::add`/
`module_registrar::add` require exactly it** — asking for plain `constructible_from<TModule>` there left
such a module unregistrable by the ordinary call while `module_factory` accepted it directly. A module
joins the channel only by declaring such a constructor.

Path access is `cfg.find("channels[2].rate")` / `at(...)`: segments split on `.`, an index as `[N]`
after a segment, a leading `[N]` for an array root, and a key containing `.` or `[` reachable only
through `root()`. The **two failures are different on purpose** — a malformed path throws `config::access_error`
even from `find` (a typo in the module's own source must not become "nothing there"), while something
absent is `nullptr` from `find` and a `config::access_error` naming the full path and where it stopped from `at`.
The two config exceptions are **`atp::config::access_error`** — one node, one index or one path is wrong —
and **`atp::runtime::config_error`** — the host could not read, validate or build a config. The first used to
be `atp::config::error`, which left the namespaces as the only thing keeping them apart, so a `catch` written
from memory caught the wrong one.
Parsing always runs to the end of the string so the grammar verdict never depends on the data. Typed
reads with a fallback are the free functions `config::int_or`/`bool_or`/`double_or`/`string_or` (in `<atp/config/read.hpp>`, reached through the `<atp/module.hpp>` umbrella) over a
**nullable** node, which is why they serve both levels of lookup and are not members — and they are the
**only** fallback vocabulary, the `node::*_at(key, fallback)` members that used to sit beside them having
been removed as a second word for the same thing. The throwing `node::*_at(key)` stay, for a message that
names both the key and the two forms.

In the schema a module's `"config"` is an object in place, a **string naming an entry of `"configs"`**
(parsed on the first colon; no prefix means the document's top-level `"configs"`), or a **`"file:<path>"`
string** (3.3). An entry of `"configs"` must be an object or a `file:` string and **never a bare
reference to another entry** — that one rule is the whole defence against cycles, since a chain of
references is then exactly one step long. The model stores every spelling **verbatim** so
`encode(decode(doc)) == doc` and neither a reference nor a file is ever expanded on save; resolution
happens in `pipeline_builder`, which is also where a file is read (`runtime/config_file.hpp`). **The
extension decides the format**: `.json` must parse (a broken one is an error with a position, not a
silent slide into "unknown format"), anything else is opaque text with a null root, `is_opaque()` true,
the bytes in `text()` and the path in `origin()` — and `is_opaque()` is not derivable, since a `.json`
holding literally `null` also leaves an empty tree beside a non-empty text. A relative path resolves
against **the document's directory**; without one the error says so by name rather than resolving
against the process's current directory, which is why studio passes `app_state::saved_dir()` and not
`config_dir()`. `file:` is **not** a duplicate of `$include`: that one is a textual expansion done
before validation, so saving inlines it and the reference disappears, it cannot express a format the
host does not parse, and it leaves no `origin()`.

The C path grew to **eleven callbacks appended to `atp_api` behind `struct_size`, with no `ATP_C_ABI`
bump**, and the last four (`config_find_path`, `config_text`, `config_origin`, `config_is_opaque`) are
asked for by **their own** detector `atp_api_has_config_text` — a host carrying the first seven answers
yes to `atp_api_has_config` and must answer no to this. Four traps there: config text is valid for the
module's whole lifetime (stronger than a port's, since it points into the host's tree rather than the
shared scratch); the boolean `guarded` must not be used for callbacks answering a handle — it reduces
its body to 1 or 0, which turns `ATP_CONFIG_OBJECT` into `ATP_CONFIG_BOOL` silently, hence
`guarded_value<TRet>` beside it; `config_find_path` follows a path **only from the root**, because the
flat preorder index has no reverse mapping; and a host-side error raised inside a callback during
`desc.create` surfaces because the constructor calls `rethrow_pending()` after it — **destroying what
the plugin already built first**, since a throw from a constructor runs no destructor and nothing else
would ever call `desc.destroy`.

Both bridges materialise the tree once per instance (`dict`/`list`, Lua table) and carry three more
fields beside it — `config_text`, `config_origin`, `config_opaque` — bound where `config` is: before
`__init__` in Python, at `atp._instantiate` in Lua. They **differ on encoding on purpose**: Python
decodes the text as UTF-8 strictly, so a file in another encoding fails the module's creation naming the
file, while Lua hands the bytes over as they are, a Lua string being bytes. A Lua table also loses key
order, which nothing addresses by position.

In studio the config is a **JSON area in the inspector, not a property row**, read-only while the
pipeline runs like the rest of the structure (via `state_.view->running()`, since `project` knows
nothing about a runner). `project::set_shared_config` exists because otherwise the reference form would
be unreachable from the GUI and a saved reference would not validate on reopening; it now also accepts a
`file:` string and still refuses a bare reference. A config that names a file is shown **read-only**,
previewed through the very `load_module_config` the run uses, so an unreadable file says here exactly
what it would say then; studio never edits or rewrites that file.

Naming trap: the edited object is `atp::studio::project` (`studio/project.hpp`, renamed from `document`; locals are `proj`), but the **MCP wire vocabulary stays "document"** — tools `new_document`/`open_document`/`save_document`/`get_document`, the `"document"` result key, the `atp://document` resource and `mcp/document_tools.hpp` all keep their names, while `workspace` exposes the object as `project()` with its path as `project_path()`/`project_dir()`.

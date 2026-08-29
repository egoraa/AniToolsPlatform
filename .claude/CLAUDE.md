# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Machine-specific setup — the exact generator, toolchain paths, Qt kit location and build directory of
the current machine — belongs in `CLAUDE.local.md` at the repo root, which is gitignored and per-developer.
Keep **this** file portable: anything naming an absolute path or a single operating system goes there, not here.

**Six more instruction files live in `.claude/subsystems/`.** They are *not* loaded automatically,
so **read the matching file before touching its subtree** — each holds rules that are not repeated
here, and several of them exist because the obvious tidy-up is wrong. A change in one of those subtrees
is also a change to its file.

| read this | before touching | what only it says |
|---|---|---|
| `.claude/subsystems/studio.md` | `src/studio/` | authoring script modules, the config tree widget, and the GUI rules that are easy to undo by tidying |
| `.claude/subsystems/runtime.md` | `src/runtime/` | the host side of the C path: `c_config`, the eleven `atp_api` callbacks and their traps |
| `.claude/subsystems/bridges.md` | `src/bridges/` | both script bridges — the shared UTF-8 rule, then Python and Lua one after the other |
| `.claude/subsystems/mcp.md` | `src/mcp/` | the stdio MCP server, its flags and its wire vocabulary |
| `.claude/subsystems/cmake.md` | `cmake/` | install, SDK export, packaging and CPack |
| `.claude/subsystems/templates.md` | `templates/` | the out-of-tree template projects that double as CI fixtures |

## Build and test

CMake ≥ 4.1, C++23. There is **no CMakePresets.json**; configure the build
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

The two script bridges are the second and third consumers of the C path, each embedding an interpreter
so that a module can be a script. The out-of-tree template projects double as CI fixtures and must be
edited whenever the ABI they name moves.

## Targets and layout

Almost everything is header-only, split into two INTERFACE targets: `atp_platform` exposes `include/` — the
**module author SDK**; `atp_runtime` adds `src/runtime/include/` on top — **host machinery**, all of it under
`<atp/runtime/...>` in `namespace atp::runtime` behind the umbrella `<atp/runtime.hpp>` (`group`,
`pipeline`, `pipeline_runner`, `module_loader`, `c_module`, `host_node`, `log_ring`, `log_pump`,
`thread_name`, `console_encoding`, `json_codec`, `raw_config` and the config subsystem — `config_source`,
`config_binding`, `config_file`, `config_loader`, `config_model`, `config_validator`, `pipeline_builder`).
**Two** runtime headers are
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
libraries. **There is no compile PDB to place anywhere**: `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` is
`Embedded` (`/Z7`), so debug information rides inside the objects and, for a static library, inside the
`.lib`. That is a **correctness** setting rather than a preference — do not give the tree a compile-PDB
directory instead, because a shared `vc140.pdb` leaves a line breakpoint binding nowhere, in any target —
the symptom is `LNK4099` at link time. Why, in full: `docs/architecture/cmake.md`, "Install, SDK
export and packaging", the bullet on the build tree. A plugin is the one target that does
**not** follow `CMAKE_LIBRARY_OUTPUT_DIRECTORY`: `atp_add_plugin` sends it to `ATP_PLUGIN_OUTPUT_DIRECTORY`,
which the root defines and an out-of-tree build does not, so the installed helper behaves as it always did.
That variable carries `$<CONFIG>` under a multi-config generator and must not under a single-config one —
CMake appends a per-configuration subdirectory only to a value with no generator expression in it, and the
plain spelling would put the plugins in `bin/plugins/Debug` while the hosts sat in `bin/Debug`. Two
consequences: **no post-build step copies a plugin or a bridge package next to `atp_app` or
`atp_studio`** — it would copy a file onto itself — and `atp_deploy_qt` deploys **once per
output directory**, the first caller owning the deployment and later ones depending on it. The
seven fixture plugins under `tests/test_plugin/` are the deliberate exception, kept in a directory of their
own: three of them are broken on purpose and several suites hand that directory to a scanner.

`atp_studio_lib` is the headless studio core (`src/studio`, included as `<atp/studio/...>`), also
linked into `atp_tests`. The GUI is `atp_studio`: Qt 6 Widgets, panels as private hpp/cpp pairs in
`src/studio/ui/` (namespace `atp::studio::ui`, no Q_OBJECT/moc), custom QGraphicsScene canvas — so a
panel class of its own has no `staticMetaObject`, `findChild<panel_type*>` does not compile, and a
child is looked up by object name through `QWidget*`/`QObject*` instead. Studio can **author** script
modules, not just host them, and it does so for **every language in `studio/languages.hpp`** — that
list is the one place a language is added. The canvas palette, the node mark, the Log dock, the four
GUI rules that are easy to undo by tidying, and how a module folder is provisioned:
`.claude/subsystems/studio.md`.

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
a round trip through CP1252 — and a test written that way is green while the setting is wrong.

**Both bridges carry paths as UTF-8 and never as `path::string()`**, because on Windows that
conversion throws for what the process code page cannot represent and the throw escapes an
`extern "C"` entry point that must not throw. What that costs each of them: `.claude/subsystems/bridges.md`.

`atp_mcp` is a headless MCP server over stdio on top of the studio core.

## Conventions

Full style spec: `docs/code_style.md`. Essentials:

- All code comments are written in English, live in headers only, and are Doxygen `///` blocks attached to a declaration (brief first sentence, then `@param`/`@return`/`@throws` as needed). **`.cpp` files carry no comments at all** — test files included; a `}  // namespace x` closer is layout and a `// NOLINT(...)` marker is a directive to a tool, neither of them a comment. A header has no floating `//` block over a namespace or a group of functions either: the explanation belongs to the declaration it is about. Comments explain design rationale ("why"), not mechanics; a "why" with no declaration to sit on goes into the matching file under `docs/architecture/` — commit messages are one line and hold no prose.
- Naming (STL-style, unified across the codebase):
  - Files: snake_case, one class per header, file name = class name (`queued_input.hpp` → `queued_input`).
  - Types, functions, variables: snake_case; data members get a trailing underscore (`value_`).
  - Type-erased/abstract bases: `_base` suffix (`io_base`, `input_base`, `module_base`).
  - Tag types: `name_t` plus a lowercase `inline constexpr` instance (`safe`/`unsafe`).
  - Template parameters: PascalCase with `T` prefix (`TBase`, `TItem`, `TInputs`).
  - Method prefixes: `try_` for non-throwing variants (`try_pop`), `do_` for private virtuals behind an NVI wrapper (`do_connect`).
  - gtest suite/test names stay PascalCase — that is googletest's own convention.

## Git

- **A commit message is one line.** No body, no trailer, no bullet list — the subject says what the change does and nothing else. Anything that needs a paragraph is documentation and belongs in `docs/` (`architecture/` for design rationale, `code_style.md` for conventions), where it is read and kept up to date; a commit body is neither.
- **Every change goes on a branch.** `master` is never committed to directly: branch first, work there, merge when it is done and green.

## Architecture

Full architecture digest with rationale: `docs/architecture.md` — a hub over `docs/architecture/`,
split by subsystem on the same boundary as `.claude/subsystems/` (`sdk`, `runtime`, `bridges`,
`studio`, `config`, `cmake`); the hub itself carries only the tree layout and the cross-cutting
conventions. Layer map:

**IO layer** (`include/atp/io/`): `io_base` → `input_base`/`output_base` → `input<T>` → `queued_input<T>`; `output<T>`. Thread safety is a **runtime property of the instance**, chosen at construction with the `safe`/`unsafe` tag. Delivery avoids `dynamic_cast`: the input answers `accepts(type_index)` (checked once, at connect) and receives via `deliver(const void*, erased_type)`. Reading is **pull-only** — `get()` for state, `take()` for events, both returning a **copy into the caller's frame**. An output keeps **no cache**: it delivers and counts writes (`write_count()`), nothing else, and there is no `peek()` and no replay connect. The write path materialises nothing when the type matches — the consistent snapshot is the caller's own object — spills to the heap above `io::heap_copy_threshold` (4 KiB) when a conversion needs a `T`, and hands an owned rvalue to the **last** subscriber through `deliver_move` (copying default, so an input kind unaware of it stays correct). Hence a value of any size is a legal port type. No user code ever runs on the writer's thread, so the callback ergonomics live in `io::watcher` instead: rules are registered in `initialize`, and `poll()` runs them from `iterate` on the module's own thread, returning the `work_status` to hand back to the runner. The one thing `deliver()` does fire on the writer's thread is the optional `notifier_base` the executor attaches to wake a sleeping consumer — it carries no value and is contractually forbidden from throwing or running user code, so pull-only reading is preserved. Gotchas:

- `erased_of<T>()` statics live **per-DLL** — use their contents only, never compare addresses.
- `input<std::any>` accepts anything; the reverse (`output<std::any>` → typed input) is deliberately unsupported.
- Registries are **not thread-safe** — setup phase only.
- Registries enumerate in **declaration order**, and that is a contract: `list()`/`entries()`/`owned()` hand back what the author wrote, in the order they wrote it. The store is a `std::vector` with a linear search, and the order is **paid for**, not free: measured in Release, `find()` over four ports costs 10.7 ns against a hash map's 8.3 ns, and the gap widens with size. It is the right price only because lookups happen at setup and on description requests, never in `iterate()`. **Nothing may sort ports or modules by name** — the declared order is the answer, and a view that re-sorts it disagrees with the canvas. The four remaining `ranges::sort` calls over ports or modules (`app/main.cpp`, `studio/ui/panels/runtime_widget.cpp`) rank metrics worst-first and are unrelated.
- Outputs hold **raw pointers** to connected inputs — `disconnect()` before destroying an input.
- `input<T>::store` is a **pair** (`T&&` and `const T&`): an input kind overriding one half must override the other, or values the writer does not own land in the base's storage silently.

**Properties** (`include/atp/io/`, the third kind of declared entity alongside inputs and outputs): typed setting values with a default, edited live and read pull-only, mirroring input reading (`get()` state, `take()` event «changed since last take»). Every write raises the changed flag — there is deliberately no comparison with the old value. **An enumeration is not a separate kind of property** — it is a non-empty `options()`, declarable either as a type-level name table (`enum_names<E>`) or as an instance-level set at the declaration, `make<property<int>>("channels", 2, allowed(1, 2, 6))`, which **replaces** the type table (that is how a module narrows an enum to the subset it supports). The invariant is that the value is always inside the set — default, typed write and `from_string` all pass one check against the canonical string, so `to_string()` never throws and the whole string layer (config, `-p`, studio) is safe. `property_kind` is **only** the form of the value — `{integer, real, boolean, text}` — and is independent of the set: a numeric enumeration still reaches the config as a number. Integer and real are apart for the reason `config::kind` keeps them apart, and a host therefore never recovers the form by re-parsing the printed string; the MCP wire says `integer`/`real` (a real's JSON Schema type stays `"number"`, the only word that schema has) and tolerates the older `"number"` as a real.

**Module layer** (`include/atp/`): `atp::module<TPorts, Name, Version>` implements `module_base`; name and version are NTTPs, readable at compile time and at runtime. Concrete modules subclass `atp::io::inputs`/`outputs`/`properties` and declare ports as reference members bound by `make<>()`; gathered by the `atp::ports<TIn, TOut, TProps>` node, which lives in the module layer (`module/ports.hpp`, concept `ports_list`) rather than in `io/`. `module<>` reaches its sections through the covariant `inputs()`/`outputs()`/`properties()` overrides — `inputs().count`, with the parentheses — and `module_base` declares those six as pure virtuals. `make<>` is short: `make<int>("count")` is an `input<int>`/`output<int>`, `make("gain", 0.5)` a `property<double>`; spelling the port or property type out still works and is what `c_module` does when the type comes from a runtime `atp_kind`. Lifecycle: **only `initialize(module_context&)` receives the context** — `start()` and `stop()` take no arguments, so a module needing services later stores the reference from `initialize`. `iterate(std::stop_token)` returns **`work_status`** (busy/idle — drives the runner's pacing) and gets no context either, being the hot path. Contracts:

- **`stop` must be correct after `initialize` without `start`** — fail-fast rollbacks call it.
- `service_directory` is the only service: publish typed interfaces in `initialize`, look peers up in `start` — that split makes module init order irrelevant; remove publications in `stop`. Type safety without `dynamic_cast`: `void*` + `type_index` equality guard.
- Per-instance settings are **properties**, not creation arguments — factories bind constructor config at registration, so all instances of one factory are identical and different configs are separate registrations.
- A plugin-created module pins its DLL against unload via the `shared_ptr` in `module_deleter` (it may outlive the loader).
- Plugin contract (`plugin.hpp`, split into `plugin/abi.hpp` + `plugin/build_id.hpp` + `plugin/handshake.hpp`; CMake reads the ABI number out of `plugin/abi.hpp`): C symbols `atp_abi_version()` and `atp_register_modules(atp::module_registrar&)`; `plugin_abi` is currently **1**; bump it on any ABI-incompatible change to what a plugin sees (`module_base` virtuals, io types, factories). The emphasis is on **ABI**: a rename or a move that breaks only a plugin's **source** is not a bump — it is answered by a clean break with no shims, the number's job being to refuse a stale **binary**. Host and plugins must share one toolchain and C++ runtime. `atp_build_id()` is an optional third symbol carrying the toolchain and standard-library identity, and the loader refuses a mismatch — it catches what the ABI number cannot (a Debug host with a Release plugin differs in `_ITERATOR_DEBUG_LEVEL`, i.e. container layout, i.e. memory corruption rather than a failed load). Its absence is tolerated silently, so adding it was not a bump; `ATP_PLUGIN_HANDSHAKE()` emits both.
- **Describing a module costs no instance.** `module_factory_base::declaration()` is pure virtual and answers `atp::module_declaration` — the declared ports and properties — from the type: `module_factory` builds `TModule::ports_type` alone and never calls the constructor, `c_module_factory` reads the C descriptors, `detail::pinned_factory` forwards. A module written by hand from `module_base` names no `ports_type`, and only that one is still described by a probe instance, which is the one place a throwing constructor still makes a module `broken` in the palette. `studio::port_info`/`property_info` are aliases of `atp::port_declaration`/`property_declaration`.
- **Foreign-language plugins** (`include/atp/plugin_c.h`, host adapter in `src/runtime/include/atp/runtime/c_module.hpp`): a second registration path, purely additive, `ATP_C_ABI` versioned separately from `plugin_abi` and expected to stay at **1** because it grows through `struct_size` fields instead. Adding one means: append to the struct, read it only behind a size check (`detail::c_desc_source` is the pattern), update `tests/platform/plugin_c_layout_tests.cpp` **and** the hand-written Rust mirror — and never move the acceptance floor, which is the frozen `ATP_MODULE_DESC_SIZE_V1` and not the current `sizeof`. The first such field is `source`, the file a module is declared in (the Python bridge's script); it travels beside the registration in `registered_module` rather than in the factory, because a factory is a plugin ABI type, and reaches `module_info::source` in `load_plugin`, not in `describe`. Three pure C symbols (`atp_c_abi_version`/`atp_module_count`/`atp_module_desc_at`, pulled not pushed), POD descriptors declaring ports and properties, and function pointers for the lifecycle; the host builds real `input<T>`/`output<T>`/`property<T>` from an `atp_kind`, so a foreign module connects to a C++ one with no adapter in the config. **Every C++ template, allocation and exception stays host-side** — that is what lets the plugin be a Rust `cdylib` and why `atp_build_id` is not checked there. Constraints worth knowing before touching it: the payload type set is closed (`blob` = `io::blob` is the escape hatch and must stay a real C++ type), ports are declared statically because the builder connects before it initializes, no allocation crosses the boundary in either direction, the boundary is exception-free both ways (`set_error` + a return code becomes a C++ exception host-side, and a host-side failure inside a callback is stored and rethrown after `iterate` so the plugin cannot swallow it), and the adapter stores `module_host*` rather than `module_context*` because `group::initialize` builds each child's context on its own stack. The host side of it — `c_config`, the eleven `atp_api` callbacks and their traps — is in `.claude/subsystems/runtime.md`. Rationale in full: `docs/architecture/sdk.md`, "The C path: modules in other languages".

**Execution platform** (`src/runtime/include/atp/runtime/`, `namespace atp::runtime`, target `atp_runtime`): `runtime::group : module_base` is an owning **composite** whose lifecycle cascades recursively in insertion order (`initialize` — local fail-fast with reverse-order stop of the initialized; `stop` — reverse order, continues on error, rethrows the first; `iterate` — busy-wins aggregation). Group ports are **aliases** to child ports (path form `"child.port"` only). A group is not a unit of execution and not thread-safe. `runtime::pipeline` is the aggregate root; `runtime::pipeline_runner` owns the named threads. Contracts:

- `start()` **validates** that no cross-thread connection lands in an `unsafe` input — the thread boundary is the criterion, and the error names the threads.
- Errors: first one wins and stops the whole pipeline; `wait()` blocks until the first error, shuts down and rethrows (also after a prior `stop()`; stored until the next `start()`). `stop()` is idempotent and never throws.
- All runner control is **owner-thread-only** — the stop/wait race is excluded by contract, not by synchronization.

**The config document** (schema **1.0**, `runtime::config_schema_version`): a group's children live under
`"modules"`, and a module node carries `"properties": {"name": scalar}` and `"config"`. The `children`,
`params` and `replay` keys are **rejected as unknown**, and tests pin that so they cannot creep back;
nesting under `properties` is a validator error. An entry of `"plugins"` may name a **directory**: the
plugins directly in it are loaded, non-recursively and sorted by file name, deduplicated by
`weakly_canonical` against the rest of the list. A file there that exports neither entry point throws
`atp::runtime::not_a_plugin` and is **skipped** when it came from a directory; every other failure —
including a file that would not open at all — stops the host, and a file named explicitly is forgiven
nothing.

A module's `"config"` is an **object in place**, a **string naming an entry of `"configs"`** (parsed on
the **first** colon; no prefix means the document's top-level `"configs"`), or a **`"file:<path>"`
string**. An entry of `"configs"` must be an object or a `file:` string and **never a bare reference to
another entry** — that one rule is the whole defence against cycles, since a chain of references is then
exactly one step long. The model stores every spelling **verbatim**, so `encode(decode(doc)) == doc` and
neither a reference nor a file is ever expanded on save; resolution happens in `pipeline_builder`, which
is also where a file is read (`runtime/config_file.hpp`). **The extension decides the format**: `.json`
must parse (a broken one is an error with a position, not a silent slide into "unknown format"), anything
else is opaque text with a null root, `is_opaque()` true, the bytes in `text()` and the path in
`origin()` — and `is_opaque()` is **not derivable**, since a `.json` holding literally `null` also leaves
an empty tree beside a non-empty text. A relative path resolves against **the document's directory**;
without one the error says so by name rather than resolving against the process's current directory.
`file:` is **not** a duplicate of `$include`, which is a textual expansion done before validation: saving
inlines it and the reference disappears, it cannot express a format the host does not parse, and it
leaves no `origin()`.

`runtime/property_override.hpp` implements the edit-by-path vocabulary — `parse_property_override` splits
"path.prop=value" on the **first** `=`, then the **last** `.` to the left — used by
`atp_app -p path.prop=value` (repeatable, applied before `runner.start`, so modules see the values already in
`initialize`) and by studio's `session::set_property`.

**Declared config** (`include/atp/module/module_config.hpp`, `plugin_abi` 1): a module's config is an
**object with declarations in it** — an heir of `atp::module_config` names its fields as reference members
(`field` scalar, `group` nested object, `list` array), the module names the type as
`using config_type = ...` and takes ownership of one, `std::unique_ptr<my_config>`, in its constructor.
Required means **no default** (`field<double>("gain")`); a `const char*` overload exists because a literal
is not one of the four scalar forms. **An enumeration is not a seventh kind**, exactly as it is not a
seventh kind of property: `field("layout", channel_layout::stereo)` binds a `channel_layout&`, its kind is
`string`, and what makes it an enumeration is a non-empty `entry::options()` — filled from the type's
`io::enum_names` table, or from `io::allowed(...)` listed at the declaration, which **replaces** the table
so a module can narrow an enum to what it supports (`io/option_set.hpp` holds that vocabulary, kept
out of `property_base.hpp` so a config can name it without dragging `io_base` along). Two
consequences: the storage of an enum field is a slot in `owned_`, not one of the four deques, because the
set of enum types is open; and `entry` records `std::type_index` beside the kind, with `to_string`/
`from_string` going through a per-type ops table, because two enums and a plain string all answer
`field_kind::string` and casting one storage to another would reinterpret an object of a different size.
`from_string` is the **one door** for the whole string kind, host-side included: it is what checks the
set, and `load_fields` reading an enum with `set(std::string)` would throw where it owes a problem line.
Six things to know before touching it:

- **The base knows nothing about the document** — no node, no parser, no path grammar — which is what
  keeps a document library out of the plugin ABI and lets a host describe and edit a config whose module
  it never built.
- **The factory makes the config and the host fills it**: `module_factory_base::make_config()` hands out
  the object at its declared defaults, `runtime::load_fields` (`runtime/config_binding.hpp`) fills it from
  a `config_source` and collects problems as "path: what is wrong" lines **without ever throwing**,
  `load_fields_or_throw` names the file and every problem at once, and `create(config_ptr)` hands the
  filled object to the module — so it is built **once**.
- **`is_set()` is contract, not decoration**: without it a required field nobody filled is
  indistinguishable from one filled with the zero of its type, and only a write **through an entry**
  raises it — a module writing to its own config through the bound reference is its own business.
- **A whole number widens into a real and never the reverse** — JSON writes `48000` for a real,
  `config::node` keeps the two apart, and without the widening a real field would silently take its
  default.
- **Every container is a `deque`**, including what `list<T>` returns: an heir binds references into the
  base's storage, so `module_config` is neither copyable nor movable and `std::vector` would not even
  compile; that is also why `entry::resize` grows an array of objects with `emplace_back` rather than
  `deque::resize`.
- **The tree of a document is `atp::runtime::raw_config`** — the same base with no field declared, taking
  the document whole through `adopt()` and offering `root()`/`find(path)`/`at(path)` on top of it. It
  lives host-side and is what the C path and both bridges get; a C++ module reading its own `.ini`
  declares `using config_type = atp::module_config;` and reads `text()`.

There is **no `module_declaration::config_schema`**: MCP walks `module_config::entry` for its
`"config"` key (`.claude/subsystems/mcp.md`) and studio holds the factory's own prototype in
`studio::module_info::config_schema` (`shared_ptr<const atp::module_config>`, empty for a module that
declares none and for a `raw_config`), drawing it as a tree (`.claude/subsystems/studio.md`). Whether a config
reaches the module at all is the separate `module_info::takes_config`. The shape in full:
`docs/architecture/config.md`, "The declared form" and "Transport".

**The config channel** is the third declared entity beside ports and properties: a structured setting
reaching the module in its **constructor**, that is before both `connect` and `initialize`, for what a
property cannot express (a list, a table, a nested object) and what is not edited live. What a
constructor receives is the config **as a whole** — its declared fields with their values and, when it
came from a file, the bytes of that file and its name.

**`atp::config::node` describes one node** of a document and is deliberately left as that: a closed
seven-form variant with **no parser and no `dump()`** (naming `nlohmann::json` in an ABI signature would
drag it into every plugin), both of which live host-side — the text boundary in `runtime/json_codec.hpp`,
the conversion to and from the protocol's node in `runtime/config_value_json.hpp`. It **may** be built and
edited in place (`operator==`, an inserting `operator[](key)`, `push_back`, `erase`, all layout-neutral so
no ABI moved); what the type must not name is a document library, not that it is read-only. There is no
`set(key, value)` — it said exactly what `node[key] = value` says. Walking a node is `entries()` (object)
and `elements()` (array), each handing back the vector itself and a shared empty one for any other form,
so a traversal never asks the kind first; a node is never walked by index. **`elements()` is not called `items()` on purpose** — `nlohmann::json::items()`
means the opposite (an object's key/value pairs) and both types are handled side by side here.

**The document is a `config::node` everywhere** — `load_config`, `validate`, `decode`/`encode`,
`module_node::properties`/`config`, `config::configs`, and studio's project with its undo stack. JSON is a
format at the file and protocol edges, not a type in the middle, and the model therefore keeps
`5` distinct from `"5"`. Two consequences: an object keeps **insertion** order, not the order written in
the file, while `json_dump` sorts keys on the way out, which is what makes a saved document
reproducible byte for byte; and because `encode` writes keys in its own order
while `node`'s equality is order-sensitive, the round-trip invariant is pinned on the **dumped text**,
`json_dump(encode(decode(doc))) == json_dump(doc)`. `docs/architecture/config.md`, "The document
model".

`module_factory_base::create` takes a `config_ptr` and refuses one another factory made rather than
casting it blindly; `module_registry::create` keeps overloads that make the config themselves (at its
declared defaults, **unfilled** — filling means reading a document, and there is none there), and
`module_factory` deliberately does not. Its class constraint is a **disjunction**, because a module whose
only constructor takes a config is constructible from no arguments at all: the named concept
`factory_constructible` over `declares_config` and `takes_config`, which **`module_registry::add` and
`module_registrar::add` require exactly** — plain `constructible_from<TModule>` is not enough there, and
leaves such a module unregistrable by the ordinary call while `module_factory` accepts it directly. A module joins
the channel only by declaring such a constructor.

Path access is `cfg.find("channels[2].rate")` / `at(...)`: segments split on `.`, an index as `[N]` after
a segment, a leading `[N]` for an array root, and a key containing `.` or `[` reachable only through
`root()`. Parsing always runs to the end of the string, so the grammar verdict never depends on the data,
and the **two failures are different on purpose** — a malformed path throws `config::access_error` even
from `find` (a typo in the module's own source must not become "nothing there"), while something absent is
`nullptr` from `find` and a `config::access_error` naming the full path and where it stopped from `at`.
The two config exceptions are **`atp::config::access_error`** — one node, one index or one path is wrong —
and **`atp::runtime::config_error`** — the host could not read, validate or build a config. Typed reads with a
fallback are the free functions `config::int_or`/`bool_or`/`double_or`/`string_or` (in
`<atp/config/read.hpp>`, reached through the `<atp/module.hpp>` umbrella) over a **nullable** node, which
is why they serve both levels of lookup and are not members — and they are the **only** fallback
vocabulary: there is no `node::*_at(key, fallback)` member. The
throwing `node::*_at(key)` stay, for a message that names both the key and the two forms.

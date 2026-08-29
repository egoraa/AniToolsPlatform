# AniToolsPlatform architecture

A header-only C++23 platform for modular pipelines: modules with typed ports connect into groups, and
groups are executed by the runner's named threads. This is the rationale behind the decisions; the
contracts themselves are in the code comments (Doxygen on the public API).

## How to read this

The document is split by subsystem, on the same boundary as `.claude/subsystems/`. The two differ in
genre: those files hold the rules you must follow when editing a subsystem, these hold why the rules
are what they are.

| file | covers | what only it says |
|---|---|---|
| [`architecture/sdk.md`](architecture/sdk.md) | `include/atp/` | the io layer, the module layer, the plugin ABI and the C path |
| [`architecture/runtime.md`](architecture/runtime.md) | `src/runtime/`, `src/app/` | the composite, the runner, logging, metrics, `atp_app` and its control channel |
| [`architecture/bridges.md`](architecture/bridges.md) | `src/bridges/` | the Python and Lua bridges |
| [`architecture/studio.md`](architecture/studio.md) | `src/studio/` | the studio core, canvas, docks, script languages, attached mode |
| [`architecture/config.md`](architecture/config.md) | a cross-cutting theme | the module config from declaration to transport, and the document model |
| [`architecture/cmake.md`](architecture/cmake.md) | `cmake/` | install, SDK export, packaging, licensing |

Below is what belongs to no single subsystem: the layout of the tree, and the conventions common to
all of the code.

## Layout

- `include/atp/` — the **module author's SDK** (target `atp_platform`, INTERFACE), arranged into
  subsystems as "folder plus umbrella header": `io/` (`io.hpp`), `module/` (`module.hpp`),
  `hosting/` (`hosting.hpp`), `config/` (`config.hpp`) and `plugin/` (`plugin.hpp`). Only those five
  umbrellas and `plugin_c.h` sit at the root. The canonical include is `<atp/...>` everywhere.
  - **The folder is the box, the umbrella is the audience**, and the two are not the same thing.
    `module.hpp` gathers what a **module author** writes against: the `module<>` class, the base, the
    context, the host, services, the module config, `log_level`, the io layer and the config
    vocabulary. `hosting.hpp` adds what only a **host or loader** needs: factories, the registry, the
    registrar, `null_host`. A new header is declared in the umbrella whose audience needs it — that is
    the rule for extending them, and the same rule decides what a module author is not shown.
  - `support/` **deliberately has no umbrella**: it is not a subsystem but utilities (`version`,
    `fixed_string`, `type_compare`) with no common audience, included by name.
- `src/runtime/include/atp/runtime/` — the **host runtime** (target `atp_runtime`, INTERFACE on top of
  `atp_platform` and `atp_json`): `group`, `pipeline`, `pipeline_runner`, `module_loader`, `c_module`,
  `c_config`, `host_node`, `log_ring`, `log_pump`, `command_queue`, `connection_sample`, `thread_name`,
  `console_encoding`, `json_codec`, `raw_config` and the config subsystem. All of it is one folder, one
  `namespace atp::runtime` and one include, `<atp/runtime/...>`. The umbrella is `<atp/runtime.hpp>`,
  and **two headers are deliberately left out of it**, both for the same reason: a header that reaches
  every consumer of the runtime must not drag either the socket stack
  (`runtime/socket_platform.hpp` exists for `<winsock2.h>`) or a document library
  (`runtime/config_value_json.hpp` names `nlohmann::json` in its signatures) into a translation unit
  that only wanted a pipeline. Whoever needs them includes them by name. Plugins see only the root
  `include/`; the host sees both.
- `examples/demo` — the pipeline demo; `examples/plugin_demo` — one module as both a monolith and a
  DLL plugin; `examples/plugin_c_demo` — a plugin in **pure C** through `plugin_c.h`, the only place in
  the tree that proves that path really needs no C++; `tests/` — googletest.
- The build uses native toolchains: Linux/GCC (Ninja) and Windows/MSVC (VS Build Tools 2022).
  Development happens in CLion with its bundled tools (profile in `cmake-build-debug/`).

## Cross-cutting conventions

- Two styles of type check, deliberately: registries use exact `typeid`, connections use the input's
  `accepts()`.
- Configuration errors are exceptions thrown at the point of the call (`invalid_argument` for a bad
  argument, `runtime_error` for a conflict or a missing entry); `at` throws where `find` returns
  nullptr.
- Lifetimes are explicit contracts on the caller (disconnect before destroying an input; an alias does
  not outlive its port; the module registry outlives the loader), pinned by construction wherever
  possible — the group's destructor, the DLL pin, member order.

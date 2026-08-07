# AniToolsPlatform

A header-only C++23 platform for modular pipelines. Modules declare typed ports, connect into groups,
and a runtime executes them on named threads with a documented threading contract. On top of the same
core sit a Qt visual editor and an MCP server, so a pipeline can be built by hand, in a GUI, or by an
agent — and it is the same pipeline in all three cases.

The platform is designed around third-party modules: plugins are built separately, against an
installed SDK, and loaded through a versioned ABI — in C++, or in any language that can export three
C functions.

## Targets

| Target | What it is |
|---|---|
| `atp_platform` | **The module author SDK** (`include/`, INTERFACE): the io layer, `module`/`module_base`, factories, the registry, `plugin.hpp`, and `plugin_c.h` for a plugin that is not C++. A plugin links this and nothing else. |
| `atp_runtime` | **Host machinery** on top of it (`src/runtime/include/`): `group`, `pipeline`, `pipeline_runner`, `module_loader`, and the JSON config subsystem. Hosts link this; plugins must not. |
| `atp_app` | A config-driven host: `atp_app config/demo.json`. See [Driving a running host](#driving-a-running-host). |
| `atp_studio` | The Qt 6 visual editor. |
| `atp_mcp` | An MCP server over stdio exposing the catalog, editing and execution as tools. |
| `atp_demo`, `atp_host_static`, `atp_host_dynamic` | The examples in `examples/`. |
| `atp_demo_plugin`, `atp_c_demo_plugin` | Loadable example plugins: one in C++, one in pure C. |

## Build

CMake ≥ 4.1. There is no `CMakePresets.json` — configure a build directory directly.

```bash
cmake -S . -B <build-dir> [-G <generator>] [-DCMAKE_PREFIX_PATH=<qt-kit>]
cmake --build <build-dir>
ctest --test-dir <build-dir>
```

**One caveat worth stating once.** With a multi-config generator (Visual Studio, Xcode, Ninja
Multi-Config) the configuration is chosen at *build* time with `--config`, and binaries land in a
per-config subdirectory. With a single-config one (plain Ninja, Unix Makefiles) it is chosen at
*configure* time with `-DCMAKE_BUILD_TYPE`, and there is no such subdirectory. Every `--config`/`-C`
flag below is a no-op in the second case.

Qt 6 Widgets is needed for `atp_studio` only; without it CMake reports `Qt6 not found` and builds
everything else. `-DATP_WERROR=ON` turns warnings into errors (CI sets it; a local build should not).
`-DATP_SANITIZER=thread` or `=address,undefined` instruments the whole tree.

## Driving a running host

`atp_app` can open a control channel that speaks the same MCP as `atp_mcp` — newline-delimited
JSON-RPC, one request per line:

```bash
atp_app config/demo.json --control 7777    # 0 asks the OS for a free port and prints it
```

Seven tools are served: `get_status`, `describe_pipeline`, `read_connections`, `set_module_metrics`,
`read_module_metrics`, `set_live_property` and `stop` — the last one shuts the host down the way
Ctrl+C would. There is no `run`: the host builds its pipeline from the config it was given and does
not rebuild it.

> **The channel is unauthenticated.** It binds to `127.0.0.1`, so it is not reachable from the
> network, but every process on the machine can drive it — and `stop` means what it says. TCP offers
> neither the file permissions of a socket file nor a portable way to identify the peer. This is why
> the endpoint stays closed unless you name a port.

## Watching a deployed pipeline

The studio can attach to a host that is already running and mirror its pipeline:

1. start the host with a control channel — `atp_app config/demo.json --control 7777`;
2. in the studio, `Host > Attach to a running host...` and give it the address.

The canvas then shows the remote graph, with live values on the connections and the Runtime dock
reporting its threads and per-module cost. The structure is read-only — you are looking at something
you did not build — but properties can still be edited on the fly, which is the point: this is how a
deployed pipeline gets tuned. `Host > Detach` brings your own project back exactly as you left it.

The mirror is a real config, so `Save As...` exports it. It carries the graph and nothing else: the
plugins the modules came from and the thread layout are not part of a running pipeline in any
readable form, so they cannot be recovered from one.

## Writing a module

A module declares its ports as reference members bound by `make<>()`, and the sections it does not use
are simply omitted:

```cpp
#include <atp/module.hpp>

struct counter_outputs : atp::io::outputs {
    atp::io::output<int>& count = make<atp::io::output<int>>("count");
};
struct counter_props : atp::io::properties {
    atp::io::property<int>& step = make<atp::io::property<int>>("step", 1);
};
using counter_ports = atp::io::ports<atp::io::inputs, counter_outputs, counter_props>;

class counter : public atp::module<counter_ports, "counter", atp::ver<"1.0">> {
   public:
    atp::work_status iterate(std::stop_token) override {
        outputs().count(next_);
        next_ += properties().step.get();
        return atp::work_status::busy;
    }

   private:
    int next_ = 0;
};
```

The name and the version are template parameters, readable both at compile time and at run time.
`iterate` returns `busy` or `idle`, which is what paces the runner. Reading is pull-only: `get()` for
state, `take()`/`try_pop()` for events — no user code ever runs on the writer's thread. `examples/demo`
has the rest, including a heterogeneous chain and the universal `std::any` input.

## Writing a plugin

Install the SDK, then build the plugin as its own project:

```bash
cmake --install <build-dir> --prefix <sdk-prefix>
cmake -S templates/plugin -B <plugin-build> -DCMAKE_PREFIX_PATH=<sdk-prefix>
cmake --build <plugin-build>
```

```cmake
find_package(AniToolsPlatform REQUIRED)
atp_require_plugin_abi(10)
atp_add_plugin(my_plugin SOURCES plugin.cpp)
```

`atp_require_plugin_abi` fails at configure time if the SDK's ABI is not the one the plugin was
written for, instead of leaving it to be refused at the `atp_abi_version()` handshake inside the host.
`atp_add_plugin` sets the target properties that are load-bearing rather than cosmetic — hidden
visibility, the toolchain-agnostic file name, linking `atp::platform` alone. `templates/plugin/` is a
working starting point; copy it anywhere.

Host and plugins must share one toolchain and one C++ runtime. The `atp_abi_version()` handshake cannot
detect a violation, so `ATP_PLUGIN_HANDSHAKE()` also exports `atp_build_id()` — a string describing the
toolchain and standard library, which the host compares and refuses on a mismatch. Without it a Debug
host loading a Release plugin corrupts memory instead of failing to load.

## Writing a plugin in another language

The requirement above is a property of the C++ contract: a plugin compiles its own copies of the
registries, instantiates its own ports and reports errors by throwing. For a plugin that is not C++
there is a second, purely C path — [`include/atp/plugin_c.h`](include/atp/plugin_c.h), three exported
symbols and no other header:

```c
#include <atp/plugin_c.h>

ATP_C_EXPORT unsigned atp_c_abi_version(void)                   { return ATP_C_ABI; }
ATP_C_EXPORT unsigned atp_module_count(void)                    { return 1; }
ATP_C_EXPORT const atp_module_desc* atp_module_desc_at(unsigned i) { return i == 0 ? my_desc() : NULL; }
```

A descriptor declares the module's ports and properties as plain data and hands over function pointers
for `create`/`iterate`/`destroy`; the host builds real platform ports from it, so such a module connects
to a C++ one with nothing in between. Every C++ template, allocation and exception stays on the host's
side of the boundary — the plugin contains no `std` type and frees nothing the host allocated — which is
why it can be a Rust `cdylib` or a bridge embedding an interpreter, and why `atp_build_id` is not checked
there. The price is a closed set of port payload types (integers, `double`, `bool`, text, and a byte blob
as the escape hatch for everything else).

Declare the target with the `C_ABI` keyword, which gives it the SDK's headers without imposing C++ on it:

```cmake
atp_add_plugin(my_c_plugin C_ABI SOURCES plugin.c)
```

`examples/plugin_c_demo/` is a working plugin in pure C, and `atp_app config/c_demo.json` runs it wired
between two C++ modules. `templates/plugin_rust/` is the same idea in Rust — a `cdylib` built by `cargo
build` alone, with a hand-written mirror of the header in `src/abi.rs` and no dependencies at all. Copy
either one to start.

One thing to get right in a language with its own unwinding: a panic crossing into a C++ frame is
undefined behaviour, so guard every entry point (the Rust template wraps them in `catch_unwind` and
reports the panic through `set_error`, which stops the pipeline with the panic's text instead of the
process).

## Reading further

- [`docs/architecture.md`](docs/architecture.md) — the design digest, with the rationale behind the io
  layer, the execution platform, the config schema, the studio core and the packaging (in Russian).
- [`docs/code_style.md`](docs/code_style.md) — the style spec that `.clang-format` and `.clang-tidy`
  enforce.
- [`docs/benchmarks.md`](docs/benchmarks.md) — measured cost of the io and runtime hot paths, how to
  reproduce it, and what the numbers say about `unsafe`, `drain()` and on-demand wake-up latency
  (build with `-DATP_BUILD_BENCHMARKS=ON`).
- `cmake --build <build-dir> --target docs` — the Doxygen API reference, if doxygen is installed.

## License

Apache License 2.0 — [`LICENSE`](LICENSE), [`NOTICE`](NOTICE). Copyright 2026 The AniToolsPlatform Authors,
listed in [`AUTHORS`](AUTHORS).

One license for the whole repository, the SDK included, so a plugin built against `atp::platform` can
be licensed however its author wants — closed included. What Apache-2.0 adds over MIT is the patent
grant of section 3, which is what this platform actually needs: a plugin compiles these headers into
itself across a versioned ABI, and the grant makes that unambiguous rather than implied. Section 5
makes a contribution arrive under the same terms, so there is no CLA to sign.

Third-party components that reach a user are listed in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md). One of them is not permissive: `atp_studio` uses Qt
6 under the LGPLv3 — dynamically linked, with the Qt libraries shipped beside the executable and
replaceable by whoever received them, which is what that license asks for. A build configured with
`-DATP_BUILD_STUDIO=OFF`, and any package without `atp_studio` in it, carries no Qt code at all.

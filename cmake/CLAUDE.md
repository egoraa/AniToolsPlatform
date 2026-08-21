# Install and packaging

**Installing the SDK**: `cmake --install <build-dir> --prefix <p>` gives a package that
`find_package(AniToolsPlatform)` consumes out of tree, exporting exactly one target, `atp::platform`
(also an alias in-tree, so both spellings name the same thing). Rules live in `cmake/Install.cmake`
plus the template `cmake/AniToolsPlatformConfig.cmake.in`; `ATP_INSTALL` defaults to
`PROJECT_IS_TOP_LEVEL`. The package carries `AniToolsPlatform_PLUGIN_ABI` and
`atp_require_plugin_abi(<n>)`, which turns an ABI mismatch into a **configure** error instead of a
runtime handshake failure; the number is parsed out of `include/atp/plugin/abi.hpp` at configure time, so
the header stays the single source of truth. `atp_runtime` is deliberately **not** exported — a plugin
must not link the host runtime, and it would drag the FetchContent'd nlohmann/json along; the reasons
are written out in `cmake/Install.cmake` and `docs/architecture.md`.

**Packaging**: the same `ATP_INSTALL` switch also installs `atp_studio`/`atp_app`/`atp_mcp` into
`bin/`, the sample configs into `bin/config/` and everything loadable — both demo plugins, the Python
bridge, the scripts it reads — into `bin/plugins/`, with the Qt runtime placed in the install tree by
`qt_generate_deploy_app_script` (the build-tree `atp_deploy_qt()` knows nothing about it). The plugin
directory is named **once**, by `ATP_PLUGIN_DIRNAME` in the root `CMakeLists.txt`, and the same name is
laid out next to `atp_app` in the build tree: a config reaches a plugin as `../plugins/<name>` relative
to itself, so the two trees must spell it alike or `atp_app config/demo.json` means two different
things. Each plugin is installed **by the directory that declares it** — `examples/plugin_demo`,
`examples/plugin_c_demo`, `src/bridges/python` — and `src/app` installs only `atp_app`, the configs and
`averager.py`, which is an asset of the sample pipeline rather than of the bridge. `cmake/Packaging.cmake` configures CPack — ZIP on Windows, DMG on macOS, TGZ on Linux — and
is included **last**, because `include(CPack)` freezes the rules and variables as they stand. Releases
are a separate workflow, `.github/workflows/release.yml`, on `v*` tags and on `workflow_dispatch`.

The three plugin templates ship in the `sdk` component under `<prefix>/templates/` — `templates/plugin`
cannot even be configured without an installed prefix to point `find_package` at, so a package without
them hands an author a reference to something they do not have. The rules exclude `target/` and
`Cargo.lock`: both exist in a developer's tree and are gitignored, and `install(DIRECTORY)` reads
neither git nor `.gitignore`.

Declaring a plugin is `atp_add_plugin(<name> SOURCES ...)` from
`cmake/AniToolsPlatformPluginHelpers.cmake`. The file is installed verbatim into the package and
included both by the package config and by the root `CMakeLists.txt`, so the in-tree plugins are
declared by the very function an out-of-tree author calls — when changing the helper, remember both
callers.

# Build, install and licensing (`cmake/`)

How the SDK leaves the tree and what a shipment turns into: the exported package, declaring a plugin
in one line, the shape of the build and install trees, packaging — and the licence that travels with
all of it. The document map and the tree layout are in `../architecture.md`.

## Install, SDK export and packaging (`cmake/Install.cmake`, `cmake/Packaging.cmake`)

- `find_package(AniToolsPlatform)` yields **one** target, `atp::platform` (headers in
  `CMAKE_INSTALL_INCLUDEDIR`, package in `lib/cmake/AniToolsPlatform`). The same name exists in the
  tree as an alias of `atp_platform`, so an out-of-tree example spells its dependency exactly as
  in-tree code does.
- The target carries **its own** requirements rather than those of the build that produced it:
  `BUILD_INTERFACE`/`INSTALL_INTERFACE` instead of a bare path (otherwise the builder's own directory
  lands in the export), and `target_compile_features(... cxx_std_23)` — a global `CMAKE_CXX_STANDARD`
  never reaches the consumer, and without this an out-of-tree plugin compiles under its compiler's
  default and fails inside the headers.
- **The ABI number is a package constant.** The config sets `AniToolsPlatform_PLUGIN_ABI` and a
  function `atp_require_plugin_abi(<n>)`, so a mismatch becomes a **configuration** error rather than a
  refusal at the `atp_abi_version()` handshake — that is, before building and deploying, which is what
  a versioned ABI was introduced for. The number is read out of `include/atp/plugin/abi.hpp` by a
  regex at configure time and exactly one line has to match: the source of truth stays in C++ and the
  package constant cannot drift from it.
- **`atp_add_plugin(<name> SOURCES ...)`** (`cmake/AniToolsPlatformPluginHelpers.cmake`) declares a
  plugin in one line. The file is installed into the package **verbatim** and is included both by the
  package config and by the root `CMakeLists.txt`, so the in-tree plugins — the demo plugins, both
  bridges and the seven test fixtures — are declared by the very function an outside author calls, and
  it cannot quietly drift away from them. What it sets is not cosmetic: hidden visibility (on which
  name-based type identity rests, `type_compare.hpp`), `PREFIX ""` with an explicit `OUTPUT_NAME` (the
  file name is part of the config format, and `module_loader` appends the extension), and a link
  against `atp::platform` **alone**. The target type is `MODULE`: CMake refuses to link such a library
  into another target, which turns "a plugin is opened by path, not linked" from an understanding into
  a configuration error. On Apple, `MODULE` defaults to `.so` while
  `atp::runtime::plugin_extension` is `.dylib` there, so the suffix is set explicitly — otherwise an
  extensionless path in a config, and the plugin-directory scan, would break on that one platform. A
  static CRT under MSVC is the one incompatibility invisible to the ABI handshake, so the function
  warns about it.
- **`templates/plugin/`** is a plugin project that is not part of the platform's build and reaches the
  SDK only through `find_package`. It doubles as the fixture of the `out-of-tree plugin` CI job, which
  installs the SDK into a staging prefix, configures the template as a separate project, builds it and
  runs `atp_app` on a config wiring `counter` → `doubler` → `printer` — so **both ends of every
  connection come from different libraries**. Nothing else in the repository is like that, which is
  how a type-identity failure across the plugin boundary on macOS once reached master unnoticed. The
  payload types are chosen, not incidental: `int` goes in — every library agrees about it in advance,
  so that half proves only that delivery happened — and `std::string` comes out, which is where the
  boundary breaks: the type must be recognised as the same one, and the buffer is freed by the side
  that did not allocate it (the shared-C++-runtime contract, invisible to the ABI handshake). The run
  is bounded from outside by `timeout`, and exit code 124 is success: the pipeline worked the whole
  window and did not leave on its own, which it does only on error.
- **`atp_runtime` is not exported**, and that is a decision rather than an omission. A plugin that
  linked the host runtime would pull a second copy of the registries and the loader into the process;
  on top of that `atp_runtime` pulls nlohmann/json, which arrives through FetchContent and installs no
  rules of its own, so exporting would mean either shipping someone else's headers under our prefix or
  a package that cannot be configured without a third-party json. A host that needs the runtime builds
  the repository.
- The package version is `SameMajorVersion` and `ARCH_INDEPENDENT`: the pointer-size check compares
  the machine that built the SDK with the consumer's and says nothing about headers, while the
  compatibility that actually matters here — one toolchain and a shared C++ runtime — cannot be
  expressed in a version file at all, and the ABI constant is the closest machine-checkable form of it.
- `ATP_INSTALL` defaults to `PROJECT_IS_TOP_LEVEL`: a subproject's install rules would otherwise land
  in the parent's package.
- **The applications are installed beside the SDK as one tree:** `atp_studio`, `atp_app` and `atp_mcp`
  in `bin/`, the sample configs in `bin/config/`, and everything loadable — the demo plugins, the
  bridges and the scripts they read — in `bin/plugins/`, so that `atp_app config/demo.json` means the
  same thing in an unpacked release as it does in the documentation. The directories are kept apart: a
  config directory is no place for a `.dll`, and the plugin directory's name is set once, by
  `ATP_PLUGIN_DIRNAME` in the root `CMakeLists.txt`, because a config addresses a plugin as
  `../plugins/<name>` relative to itself and that works only while the build tree and the install tree
  name the directory identically. Each plugin's install rule sits where its target is declared
  (`examples/plugin_demo`, `examples/plugin_c_demo`, `src/bridges/python`), not in `src/app`, which is
  merely the first consumer. Qt is placed into the install tree by
  **`qt_generate_deploy_app_script`** rather than a second windeployqt call: the build-time
  `atp_deploy_qt()` serves the build directory and knows nothing about the install one, whereas Qt's
  generator is the only thing that speaks all three platforms at once (windeployqt, macdeployqt, and
  Linux, where there is no tool and the libraries and plugin directories are laid out by hand).
  Translations are disabled — the interface is English and does not read them.
- **The build tree has the same shape as the install tree**, and that is not cosmetic: `bin/` for
  executables with Qt beside them, `bin/plugins/` for everything loadable, `bin/config/` for configs,
  `lib/` for static and import libraries. There is no compile PDB in this layout at all: under MSVC the
  debug information is embedded in the object files (`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` =
  `Embedded`, i.e. `/Z7`) — a **correctness** setting rather than a preference. A compile PDB is a type
  server that an object refers to by a (GUID, age) pair, and CMake passes the file name in `/Fd` **only
  for a static library**; every other target gets the default name `vc140.pdb`, so naming a directory
  for them means the whole tree writes into one shared file. The age grows on each write, objects
  compiled before the last growth stop matching, and the linker answers `LNK4099` and links them "as
  if there were no debug information": public symbols survive in the PDB while types, locals and line
  tables disappear, and a line breakpoint binds nowhere any more — not in a plugin, and not in the host
  itself. Embedding also covers what the directory was named for: a static library now carries its
  debug information inside the `.lib`. The variable is only apparently platform-specific: its value is
  **ignored by a compiler that does not target the MSVC ABI**, so gcc and clang build unchanged, and
  the generator expression is the same as CMake's default, so Release grows no heavier either way.
  The converse is worth knowing too: the single Windows CI job builds Release, which is exactly the
  configuration where the expression is empty — nobody but a developer on their own machine exercises
  Windows Debug. The three `CMAKE_*_OUTPUT_DIRECTORY` variables are set in the root `CMakeLists.txt`
  before the first `add_subdirectory`, so the vendored dependencies inherit them too. A plugin is the
  one target that does not follow `CMAKE_LIBRARY_OUTPUT_DIRECTORY`: `atp_add_plugin` puts it in
  `ATP_PLUGIN_OUTPUT_DIRECTORY`, which is undefined out of tree, where the installed helper therefore
  behaves as it always did. That variable carries `$<CONFIG>` for a multi-config generator and must not
  for a single-config one — CMake appends a per-configuration subdirectory only to a value with no
  generator expression in it, and without the fork the plugins would land in `bin/plugins/Debug` while
  the hosts sat in `bin/Debug`. Two consequences follow. **No post-build step lays a plugin or a
  bridge package beside `atp_app` and `atp_studio`** — it would copy a file onto itself, and the
  failure of such a copy is silent (a stale bridge next to the studio, which the studio still considers
  its own). And `atp_deploy_qt()` deploys Qt **once per directory**: the first caller owns the
  deployment and later ones depend on it, because `atp_studio` and `atp_ui_tests` live in the same
  `bin/` and two concurrent windeployqt runs write the same DLLs. The exception is the seven test
  plugins in `tests/test_plugin/`: three of them are broken on purpose and several suites hand that
  directory to a scanner as a search directory, so the directory must contain nothing else.
- **The bridge tests count modules by directory rather than over the whole registry.** A bridge always
  scans its own language's subdirectory beside itself, and there — in the build tree and in the
  shipment alike — sit the sample pipeline's scripts (`averager.py`, `packer.py`, `scale.lua`), so the
  assumption "nothing is next to the bridge" holds nowhere, least of all in an unpacked release.
  Hence `SkipsABrokenScriptAndKeepsItsNeighbour` counts modules whose `source` lies in the scanned
  directory, and `LoadsWithNoModulesWhenNothingIsScanned` requires that with an empty path variable
  everything registered came from the bridge's own directory. Both wordings check the same claim and
  neither depends on what else sits beside the bridge.
- **CPack ships one package, not components** (`cmake/Packaging.cmake`, included **last**:
  `include(CPack)` freezes the variables and rules as they stand at the call). ZIP on Windows, DMG on
  macOS, TGZ on Linux. NSIS would give Windows a real installer but needs makensis on the build
  machine, whereas a ZIP needs nothing and unpacks anywhere — and the studio already starts from any
  directory, because Qt travels beside it.
- **The release is a separate workflow** (`.github/workflows/release.yml`), on a `v*` tag plus
  `workflow_dispatch`. The second is not for convenience: without it the first real test of deployment
  on a platform you cannot touch locally would happen on a tag. Publishing is a separate job after the
  matrix — three parallel jobs creating the same release is a race with a baffling failure.

## Licensing (`LICENSE`, `NOTICE`, `THIRD-PARTY-NOTICES.md`)

- **Apache-2.0 over the whole repository, under one licence**, SDK included. Splitting it — a
  permissive SDK plus copyleft on the hosts — would make sense if there were a product to protect;
  while there is none, the only measurable effect of a split is a two-colour repository that puts off
  exactly the plugin authors everything here was built for. The decision is reversible in one
  direction only: a sole rightsholder licenses **future** versions however they like, but someone
  else's contribution accepted under Apache-2.0 cannot be relicensed without its author's consent — so
  the question closes no later than the first external PR into `src/studio`.
- **Why Apache rather than MIT.** Section 3 is an explicit patent grant, and for this platform it is
  no formality: the SDK is header-only, so a plugin compiles these headers into itself across a
  versioned ABI, whereas under MIT one is left arguing whether the word "use" implies a patent licence.
  Section 5 makes a contribution arrive under the same terms, so no CLA is needed; section 6 keeps the
  project's name and icon. The price is incompatibility with GPLv2-only.
- **`NOTICE` is kept nearly empty on purpose.** §4(d) obliges every derivative shipment to reproduce
  its contents, and that is forever — whatever is put there, downstream carries for good. So it holds
  only the project's copyright and a pointer, while third-party licence texts live in
  `THIRD-PARTY-NOTICES.md`, which those licences require themselves (MIT for json, LGPL for Qt) rather
  than §4(d).
- **The notice names "The AniToolsPlatform Authors" rather than a person** — the Go and Chromium
  pattern. The list of rightsholders lives in `AUTHORS` and travels into the package beside `LICENSE`
  and `NOTICE`, so a change in who holds the rights needs no edit in every file and every artifact a
  user sees: the About dialog, the doxygen footer, the README. The downside is honest: a collective
  name blurs the chain of title, and on a sale of rights or a registration the identity is disclosed
  anyway — a collective name in the notice does not change that, all the more since the commit history
  and `.mailmap` name the author outright.
- **An SPDX line instead of file headers.** `// SPDX-License-Identifier: Apache-2.0` as the first line
  of every `.hpp`/`.cpp`: the full APPENDIX boilerplate is two hundred files of noise and endless
  fighting with clang-format, while no marking at all breaks §4(c) the moment a plugin author copies a
  header into their own project. This does not contradict the "no comments in `.cpp`" convention, by
  the same logic as `// NOLINT`: the line addresses a tool, not a reader. Non-C++ files (CMake,
  Markdown, configs) are deliberately left unmarked — strict REUSE would demand them too.
- **The notices are installed into both components**, `sdk` and `applications`, and into the root of
  the prefix. Each component is a shipment in its own right: json rides inside the applications'
  binaries and the Qt runtime sits beside the studio, while in the SDK a plugin author receives a copy
  of the headers. §4(a) requires the licence to reach whoever received the files, which makes this one
  of the few rules in `cmake/Install.cmake` that is not about the SDK. Not `CMAKE_INSTALL_DOCDIR`,
  because the shipment is a portable archive rather than a distribution package, and nobody looks in
  `share/doc/AniToolsPlatform` there.
- **`templates/plugin/` carries its own copy of the licence.** The directory is copied whole and
  outward, so §4(a) is served on the spot; its README says outright that the SPDX lines apply to the
  template's code and that authors licence their own plugin however they like, closed source included.
- **Qt is the one non-permissive link**, and it concerns `atp_studio` alone: LGPLv3, dynamic linking,
  libraries shipped beside the executable and replaceable by the recipient — exactly what that licence
  asks in return for the right not to open your own code. The §4(e) requirement (a prominent notice in
  the combined work) is served by the About dialog: `main_window::show_about` names Qt and its licence
  and shows **`qVersion()`** rather than `QT_VERSION_STR` — the version of the library actually loaded,
  because the point of the notice is precisely that it is replaceable. A build with
  `-DATP_BUILD_STUDIO=OFF` contains no Qt code at all, and none of the above applies to it.

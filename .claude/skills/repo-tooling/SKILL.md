---
name: repo-tooling
description: Use when running static analysis (clang-tidy), formatting sources (clang-format) or generating the Doxygen API reference for AniToolsPlatform — covers the CMake targets, the pinned clang-format version, the compilation-database requirement and the traps that make a run silently analyse nothing.
---

# Static analysis, formatting and API docs

## clang-tidy

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

Machine-specific invocation (the analyser's path, the environment it needs, the traps that produce a false
"clean") is in `CLAUDE.local.md`.

## clang-format

Formatting is `.clang-format`, enforced by the `clang-format` CI job. **The version is pinned** — the tool's
output moves between releases, so an unpinned one reformats files nobody touched — and the pin lives in two
places that must stay in step: `cmake/Format.cmake` and the workflow. Install exactly that version with
`pip install clang-format==22.1.8`; it works on any platform and needs no LLVM install. `cmake --build
<build-dir> --target format` reformats every tracked source in place (the target appears when a
clang-format is found, and configure reports it if its major differs from the pinned one).

**A file git does not track yet is not formatted.** The file list comes from `git ls-files`, so a newly created header or test is skipped until it is added — write, `git add`, *then* format, or the first thing the clang-format CI job sees is a diff in a file you thought you had formatted.

## API reference

`docs/Doxyfile` (run by hand from the repo root as `doxygen docs/Doxyfile`, or through the optional `docs`
CMake target, which appears only when `find_package(Doxygen)` succeeds — a machine without doxygen configures
unchanged). Output goes to `build/doxygen/html` (gitignored). `EXTRACT_ALL=NO`, so the run doubles as a
documentation-coverage check. Namespaces are documented in `docs/namespaces.dox` — doxygen drops an
undocumented namespace together with the free functions, enums and constants declared directly in it.

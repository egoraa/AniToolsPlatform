# Code style specification

The common rules for how AniToolsPlatform's code is written. Formatting comes from `.clang-format` at
the repository root; this document records what the formatter cannot check: naming, file structure and
idioms.

## Glossary

Two words that are easy to confuse, and on which the names of files, targets and config keys depend.

- **plugin** — the file the host loads (`.dll`/`.so`/`.dylib`) and that registers modules. It is what
  the `"plugins"` key in a config lists and what `atp_add_plugin()` declares. It must not be linked:
  the `MODULE` target type makes the attempt a configuration error rather than a broken convention.
- **module** — a unit of work inside a plugin: what implements `module_base`, what
  `"module": "counter"` names, and what a pipeline sees. A group is a module too
  (`group : module_base`).
- An example and a template are named `plugin_*` — **after the plugin through which the code reaches
  the host**, not after what the author types. In `templates/plugin` and `templates/plugin_rust` the
  author builds the plugin themselves; in `templates/plugin_python` their code rides inside
  `atp_python_bridge`, which the platform ships. Nothing can be loaded except through a plugin, so
  there is always something for the directory name to name.
- A module presents itself in the log **by its own name**, not by the name of the plugin that brought
  it.

## Language and project structure

- C++23, a header-only platform: all the code is headers under `include/atp/`, and the target
  `atp_platform` is an INTERFACE target.
- One class per header; the file name is the class name (`queued_input.hpp` → `queued_input`). The
  admissible exception is a tight "base plus template" pair kept together, when the base is needed by
  that template alone.
- The canonical include style is `<atp/...>` in angle brackets, between the library's own headers
  included.
- **An SDK subsystem is a folder plus an umbrella header named after it**: `io/` + `io.hpp`,
  `module/` + `module.hpp`, `hosting/` + `hosting.hpp`, `config/` + `config.hpp`,
  `plugin/` + `plugin.hpp`. An umbrella **carries no content** — it only declares the headers of its
  folder, and a new header of the subsystem is added to it.
  - The folder is the box, **the umbrella is the audience**, and choosing an umbrella is a decision
    about whom to show the header to: `module.hpp` is what a module author writes against,
    `hosting.hpp` is what only a host and a loader need on top of that.
  - `support/` **deliberately has no umbrella**: it is not a subsystem but utilities with no common
    audience, included by name.
  - The host runtime has the same shape one level out: `src/runtime/include/atp/runtime/` plus
    `<atp/runtime.hpp>`. Two headers stay out of that umbrella, both for one reason — a header that
    reaches every consumer must drag neither the socket stack nor a document library into a
    translation unit that only wanted a pipeline: `runtime/socket_platform.hpp` exists for
    `<winsock2.h>`, and `runtime/config_value_json.hpp` names `nlohmann::json` in its signatures.
- **The include guard is formed from the path and distinguishes what is exported from what is
  internal**: `ANITOOLSPLATFORM_<SEGMENTS_AFTER_atp>_HPP` for `include/atp/**` (the SDK somebody
  else's plugin sees), `ATP_<SEGMENTS_AFTER_atp>_HPP` for the subsystems inside `src/` (`runtime/`,
  `studio/`, `mcp/`). The boundary is exactly where the export runs, and it holds by observance rather
  than by a check.
- A header must be self-contained: `tests/CMakeLists.txt` builds one translation unit per header of
  `include/atp/**` and `src/runtime/include/atp/**`, including that header alone. This does not
  replace the "include what you use" rule — the guard catches only an include that nothing at all
  stands behind.
- In `CMakeLists.txt` the headers are picked up by `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`, so
  there is no need to list them by hand.

## Formatting

Set by `.clang-format`: the Chromium base, four-space indent, no tabs, a 120-column limit; the bodies
of `if` and of loops are always braced (`InsertBraces`), and the only short functions put on one line
are empty ones. Do not override it locally — the formatter settles the arguable cases.

## Naming

STL-like style, uniform across the codebase.

| Entity | Rule | Example |
|---|---|---|
| Files | snake_case | `module_factory_base.hpp` |
| Types, functions, variables | snake_case | `module_registry`, `try_pop` |
| Class members | snake_case with a trailing `_` | `value_`, `name_` |
| Type-erased/abstract bases | `_base` suffix | `io_base`, `module_factory_base` |
| A typed class over a base | the bare name, no prefix | `input<T>`, `module_factory<M>` |
| Tag types | `name_t` plus a lowercase `inline constexpr` instance | `safe`/`unsafe`, `throttled_t`/`throttled` |
| Type template parameters | PascalCase with a `T` prefix | `TBase`, `TItem`, `TInputs` |
| Non-type template parameters | PascalCase with no prefix | `Name`, `Version`, `N` |
| Non-throwing variants of a method | `try_` prefix | `try_pop` |
| Private virtuals behind an NVI wrapper | `do_` prefix | `do_connect` |
| gtest suite and test names | PascalCase (googletest's own convention) | `ModuleRegistry.AddAndCreate` |

The one exception to the `T` prefix is a bare `T` on a template parameterised by exactly one value type
(`input<T>`, `output<T>`, `property<T>`): an unqualified name reads better there than any `TValue`.
Everything else is qualified: `TEnum`, `TModule`, `TArg`. The rule is checked by `.clang-tidy`
(`readability-identifier-naming`) rather than by eye alone.

The "base plus template" pair shows the rule for distributing names: the `_base` suffix goes to the
abstract interface and the short name to the concrete class used more often
(`module_base`/`module<>`, `module_factory_base`/`module_factory<M>`).

## Comments

- All comments are written in English.
- Comments live in headers only, and only as Doxygen `///` blocks on a declaration: the first sentence
  is a brief description, followed as needed by `@param`, `@return` and `@throws`. That is how types,
  functions and the members of public structures are documented.
- There are no comments in `.cpp` files — neither inside function bodies nor above them; test files
  are `.cpp` too and the rule covers them. A closing `}  // namespace x` is layout rather than a
  comment and stays; directives such as `// NOLINT(...)` are instructions to a tool that must stand on
  their own line, and they stay too.
- A header never has a floating `//` block over a namespace or a group of functions: an explanation
  belongs to the declaration it is about, or Doxygen attaches it to nothing.
- A comment explains the intent and the reason ("why it is so"), not the mechanics ("what this line
  does"). A comment that retells the code is not written.
- Invariants and contracts that do not follow from the signatures (ownership, thread safety, lock
  acquisition order) are recorded in the `///` block on the declaration.
- A reason with no declaration to stand on (why the steps are in this order, what is wrong with the
  obvious variant) goes into `docs/architecture/`, not into a function body and not into a commit: a
  commit message is one line.

## Idioms and prohibitions

- A deliberate discard of a `[[nodiscard]]` result in a test is a `(void)` cast:
  `EXPECT_THROW((void)registry.create("missing"), std::runtime_error);`
- Non-throwing (`try_`) and throwing variants of an API come in a pair, in the spirit of `std::map`:
  `at()` throws where `find()` returns nullptr.
- Behaviour is extended through private `do_...` virtuals under a public non-virtual wrapper (NVI),
  not through public virtuals.
- Thread safety is a property of the instance, chosen with the `safe`/`unsafe` tag at construction,
  rather than by the type.

## Tests

- googletest; test files are `snake_case` with a `_tests.cpp` suffix (`module_registry_tests.cpp`). A
  file lives in the directory of the subsystem it covers (`tests/platform/`, `tests/runtime/`,
  `tests/mcp/`, `tests/studio/`, `tests/ui/`) and does not repeat the subsystem's prefix in its name:
  `runtime/config_loader_tests.cpp`. The sources are globbed, so a new file does not touch
  `tests/CMakeLists.txt`.
- Helpers shared between suites live in `tests/support/` and are included from the `tests/` root:
  `#include "support/pipeline_test_support.hpp"`.
- One suite per class or aspect; a test's name is a statement about behaviour
  (`RemoveLastVersionErasesName`).
- Expected exceptions use `EXPECT_THROW`; checking an error's text is done with `try`/`catch` and a
  `FAIL()` at the end of the `try`.

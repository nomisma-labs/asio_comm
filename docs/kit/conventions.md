# Conventions

These conventions apply to every Nomisma repo from its first commit. This file is the canonical
text; every repo's agent rules ([agents.md](agents.md)) link here.

## C++ identifiers

The style follows the C++ standard library. The one deliberate exception is **PascalCase template
parameters**, so compile-time parameters stand out. Macros are UPPER_CASE, and nothing else is.

| Entity | Style | Example |
|---|---|---|
| Namespace | `snake_case`, short; root `nom` | `nom`, `nom::detail`, `nom::config` |
| Class, struct, union, enum | `snake_case` | `assembly_factory`, `order_manager`, `side` |
| Enumerator | `snake_case` (no UPPER_CASE, no `k` prefix) | `side::buy`, `log_level::debug` |
| Type alias | `snake_case` | `using nodes = …` |
| Function, method | `snake_case`, a verb for actions | `pass_request()`, `place_limit()` |
| Getter / setter / predicate | noun, `set_…`, `is_…`/`has_…`/`can_…` | `now_ns()`, `set_limit()`, `is_ready()` |
| Variable, parameter | `snake_case` | `order_id` |
| Private/protected data member | `snake_case_` (trailing underscore) | `qty_` |
| Public member of an aggregate (events, configs) | `snake_case`, no suffix | `event.order_id` |
| `constexpr` / `const` global or static | `snake_case` | `max_centroids` |
| Type template parameter | `PascalCase`, no `T` suffix | `template <typename Traits, typename Node>` |
| Non-type template parameter | `PascalCase`, no `V` suffix | `template <side Side, size_t N>` |
| Template template parameter | `PascalCase` | `template <template <typename> class Node>` |
| Concept | `snake_case`, no `is_`/`has_` | `request_handler`, `handles_request` |
| Type trait | `is_…` / `has_…` | `has_on_request` |
| Trait shortcuts | `_v` only for `::value`, `_t` only for `::type` | `has_on_request_v`, `apply_custom_traits_t` |
| CRTP parameter | `Derived` | `template <typename Derived>` |
| Internal namespace | `detail` (not `impl`) | `nom::detail` |
| Macro | `UPPER_CASE` | `DEFINE_CONFIG` |

- **No `_t` suffix on type names.** `_t` and `_v` keep their `std` meanings.
- **Acronyms are ordinary words:** `wss_client`, `sbe_codec`, `gw_connection`.
- **Keyword collisions get a trailing `_`:** `tok::and_`, `tok::true_`.
- **Don't name a variable after its type** (`order order;`). Name it by its role: `pending`,
  `resting`, `fill_qty`.

## Files, directories, includes, modules, tests

All libraries use `src/` as their source root, regardless of architectural layer.
`<component>` identifies the library and normally matches `<repo>` for a repository's main
library. A repository can contain several components under `src/`.

| Item | Rule | Example |
|---|---|---|
| Repo, directory and file names | `snake_case` | `cmake_template`, `custom_traits.cppm` |
| Extensions | `.cppm` module interface or partition, `.cpp` implementation unit or ordinary source, `.h` macro or dependency header, `_test.cxx` test | `relay_test.cxx` |
| Library sources | `src/<component>/…` | `src/assembly/relay.cppm`, `src/asio_comm/timer.cppm` |
| Imports | Nomisma library API through its primary module | `import nom.assembly;` |
| Includes | Remaining public headers use `<component/file.h>` without the `src/` prefix; name clashes are handled case by case | `#include <example/macros.h>` |
| One concept per file | The file is named after it | `relay.cppm`, `constructible.cppm` |
| Primary module | Usually `nom.<component>`, file `src/<component>/<component>.cppm` | `export module nom.assembly;` |
| Module partition | `nom.<component>:<part>`, file `src/<component>/<part>.cppm` | `export module nom.assembly:relay;` |
| Unit tests | `tests/unit/<suite>/<file>_test.cxx`, or inline next to the code at `<code dir>/tests/<suite>/<file>_test.cxx`; created with `nom_c2test()` and labeled `unit`. One executable per suite, target `<project>_<suite>`; test-only headers are `<name>_test.h`. | `tests/unit/assembly/relay_test.cxx` |
| Integration tests | `tests/integration/<scenario>/`, with C++ sources named `<file>_test.cxx`, labeled `integration`; library usage tests build a separate CMake project against the library. | `tests/integration/library_usage/` |
| Apps | `apps/<app>/main.cpp`, with optional `apps/<app>/config/` staged into the build tree | `apps/relay_handler_contract/` |
| Repository documentation | `docs/<repo>/`, alongside shared `docs/kit/`; no layer subdirectories | `docs/asio_comm/` |
| Repository tools | `tools/<repo>/`, alongside shared `tools/kit/`; no layer subdirectories | `tools/json_config/` |

Use `*.cpp` to select regular C++ sources and `*_test.cxx` to select C++ test sources.
This applies to unit, inline, and integration tests. Test-only headers use `_test.h`.
Tests default to enabled only when the project is top level and are never installed.

Our C++ library declarations live in named module interfaces and partitions. Apps,
unit tests and integration consumers import the primary module. Modules do not dictate the C++
namespace hierarchy:
`nom.assembly` can export names directly in `nom`.

Macros stay in headers. Third-party dependencies can be included in a global module fragment,
hidden behind a module API, or consumed through their headers when needed. Use `import std;`
in each file that uses standard-library declarations. Keep macro-bearing standard headers
(such as `<cassert>`) and third-party includes before imports, in the global module fragment
for module units. Enable standard-library module detection before `project()` and opt each
compiled consumer target in with `IMPORT_STD` in its kit helper call. For targets created
outside the kit, use `nom_enable_std_modules(target)` after creating the target.
See [build.md](build.md#modules) for the supported toolchain and consumer setup.

## CMake names

Target names are global in a superbuild, so every target carries its project's name.

| Item | Rule | Example |
|---|---|---|
| Project | The repo name | `project(assembly …)` |
| Main library target | The project name, exported as `nom::<project>` | `assembly` / `nom::assembly` |
| Other targets | `<project>_<name>` | `assembly_tests`, `config_generate` |
| Executables | `nom_app(<app>)` creates target `<app>` and binary `<app>`; choose a name unique across the whole build | `relay_handler_contract` |
| Options and cache variables | `<PROJECT>_<NAME>`; tests and apps default to ON only when the project is top level | `ASSEMBLY_BUILD_TESTS` |
| Kit functions | `nom_` prefix; use short names for target helpers | `nom_lib()`, `nom_app()`, `nom_c2test()` |
| Local variables | `lower_snake_case` | `set(module_files …)` |

## Enforcement

| What | Tool | Fix |
|---|---|---|
| Formatting | `.clang-format`; `tools/kit/checks/format.sh` | `tools/kit/checks/format.sh --fix` |
| Naming | `.clang-tidy` (readability-identifier-naming); `tools/kit/checks/naming.sh` | rename by hand |
| License header | `python3 tools/kit/checks/headers.py` | add the header below |

The pre-commit hook runs the formatting and header checks on each commit's staged files
([build.md](build.md#git-hooks)).

Every source file (C++, CMake, Python, shell, YAML, Dockerfile) starts with, after an optional shebang:

```cpp
// SPDX-FileCopyrightText: 2026 Alexey Tkachenko
// SPDX-License-Identifier: MIT
```

using `#` instead of `//` where that is the comment syntax. This example is for an MIT file.
New project files use the copyright and license from `project_license.json`; copied kit and
scaffold files retain MIT. The project terms are in `LICENSE`, and the kit's MIT notice is in
`cmake/kit/LICENSE`. The checker verifies the selected license, including the staged policy
when committing. See [license selection](new_repo.md#licensing).

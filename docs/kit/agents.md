# Agent rules

The rules every Nomisma repo shares. They come from `cmake_template`, which owns this file; each
repo's `AGENTS.md` imports it and adds the repo's own rules.

## Build and test

- Run `./bootstrap.sh` for fresh validation builds, tests and quality checks, or `./bootstrap.sh <preset>` for one preset.
  Presets are discovered through CMake; the default selects all matching configure/build/test names except `release`.
  It clears selected build directories and previous bootstrap logs; `--help` lists the discovered presets.
  Python 3.10+ is required; `tools/kit/checks/python.py` validates syntax and imports before builds.
- First checkout: `git submodule update --init --recursive`.
- Configure once with `cmake --preset dev`; then, in `build/dev`, `ninja` builds and
  `ctest --output-on-failure` tests. Binaries land in `build/<preset>/bin/`.
- `cmake --workflow --preset dev` does all three. Other presets: `complete`, `clang`, `asan`, `release`, `ci`.
  A `dev` build leaves out the tests labeled `integration` and `kit`; the others run every test, in
  parallel through their presets, so `cmake --workflow --preset complete` checks a change fully.
  `complete` is Debug with every app; `nom_app(... BUILD_FOR dev ci)` limits normal build/install participation.
  Details: [build.md](build.md).
- Native Python packaging is opt-in: use the pinned environment and wheel build commands in
  [build.md](build.md#native-python-wheels). Ordinary C++ builds need no packaging tools.

## Layout

- `src/<component>/`: libraries in every architectural layer, declared with `nom_lib()`.
  Public APIs use named modules, with `.cppm` interfaces/partitions and `.cpp` implementation
  units. The main component normally shares its repository name, including module `nom.<repo>`.
  The source root is relative to the current repository, independently of its workspace location.
- `apps/<app>/`: executables; optional `config/` is collected into `build/<preset>/config/<app>/` and installed with the app.
- `apps/sandbox/`: experiments, normally `BUILD_FOR complete`; also buildable explicitly in other presets.
  They are git-ignored and stay in the clone; don't commit one unless asked.
- `tests/unit/<suite>/` or inline `<code dir>/tests/<suite>/`: Catch2 unit tests, labeled `unit`.
  Catch2 is found at the project root when tests are enabled, before code directories.
- `tests/integration/`: library usage and installation checks, labeled `integration`.
- `external/`: git submodules (third-party code pinned to release tags, and our own repos).
- `cmake/kit/`: the shared CMake kit and presets ([cmake/kit/README.md](../../cmake/kit/README.md)).
  Include `cmake/kit/std_modules.cmake` before the first `project()` enabling CXX when using
  `import std;`, then `cmake/kit/nom_kit.cmake` after `project()`.
  `CMakePresets.json` includes `cmake/kit/presets.json`.
- `tools/kit/`: the shared checks and scripts ([tools/kit/README.md](../../tools/kit/README.md)).
- `docs/kit/`: these shared docs ([docs/kit/README.md](README.md)).
- A repo's own CMake files go in `cmake/<repo>/`, its tools in `tools/<repo>/`, and its docs
  in `docs/<repo>/`, outside the kits. Docs and tools have no additional layer directories.

## Adding code

Follow [howto.md](howto.md): a library, a module, an app, a unit test suite, library usage integration tests,
a Python package, a dependency, an experiment.

## Rules

- Follow [conventions.md](conventions.md): `snake_case` everywhere, `PascalCase` template
  parameters, UPPER_CASE only for macros. `tools/kit/checks/naming.sh` enforces the naming.
- C++ test sources use `*_test.cxx` in both unit and integration tests; regular sources use `.cpp`.
- Every source file starts with the SPDX header; `python3 tools/kit/checks/headers.py` checks it.
- Format with `tools/kit/checks/format.sh --fix`.
- Commits run the git hooks in `tools/kit/hooks/` once bootstrap has enabled them. Never skip them
  with `--no-verify`: fix what they report, stage the fixes and commit again. Commit messages carry
  no `Co-Authored-By` trailers ([build.md](build.md#git-hooks)).
- Add targets with the kit (`nom_lib`, `nom_app`, `nom_python_module`, `nom_c2test`), not raw CMake.
- Public helpers respect the calling project's `nom_*` options. Keep guards around optional
  dependency discovery, direct `add_test()` calls, and commands modifying skipped targets or tests;
  a helper call alone needs no repeated guard ([build.md](build.md#the-cmake-kit)).
- Python packaging mechanisms belong in the template; derived repos declare package metadata,
  bindings, dependencies and runtime libraries through the shared facilities.
- Template-owned paths (`bootstrap.sh`, `cmake/kit/`, `tools/kit/`, `docs/kit/`, `.clang-format`,
  `.clang-tidy`, `.clangd`, `.editorconfig`, `.github/workflows/ci.yml`) change only in
  cmake_template and reach other repos through `tools/kit/sync_template.py`. Don't edit them in
  a derived repo.
- Define our C++ library APIs in named modules; apps and tests import them.
  Keep macros in headers, and use wrappers or headers at third-party boundaries.
  Use `import std;` for standard-library declarations. Put macro-bearing and third-party
  `#include`s before imports (in a global module fragment for module units). Enable each
  consuming compiled target with `IMPORT_STD` in its kit helper call (`nom_lib`, `nom_app`,
  `nom_c2test`, or `nom_python_module`). Helpers handle disabled targets internally. Use
  `nom_enable_std_modules(target)` directly for targets created outside the kit, guarding
  optional targets when needed. Source/package consumers must enable detection before their
  own `project()` too ([build.md](build.md#modules)).
- The version lives only in `project()`; releases follow [build.md](build.md#versioning).
- When you change behaviour that a doc describes, update the doc in the same change.

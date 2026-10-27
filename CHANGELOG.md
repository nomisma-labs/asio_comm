# Changelog

Notable changes to this repo. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org); how to release:
[docs/kit/build.md](docs/kit/build.md#versioning).
A release's `### Upgrade` section lists the steps derived repos take by hand; `tools/kit/sync_template.py` prints them. <!-- template-only -->

## [Unreleased]

## [0.11.1] - 2026-10-27

### Changed

- `nom_app(<name>)` creates the target `<name>` directly, matching `nom_lib()`. Callers can
  apply target properties without reconstructing a project prefix; binary names stay the same.
- Advance the shared kit identifier to `0.11.1` so composed repositories report older copies
  with different app target naming.
- Clarify dependency ownership in derived repositories and the image-first toolchain push order.

### Upgrade

- Replace references to app targets `<project>_<name>` with `<name>` in target settings,
  generator expressions, test commands and explicit build commands. If enabled apps or other
  targets share a name in one build, choose distinct names in their `nom_app()` calls; use
  `OUTPUT_NAME` when the binary should retain its previous name.

## [0.11.0] - 2026-10-17

### Changed

- Clang 22 for the `clang` and `asan` presets, the checks (`clang-format-22`, `clang-tidy-22`,
  `run-clang-tidy-22`) and the CI image, instead of Clang 19. clang-format 22 formats our code
  exactly as 19 did. Clang 22 builds modules with reduced BMIs by default, and since Clang 21
  `-fsanitize=undefined` no longer includes `vptr`. Validation:
  [docs/lab/toolchain.md](docs/lab/toolchain.md#clang-22).

### Upgrade

- Install the Clang 22 tools on each machine:
  `sudo apt install clang-22 clang-tools-22 clang-tidy-22 clang-format-22 clangd-22`.
- Point the editor's clangd at `/usr/bin/clangd-22`, which reads the `clang` preset's database.
- Run `./bootstrap.sh` once: build folders configured with Clang 19 need a fresh start.

## [0.10.4] - 2026-10-17

### Changed

- Catch2 3.16.1: `nom_find_catch2()` accepts an installed Catch2 3.16.1 or newer within major
  version 3, so Ubuntu's 3.7.1 no longer counts; otherwise each build tree downloads and builds
  3.16.1. [docs/kit/build.md](docs/kit/build.md#toolchain) installs it once per machine, and the CI
  image builds it. Since Catch2 3.9, a suite's test cases run in random order.
- cmake_template's toolchain report shows which Catch2 builds use, and compares the CI image's
  Catch2 with the kit's.
- Advance the shared kit identifier to `0.10.4` so composed repositories report older copies of
  `nom_find_catch2()`.

### Upgrade

- Install Catch2 3.16.1 on each machine ([docs/kit/build.md](docs/kit/build.md#toolchain));
  without it every build tree downloads and builds it.
- A suite whose test cases depend on each other's order now fails intermittently. Fix the
  dependency; `--order decl` runs the cases in declaration order meanwhile.

## [0.10.3] - 2026-10-17

### Changed

- CMake 4.4.4 from Kitware's apt repository, locally and in the CI image, which pins its version
  (`CMAKE_VERSION` in `docker/ci/Dockerfile`). [docs/kit/build.md](docs/kit/build.md#toolchain)
  shows how to add the repository to a machine.
- cmake_template's toolchain report notes when the CI image's CMake and the installed one differ.

## [0.10.2] - 2026-10-17

### Added

- Every repo's bootstrap reports git submodules recorded at an older release than their upstream's
  newest, as a `NOTE`: `tools/kit/checks/dependency_updates.py` compares each submodule's release
  tag with its remote's. An unreachable remote, or a recorded commit that isn't a release, is
  reported unchecked, as a `SKIP` when nothing could be checked.

### Changed

- cmake_template's `tools/toolchain_updates.py` no longer covers submodules, and shares the
  report code of the new check.

## [0.10.1] - 2026-10-16

### Fixed

- The 0.10.0 upgrade step about `dev` builds also covers `-R`: a pattern naming `integration` or
  `kit` tests finds nothing in a `dev` build either.

## [0.10.0] - 2026-10-16

### Changed

- Tests run in parallel: the test presets set `CTEST_PARALLEL_LEVEL` empty, for one test per CPU,
  and `-j1` runs them one at a time. cmake_template's `complete` tests take 5 s instead of 20 s.
- A `dev` build runs the fast tests: the `dev` preset sets `NOM_SKIP_TEST_LABELS` to
  `integration;kit`, whose tests build other projects, so `ctest` in `build/dev` leaves them out
  with or without a preset. `complete` and the other presets run every test
  ([docs/kit/build.md](docs/kit/build.md#tests)).
- Bootstrap names how many tests of each label a preset runs, and records a result that needs no
  fix as `NOTE`. The presets section of `docs/kit/build.md` says what each preset is for and why
  `ci` optimizes.
- The kit's own tests moved from `tests/integration/` to `tests/kit/`, labeled `kit`.
  `boost_package` builds its package and source consumers at the same time.
- Advance the shared kit identifier to `0.10.0` so composed repositories report older copies of
  the test selection.

### Added

- `nom_c2test()` labels its test `unit`. `LABELS` replaces that label list, and `PROPERTIES` sets
  other test properties, so callers no longer spell `<project>_<suite>`.
- The `NOM_SKIP_TEST_LABELS` cache variable: the kit lists the tests carrying those labels in the
  build tree's `CTestCustom.cmake`, which every ctest run there reads.
- `tools/toolchain_updates.py`, run by bootstrap in cmake_template only: newer `g++-N` and
  `clang-N` packages than the presets name, a newer Boost, apt upgrades for the installed tools,
  and newer releases of the submodules, the Catch2 download and CMake. How to adopt one:
  [docs/lab/toolchain.md](docs/lab/toolchain.md#keeping-the-toolchain-current).

### Fixed

- The git hooks test formats its sample code with the scratch repo's `.clang-format`, so it
  passes with a build directory outside the source tree.
- `tools/init_project.py` removes every kit test; new repos kept an unregistered copy of the git
  hooks test.

### Upgrade

- Run `cmake --preset dev` once, or `./bootstrap.sh`, so an existing `build/dev` picks up
  `NOM_SKIP_TEST_LABELS`; until then it runs every test.
- The `set_tests_properties(<project>_<suite> PROPERTIES LABELS unit)` line after each
  `nom_c2test()` call can go, with any `if(nom_build_tests)` guard only it needed: the label is
  now the default. Pass other test properties through `PROPERTIES`.
- A `dev` build no longer has the `integration` and `kit` tests, so commands that select them
  there find nothing: `ctest --preset dev -L integration`, or `-R` with a pattern naming them. A
  pattern that also matches other tests runs only those. Where the repo's own docs or scripts run
  integration tests through `dev`, use `--preset complete` instead.

## [0.9.0] - 2026-10-14

### Changed

- Public CMake helpers respect the calling project's options before validating
  arguments or creating targets, tests, or installation rules: `nom_app()` checks
  `nom_build_apps`, `nom_python_module()` checks `nom_build_python`, test helpers
  check `nom_build_tests`, and `nom_install_package()` checks `nom_install`.
- `nom_library_usage_tests()` retains source-consumption coverage when installation
  is disabled; its installation fixture and package-consumption test require
  `nom_install`. Python wheel installation through `nom_python` remains independent
  of native installation.
- Advance the shared kit identifier to `0.9.0` so composed repositories report
  older copies of the helper behavior.

### Fixed

- Explicitly enable Python in the imported-wheel fixture and its rejection checks.

### Added

- Integration coverage for disabled helpers, independent host/dependency options,
  and library tests with installation enabled or disabled.

### Upgrade

- Call `nom_project_options()` after each `project()` before using kit helpers.
  Helpers now enforce the resulting project-local flags internally.
- Remove caller guards only when the helper handles the complete operation. Keep
  guards for dependency discovery, direct `add_test()` calls, and operations on
  targets or tests that a disabled helper does not create.
- Remove `nom_install` guards around `nom_library_usage_tests()` to retain its
  source-consumption test. Explicitly enable `<PROJECT>_BUILD_PYTHON` in Python
  package projects; their `nom_python` component can still install with
  `<PROJECT>_INSTALL=OFF`.

## [0.8.4] - 2026-10-13

### Changed

- The workflows use `actions/checkout@v7`, `docker/login-action@v4` and
  `docker/build-push-action@v7`, which run on Node.js 24. GitHub deprecated Node.js 20 for
  actions, and runs warned about the old versions.

## [0.8.3] - 2026-10-13

### Added

- `nom_find_boost()`: the system Boost 1.90 or newer with `Boost::program_options`,
  `Boost::filesystem` and `Boost::iostreams`. The top-level `CMakeLists.txt` calls it, so new
  repos start with Boost. The toolchain lists the Ubuntu packages; the CI image already has them.
- `boost_example`, an example app that parses its options with Boost.ProgramOptions, checks and
  creates paths with Boost.Filesystem, and reads and writes gzip-compressed settings with
  Boost.Iostreams. Integration tests round-trip its settings, and build package and source
  consumers of a static library that links Boost privately.

### Changed

- `nom_install_package()` finds each dependency with the version and components its kit service
  found it with, so consumers of a package that lists `Boost` in `DEPENDENCIES` get the Boost
  targets its exported link interface names.
- Advance the shared kit identifier to `0.8.3` so mixed copies report the new Boost service.

### Upgrade

- To use Boost, call `nom_find_boost()` in the top-level `CMakeLists.txt` after
  `nom_project_options()` and before `add_subdirectory(core)`, as new repos do, and list `Boost`
  in `nom_install_package(DEPENDENCIES …)` when exported targets link it. Install the Boost
  packages from [docs/kit/build.md](docs/kit/build.md#toolchain) where they are missing.

## [0.8.2] - 2026-10-12

### Added

- `COMPILE_OPTIONS` for `nom_app()` and `nom_lib()`, accepting compiler flags such as
  `-DOPTION=value`. Options are private to applications and compiled libraries; interface
  libraries pass them to consumers. Arguments are forwarded as a CMake list.
- Build-and-run coverage for app options, static and shared library options, interface
  propagation, and unquoted macro values converted to strings in C++.

### Changed

- Advance the shared kit identifier to `0.8.2` so mixed copies report the new helper support.

## [0.8.1] - 2026-10-10

### Fixed

- Sandbox experiments are git-ignored, as intended: `.gitignore` ignores every folder in
  `apps/sandbox/`, so experiments stay in the clone, out of `git status` and out of the checks that
  read untracked files. `apps/sandbox/CMakeLists.txt` stays tracked and still builds whichever
  experiments exist; `git add -f` commits one that is worth sharing.

### Upgrade

- Add the sandbox rule to the repo's `.gitignore`, which the sync doesn't touch:
  ```gitignore
  # Sandbox experiments stay in the clone; apps/sandbox/CMakeLists.txt builds whichever exist.
  /apps/sandbox/*/
  ```

## [0.8.0] - 2026-10-09

### Added

- Git hooks in `tools/kit/hooks/`, which `./bootstrap.sh` enables for each clone by pointing
  `core.hooksPath` at them, unless the setting already names other hooks. `pre-commit` checks
  the staged copy of each staged file for whitespace errors, formatting and SPDX headers;
  `commit-msg` rejects `Co-Authored-By` trailers ([docs/kit/build.md](docs/kit/build.md#git-hooks)).
- `tools/kit/checks/format.sh --staged` and `tools/kit/checks/headers.py --staged` check the
  staged copy of each staged file, as the pre-commit hook does. The header check covers the hooks.

### Upgrade

- Run `./bootstrap.sh` once in each clone to enable the hooks, or enable them directly with
  `git config core.hooksPath tools/kit/hooks`.

## [0.7.0] - 2026-10-08

### Changed

- A repo's own CMake files go in `cmake/<repo>/`; the kit stays the shared set every repo gets.
- Advance the shared kit identifier to `0.7.0` so mixed copies report that `nom_find_metal()` is gone.

### Removed

- Metal leaves the shared dependency baseline; assembly, its only user, now carries it. The
  template drops the `external/metal` submodule, and the kit drops `nom_find_metal()` and the
  `nom_metal` package (`cmake/kit/deps/metal.cmake` and `metal_config.cmake.in`), so new repos
  start without Metal. The example's `type_at_t` uses `std::tuple_element_t` instead.

### Upgrade

- Move CMake files of the repo's own from directly under `cmake/` into `cmake/<repo>/`.
- A repo that doesn't use Metal: delete the `nom_find_metal()` call from the top-level
  `CMakeLists.txt`, remove the submodule with `git rm external/metal` (and `.gitmodules`, if that
  leaves it empty), and drop Metal from the repo's own docs.
- A repo that uses Metal keeps the submodule and takes the helper over before committing the
  sync, while `HEAD` still has it:
  ```bash
  mkdir -p cmake/<repo>
  git show HEAD:cmake/kit/deps/metal.cmake > cmake/<repo>/metal.cmake
  git show HEAD:cmake/kit/deps/metal_config.cmake.in > cmake/<repo>/metal_config.cmake.in
  ```
  Then add `include(cmake/<repo>/metal.cmake)` before the `nom_find_metal()` call. The
  `nom::metal` target and the `nom_metal` package keep their names.

## [0.6.0] - 2026-10-05

### Changed

- The shared docs move to `docs/kit/`, next to `cmake/kit/` and `tools/kit/`: `build.md`,
  `conventions.md`, `howto.md` and `new_repo.md`, with an index in `docs/kit/README.md`. A repo
  keeps its own docs in `docs/`, outside the kit.
- `AGENTS.md` splits in two. The rules every repo shares move to the template-owned
  `docs/kit/agents.md`; a repo's `AGENTS.md` keeps its description and its own rules, and imports
  the shared file with `@docs/kit/agents.md`.
- The shared CMake presets move to `cmake/kit/presets.json`. A repo's `CMakePresets.json`
  includes that file and holds only presets of its own.
- `cmake/README.md` moves into the kit as `cmake/kit/README.md`.
- `README.md` drops its bootstrap and manual build sections for a short build section; their
  details are in `docs/kit/build.md`.

### Upgrade

- Shorten `AGENTS.md` to the repo's description, the line
  `Read and follow the rules every Nomisma repo shares: @docs/kit/agents.md`, and the repo's own
  rules; `docs/kit/agents.md` now has the shared sections.
- Replace `CMakePresets.json` with cmake_template's, which includes `cmake/kit/presets.json`, and
  keep only presets of the repo's own in it, under names the kit doesn't use.
- Delete `cmake/README.md` unless it describes the repo's own CMake files; the kit's README is now
  `cmake/kit/README.md`.
- Point `docs/README.md` at `docs/kit/README.md`, and replace the bootstrap and manual build
  sections of `README.md` with a short build section that links `docs/kit/build.md`.
- Update the links the sync lists, and the `docs/build.md` link at the top of `CHANGELOG.md`: the
  shared docs moved from `docs/` to `docs/kit/`.

## [0.5.0] - 2026-10-05

### Changed

- Template-owned tools move to `tools/kit/`, which repos receive as a whole; a repo keeps its own
  tools in `tools/`, outside the kit. The checks drop their `check_` prefix and live in
  `tools/kit/checks/` (`tools/check_format.sh` is now `tools/kit/checks/format.sh`), and
  `tools/kit/README.md` lists every tool.
- `tools/kit/sync_template.py` replaces template-owned files instead of merging differences into
  them. It syncs to the newest release by default and runs the target release's copy of itself.
  When a template-owned file was edited in the repo it changes nothing and lists the file, and
  `--overwrite` takes the template's version. It stages `.template_ref` with the rest, then lists
  the repo's own files that mention moved or removed template paths, repo files inside the kit's
  folders, and the upgrade steps of the releases in between.

### Added

- `### Upgrade` sections in this changelog for the steps derived repos take by hand.
- An integration test of the sync on a scratch template and scratch repos.

### Upgrade

- A repo created before 0.5.0 has the old `tools/sync_template.py`, which can't sync to this
  release. Run the new script once from a `cmake_template` checkout at v0.5.0 or later, next to
  the repo: `python3 ../cmake_template/tools/kit/sync_template.py`. Later syncs run the repo's
  own `tools/kit/sync_template.py`.
- Update the repo's own files that the sync lists: tool paths moved from `tools/` to `tools/kit/`.

## [0.4.1] - 2026-10-05

### Fixed

- Keep template-only content out of template-owned files, which reach every derived repo: the
  example wheel job moves from `ci.yml` to the template-only `ci-template` workflow, the example's
  wheel checks move from `docs/build.md` to the README, and `bootstrap.sh` no longer looks for
  `tools/init_project.py`. A `ci-template` job fails when template-owned files mention
  template-only names.
- `docs/build.md` describes the CI that runs: the `ci` workflow preset and the first four checks.

## [0.4.0] - 2026-10-04

### Changed

- Organize the CMake kit into client helpers (`api/`), dependency services (`deps/`), internal
  utilities and package templates. Keep `nom_kit.cmake` and existing helper names as the stable
  interface; document the layout and common helper usage in `cmake/README.md`.
- Advance the shared kit identifier to `0.4.0`.

### Added

- Optional Linux Python wheels through scikit-build-core, with shared `nom_python_module()`
  installation rules and pinned packaging tools kept separate from ordinary C++ builds.
- An example Python package using the native module API and a bundled shared library, with
  installed-wheel checks for source-built and imported C++ libraries. Repository initialization
  removes the example while retaining reusable packaging support.

## [0.3.0] - 2026-10-03

### Added

- Metal v2.1.4 in the common dependency baseline. Generated repositories retain the pinned
  submodule and use `nom_find_metal()` and `nom::metal` from the shared kit. Installed packages
  share the `nom_metal` package, headers and license.

### Changed

- Include naming checks in bootstrap runs that select `clang`.
- Define Nomisma library APIs in native named modules. Keep headers for macros and
  third-party integration.
- Convert `nom.example` to module interfaces, partitions and implementation units. The `hello`
  app, unit tests and both source and installed-package consumers import the same API.
- Update the shared recipes and conventions so new repositories start with modules.
- Advance the shared kit identifier to `0.3.0`.

## [0.2.0] - 2026-10-03

### Added

- Explicit `STATIC` and `SHARED` linkage options for `nom_lib()`, allowing mixed library
  linkage without changing the global `BUILD_SHARED_LIBS` setting.
- Relocatable Unix installations: installed applications and shared libraries find packaged
  libraries relative to their own location, with an integration test that moves the install prefix.

### Changed

- GCC-based presets use `g++-16`, with GCC 16 visible in their display names.
- The CI image and hosted runners use Ubuntu 26.04. The image uses Ubuntu's compiler and build-tool
  packages instead of the toolchain PPA and Kitware repository.
- The CI image includes the Boost, OpenSSL, fmt, zlib and Cap'n Proto development dependencies;
  the removed Boost.System stub package is no longer requested.
- Advance the shared kit identifier to `0.2.0` so mixed copies report the updated kit's version.

## [0.1.0] - 2026-10-02

### Fixed

- Leave `.template_ref` blank in the unhosted template and require a resolvable template
  commit before initializing a new repository.

### Added

- Use `*_test.cxx` for C++ unit and integration test sources so they can be selected separately
  from regular `*.cpp` sources; include `.cxx` in formatting and SPDX checks.

- Python environment check in bootstrap and CI: Python 3.10+, repository script syntax, and
  imported modules. Python-dependent steps are skipped on failure.

- Optional preset argument for `bootstrap.sh`, with available presets discovered through CMake
  and listed in `--help`. The default selects all matching configure/build/test names except
  `release`; cleanup is limited to the selected builds. CMake checks each preset's toolchain.

- `nom_app(... BUILD_FOR ...)` selects normal build/install participation; `any` is the default.
  The `complete` preset is a Debug build with every app, including sandbox experiments;
  bootstrap covers it. Excluded apps remain explicitly buildable.

- Discover Catch2 and enable testing at the project root before code directories, supporting
  both top-level and inline unit test suites.

- Separate `tests/unit/` and `tests/integration/`, with CTest labels for selecting either group;
  rename the library usage integration helper to `nom_library_usage_tests()`.

- `nom_app()` collects an adjacent `config/` directory into the build tree and installs it with the app executable.
- Name target helpers `nom_lib`, `nom_app` and `nom_c2test`, keeping the testing framework explicit.
- `bootstrap.sh`: clears prior validation builds/logs, initializes dependencies and runs fresh
  preset builds/tests and quality checks, with per-step
  results, logs, a final summary and next steps; included in template synchronization.
- The template: the CMake kit, presets, checks, CI and the CI image, agent files, and the recipes
  for adding code (`docs/howto.md`).
- The example code (library `example`, app `hello`, their tests, the library usage test project) and the lab.
- `tools/init_project.py`, which turns a copy of the template into an empty repo, and
  `tools/sync_template.py`, which keeps derived repos in step with the template.

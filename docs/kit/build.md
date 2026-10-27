# Build

## Commands

Run `./bootstrap.sh` for dependency initialization, fresh builds/tests and quality checks.
Bootstrap discovers enabled configure/build/test presets with matching names through CMake,
including included files and `CMakeUserPresets.json`; it uses the repo's `build/<preset>/` layout.
The default run selects every discovered preset except `release`. Use `./bootstrap.sh <preset>`
to select one explicitly; `--help` lists the discovered names. For a personal preset, supply
matching build and test entries that reference its configure preset. CMake checks the compiler
and generator from the preset rather than bootstrap guessing them from its name.
Each run deletes only the selected build directories and previous `build/bootstrap/` logs.
A `dev` build runs the fast tests; the other presets run every test ([Tests](#tests)).
Python compatibility/imports, headers and formatting are always checked; naming is included when
`clang` is selected. A failed check returns a nonzero exit code. Invalid arguments
are rejected before cleanup. Bootstrap also enables the [git hooks](#git-hooks) for the clone.
Bootstrap also reports git submodules recorded at an older release than their upstream's newest
([Dependencies](#dependencies)). In cmake_template, a last step runs `tools/toolchain_updates.py`,
which reports compilers, Catch2 and CMake newer than the repo uses; other repos don't have it.

Each step reports `PASS`, `FAIL`, `SKIP` or `NOTE`, then a summary and next steps; independent
checks continue after a failure, and a note needs no fix to pass. Only the current run's logs are
kept under `build/bootstrap/`. A
new repo reports its tests as skipped until it has some. Submodules at unexpected commits are
reported, not reset. Initializing dependencies, or CMake's fallback download, may need network
access. Builds use one job fewer than the CPU cores, at least one;
`CMAKE_BUILD_PARALLEL_LEVEL=4 ./bootstrap.sh` overrides that. Bootstrap also works when started
from another directory.

For everyday incremental work, use the commands below.

```bash
git submodule update --init --recursive   # once, after cloning
cmake --preset dev                        # once: configure into build/dev
cd build/dev
ninja                                     # build; reruns cmake itself when a CMakeLists.txt changes
ctest --output-on-failure                 # the fast tests; -R <regex> picks tests by name
bin/<app>                                 # run an app
```

Other presets work the same way in `build/<preset>`; `build/complete` has every test, and
`ctest -j` runs them in parallel there, as the test presets do. `cmake --workflow --preset <preset>`
configures, builds and tests in one command; CI uses it.
After changing the compiler, run `./bootstrap.sh` (or `./bootstrap.sh release` for Release)
to clear both the main build and the nested integration-test builds that cache the compiler.

## Toolchain

| Tool | Version | Used for |
|---|---|---|
| Ubuntu | 26.04 LTS | development and CI operating system |
| GCC | 16 (`g++-16`); installed compiler reports 16.0.1, experimental snapshot 20260322, package `16-20260322-1ubuntu1` | the main compiler |
| Clang tools | 22 (`clang++-22`, `clangd-22`, `clang-tidy-22`, `clang-scan-deps-22`, `clang-format-22`); Ubuntu packages `clang-22`, `clang-tools-22`, `clang-tidy-22`, `clang-format-22`, `clangd-22` | second compiler, sanitizers, IDE, checks |
| CMake | 3.30 or newer; 4.4.4 from Kitware's apt repository, locally and in CI | build |
| Ninja | 1.11 or newer | generator (Makefiles can't build modules) |
| Python | 3.10 or newer; 3.14.4 locally | repository scripts use the standard library; optional wheel builds use the separate environment below |
| Catch2 | 3.16.1, built from source into `/usr/local`, locally and in CI | tests |
| Boost | 1.90 or newer; Ubuntu packages `libboost-program-options-dev`, `libboost-filesystem-dev`, `libboost-iostreams-dev` (1.90.0) | program options, filesystem and iostreams in every repo |

The Clang packages install only versioned names (`clang++-22`, not `clang++`), so presets and
scripts name them explicitly. The installed GCC package is a development snapshot rather than
a final GCC 16 release. The CI image installs the same tools from Ubuntu packages; package
revisions without explicit pins are resolved when the image is built.

Ubuntu packages an older CMake, so CMake comes from [Kitware's apt repository](https://apt.kitware.com).
To add it to a machine, install its key, then the repository; the `kitware-archive-keyring`
package keeps the key current from then on:

```bash
curl -fsSL https://apt.kitware.com/keys/kitware-archive-latest.asc | gpg --dearmor \
  | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg > /dev/null
sudo tee /etc/apt/sources.list.d/kitware.sources > /dev/null <<'SOURCES'
Types: deb
URIs: https://apt.kitware.com/ubuntu/
Suites: resolute
Components: main
Signed-By: /usr/share/keyrings/kitware-archive-keyring.gpg
SOURCES
sudo apt update && sudo apt install cmake cmake-data kitware-archive-keyring
```

The CI image pins the CMake package (`CMAKE_VERSION` in `docker/ci/Dockerfile`); cmake_template's
toolchain report notes when that pin and the installed CMake differ. A plain `apt upgrade` moves a
machine to Kitware's newest CMake; `sudo apt-mark hold cmake cmake-data` keeps the version until
you choose to move.

Ubuntu packages an older Catch2, so build the release the kit requires once per machine, with the
C++ standard the repos use. Without it, every build tree downloads and builds its own copy, which
takes a few seconds and needs network when the tree is configured:

```bash
src=$(mktemp -d)
git clone --depth 1 --branch v3.16.1 https://github.com/catchorg/Catch2.git "$src"
cmake -S "$src" -B "$src/build" -G Ninja -DCMAKE_CXX_COMPILER=g++-16 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=23 -DBUILD_TESTING=OFF -DCATCH_INSTALL_DOCS=OFF
cmake --build "$src/build"
sudo cmake --install "$src/build"   # into /usr/local
rm -rf "$src"
```

The CI image builds the same release (`CATCH2_VERSION` in `docker/ci/Dockerfile`).
cmake_template's toolchain report shows which Catch2 builds use and notes a mismatch with the image.
Where `clang-format-22` can't come from packages, `pipx install clang-format==22.1.2` installs the
same formatter from PyPI as `clang-format`; set `CLANG_FORMAT=clang-format` for the checks and hooks.

## Python environment

Ordinary C++ builds and repository scripts need no third-party Python packages or virtual environment.
Python 3.10 is the minimum because the scripts use annotations such as `str | None`;
the current local version is recorded in [Toolchain](#toolchain).

`python3 tools/kit/checks/python.py` checks the interpreter version, compiles repository Python
scripts in memory, and checks their imported modules. It discovers tracked and unignored new
scripts through Git; local helpers are syntax-checked without executing them. Bootstrap runs
this check before builds and skips Python-dependent steps if it fails. When `clang` is selected,
it also checks that the clang-tidy runner starts. The runner's optional PyYAML support is not
needed by our current naming check.

### Native Python wheels

Python packaging is optional and uses a dedicated environment. On Ubuntu, install
`python3-venv` and `python3-dev` alongside the normal toolchain, then run from the repo root:

```bash
python3 -m venv build/python-env
build/python-env/bin/python -m pip install -r tools/kit/python_package_requirements.txt
CXX=g++-16 build/python-env/bin/python -m build --wheel --no-isolation --outdir build/wheels .
```

The requirements file pins the packaging tools, including scikit-build-core. The explicit
environment and `--no-isolation` use those versions without creating another build environment.
The repository's `pyproject.toml` selects the backend, derives the package version from CMake
`project()` and enables `<PROJECT>_BUILD_PYTHON`. CMake's `nom_python` install component supplies
the native package files, independently of `<PROJECT>_INSTALL`; wheel builds disable the native
C++ installation. Python runtime dependencies remain separate package metadata.

Packaging is tested on Ubuntu 26.04 with GCC 16 and CPython 3.14.4. A wheel is built for that
environment, with no claim of compatibility across Linux distributions or Python versions.
Source and imported C++ module libraries must use the same tested compiler and compatible
runtime. Ordinary bootstrap builds do not install packaging dependencies. Add a package using
the [Python package recipe](howto.md#a-python-package).

## Presets

The presets hold the build settings that developers, CI, IDEs and agents share. The shared ones
are in `cmake/kit/presets.json`; a repo's `CMakePresets.json` includes that file and can add
presets of its own under other names.

| Preset | Compiler and build type | Tests | Purpose |
|---|---|---|---|
| `dev` | GCC 16, Debug | fast: all but `integration` and `kit` | daily work |
| `complete` | GCC 16, Debug | all | every app, including experiments, regardless of its `BUILD_FOR` list |
| `release` | GCC 16, Release | all | benchmarks and production builds |
| `clang` | Clang 22, Debug | all | second compiler; its database feeds clangd and clang-tidy |
| `asan` | Clang 22, Debug, AddressSanitizer + UBSan | all | memory errors and undefined behaviour in the code tests run |
| `ci` | GCC 16, RelWithDebInfo, warnings as errors | all | the GitHub CI build |

`./bootstrap.sh` runs every preset but `release`, which it runs only when named. GitHub CI builds
`ci`, plus `clang` for the naming check.

- `ci` is the gate: warnings fail it rather than the daily build. It optimizes because some of
  GCC's warnings, such as `-Wmaybe-uninitialized`, `-Warray-bounds` and `-Wstringop-overflow`,
  depend on analysis that only optimization runs; a Debug build misses them. It also tests the
  optimized code with `NDEBUG`, as `release` ships it, while `dev` tests Debug with assertions.
  Its debug info keeps a crash in CI readable.
- `asan` builds the code instrumented: AddressSanitizer stops a test at an out-of-bounds access,
  a use after free or a leak, and UBSan at undefined behaviour such as signed overflow or a
  misaligned access. It only checks code that tests run, so it finds more as tests grow.
  Library usage tests build their consumers with the same flags; GitHub CI doesn't run it.

Executable names and package versions are recorded in [Toolchain](#toolchain). Current module
and sanitizer validation is recorded in `cmake_template/docs/lab/toolchain.md`;
validate the supported presets when changing compiler versions.

Personal presets go in `CMakeUserPresets.json`, which is git-ignored. For example, to build
against a local checkout of the config repo (see Dependencies):

```json
{
  "version": 6,
  "configurePresets": [
    { "name": "dev-local", "inherits": "dev",
      "cacheVariables": { "FETCHCONTENT_SOURCE_DIR_CONFIG": "${sourceDir}/../config" } }
  ]
}
```

## Selecting apps by preset

`nom_app()` accepts an optional list: `BUILD_FOR dev ci`. Omit it, or use `BUILD_FOR any`, to
include the app in every preset. Use `BUILD_FOR complete` for an experiment that should only join
complete builds. The `complete` preset inherits the `dev` compiler settings and selects every app,
including apps listed only for other presets.

Apps outside the selected list remain available by target name, but are excluded from the
normal build and installation. The same selection applies to installation of their config
folders; a folder shared by multiple apps is installed if any of those apps is selected.
Dependencies of a requested target are still built as needed.

```bash
cmake --workflow --preset dev                         # configure, build selected targets, run tests
cmake --workflow --preset complete                         # same workflow with every app
cmake --build --preset dev --target sandbox_native_module  # one excluded app explicitly
```

`any` means every preset; `complete` names the preset that builds every app. For example,
`BUILD_FOR dev` includes the app in both `dev` and `complete`, so adding `complete` to that list
is optional. A normal build includes the selected apps and test executables; the workflow
also runs the tests.

Presets set the shared cache variable `NOM_BUILD_PRESET`. A custom preset inherits its parent's
selection unless it overrides that variable, so `dev-local` above still selects `dev` apps.
Without a preset it defaults to `dev`; set `-DNOM_BUILD_PRESET=release` or `complete` when needed.
This selects apps independently of `CMAKE_BUILD_TYPE`; `<PROJECT>_BUILD_APPS` still controls
whether app targets are created. For every app with Clang, use
`cmake --preset clang -DNOM_BUILD_PRESET=complete`, then build that preset.

## compile_commands.json

Every preset writes this compilation database (the exact compiler command for each file) into
its build directory. Clang tools can't read GCC's module flags, so clangd and clang-tidy use the
`clang` preset's database: `.clangd` points at `build/clang`, so configure and build that preset
once. In VS Code, use the clangd extension with `"clangd.path": "/usr/bin/clangd-22"` and
`"clangd.arguments": ["--experimental-modules-support"]`.

## The CMake kit

`cmake/kit/` is shared by every repo and owned by the template (change it in `cmake_template`, then
run `tools/kit/sync_template.py` in the other repos). The top-level `CMakeLists.txt` includes
`cmake/kit/nom_kit.cmake`, which sets C++23 without compiler extensions. This remains the single
entry point: `api/` contains the helpers used by repositories, `deps/` contains dependency
services, `internal/` contains implementation helpers, and `templates/` contains generic package
metadata templates. See the [kit overview](../../cmake/kit/README.md) for short examples.

The entry point provides:

| Function | What it does |
|---|---|
| `nom_project_options()` | The project's options (below). Call it right after `project()`. |
| `nom_lib(<name> [STATIC \| SHARED] [SOURCES …] [HEADERS …] [MODULES …] [BASE_DIR <dir>] [PUBLIC_DEPS …] [PRIVATE_DEPS …] [COMPILE_OPTIONS …])` | Library `<name>` plus alias `nom::<name>`. `MODULES` lists native interfaces and partitions; `SOURCES` lists implementation units and other compiled sources. File sets are rooted at `BASE_DIR` (default `core/`). Header-only macro or dependency adapters can be INTERFACE targets. Compile options are PRIVATE for compiled libraries and INTERFACE otherwise. |
| `nom_app(<name> SOURCES … [DEPS …] [BUILD_FOR …] [COMPILE_OPTIONS …])` | Executable target `<name>` (unique across the whole build), binary `bin/<name>`; selects default build/install participation by preset; collects an adjacent `config/` directory. Compile options are PRIVATE. |
| `nom_python_module(<target> MODULE_NAME <name> PACKAGE <package> SOURCES … [DEPS …] [RUNTIME_LIBRARIES …])` | Linux Python extension and explicitly bundled shared libraries, installed through the `nom_python` component. |
| `nom_c2test(<suite> SOURCES … [DEPS …] [LABELS …] [PROPERTIES …])` | Catch2 executable `<project>_<suite>`, registered with CTest and labeled `unit`; `LABELS` replaces the label, `PROPERTIES` sets other test properties. Needs `nom_find_catch2()`. |
| `nom_library_usage_tests(<dir> [OPTIONS …])` | Integration tests: build and run a separate project using this library as an installed package and from source (see Tests). |
| `nom_install_package(TARGETS … [DEPENDENCIES …])` | Install rules and `<project>-config.cmake`, so consumers can `find_package(<project>)` and link `nom::<target>`. The config finds `DEPENDENCIES` first, with the version and components a kit service found them with. |
| `nom_require_submodule(<path>)` | Fails early, with the fix, when a submodule wasn't checked out. |
| `nom_find_catch2()` | An installed Catch2 3.16.1 or newer within major version 3, otherwise that release downloaded and built in the build tree; called at the project root when tests are enabled, before code directories. |
| `nom_find_boost()` | The system Boost 1.90 or newer with `Boost::program_options`, `Boost::filesystem` and `Boost::iostreams`; called at the project root, before code directories. |

`nom_c2test` is specific to Catch2. Future test frameworks can have their own helpers
without changing `nom_lib` or `nom_app`.

`nom_project_options()` constructs cache option names from the upper-case project name;
for example, `project(prices)` produces `PRICES_BUILD_TESTS`. Set these options with `-D` on
the command line. Their `nom_*` aliases belong to the calling directory and are inherited by
its subdirectories. A dependency calling `nom_project_options()` establishes its own aliases,
so the host's enabled tests or apps do not enable them in the dependency.

| Option | Variable | Default | Controls |
|---|---|---|---|
| `<PROJECT>_BUILD_TESTS` | `nom_build_tests` | ON when top level, OFF as a dependency | test targets and registrations |
| `<PROJECT>_BUILD_APPS` | `nom_build_apps` | ON when top level, OFF as a dependency | apps, including the sandbox and config collection |
| `<PROJECT>_BUILD_PYTHON` | `nom_build_python` | OFF | optional Python bindings and wheel installation |
| `<PROJECT>_INSTALL` | `nom_install` | ON | native install rules and the package config |

Public helpers return without creating targets or rules when their corresponding alias is OFF:
`nom_app()` checks `nom_build_apps`, `nom_python_module()` checks `nom_build_python`,
`nom_c2test()` and `nom_library_usage_tests()` check `nom_build_tests`, and
`nom_install_package()` checks `nom_install`. App installation also requires `nom_install`;
Python's `nom_python` component remains independent of it.

Keep directory or caller guards for optional dependency discovery and for commands that modify
targets or tests skipped by a helper, such as `set_tests_properties()`. Direct `add_test()` calls
also need a `nom_build_tests` guard. A guard is unnecessary around a helper that handles the
complete operation.

In a superbuild each repo brings its own copy of the kit; the first one loaded wins, and a
different version is reported as a warning.

Named modules are part of every Nomisma library build.

## Dependencies

- **Our repos:** a git submodule in `external/<name>`, wired with
  `FetchContent_Declare(<name> SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/<name>)` and
  `FetchContent_MakeAvailable(<name>)`. FetchContent keeps the first declaration of a name, so in a
  superbuild the outermost project must declare all shared pins before making any dependency
  available. Use consistent dependency names; version compatibility remains the system repo's
  responsibility. See the [FetchContent documentation](https://cmake.org/cmake/help/latest/module/FetchContent.html).
- **Third-party code:** a submodule pinned to a release tag, handled case by case.
  - nlohmann_json: its own CMake project works as a subproject, so it is used as is (see
    cmake_template's top-level `CMakeLists.txt`). `JSON_Install` follows our install option, so an
    installed package brings nlohmann with it.
  - Header-only libraries whose CMake project doesn't suit a subproject are used through
    their include directory only; their CMake project is never run.
- **Boost:** `nom_find_boost()`, from system packages: `Boost::program_options`,
  `Boost::filesystem` and `Boost::iostreams`. Link the ones a target uses, and list `Boost` in
  `nom_install_package(DEPENDENCIES …)` when exported targets link them; the package config then
  finds the same components. To use another Boost library, add it to `nom_find_boost()` in
  cmake_template, so every repo and package finds the same set.
- **Other system libraries** (OpenSSL, later): `find_package` only.
- **Catch2:** `nom_find_catch2()`.
- **Co-developing two repos:** set `FETCHCONTENT_SOURCE_DIR_<NAME>` (upper-case name) to a local
  checkout, in `CMakeUserPresets.json` as above. No submodule bump is needed until you commit.
- **Newer releases:** bootstrap runs `python3 tools/kit/checks/dependency_updates.py`, which asks
  each submodule's remote for its release tags (`vX.Y.Z` or `X.Y.Z`) and records a `NOTE` naming
  the submodules with a newer one. A remote it can't reach, for example without a network, or a
  recorded commit that isn't a release, is reported unchecked; it never fails the run.
- **Moving a pin:**
  ```bash
  git -C external/nlohmann_json fetch --depth 1 origin tag v3.12.1
  git -C external/nlohmann_json checkout v3.12.1
  git add external/nlohmann_json
  ```

Adding one: [howto.md](howto.md#a-dependency).

## App configuration collection

When an app directory contains `config/`, `nom_app()` stages its contents at
`build/<preset>/config/<app-directory>/` whenever the app builds. Multiple executables declared
in the same directory share one collection target, `<project>_copy_<app-directory>_config`.
The staged folder is refreshed on each build, so deleted source files disappear as well. With
`<PROJECT>_INSTALL=ON` and the app selected for the current preset, the executable installs to `<prefix>/bin/` and the collected files to
`<prefix>/config/<app-directory>/`. Build before installing so the collected files are current.
App directory names must be unique within a combined build because they share `build/config/`.
This step copies config files; it does not generate or load runtime configuration. See the
[app recipe](howto.md#an-app).

## Modules

Nomisma libraries define their C++ APIs in native named modules. The primary interface is
`core/<repo>/<repo>.cppm`, declaring `export module nom.<repo>;`. Larger APIs can split into
interface partitions and re-export them from the primary interface. Template definitions live
in interfaces or partitions; compiled definitions live in `.cpp` implementation units declaring
`module nom.<repo>;`. Apps and tests use `import nom.<repo>;`.

Use `MODULES` in `nom_lib()` to declare interfaces and partitions, including for libraries
consisting entirely of templates. The [module recipe](howto.md#a-module) shows the CMake and
source layout.

Headers still have specific roles at the boundaries:

- Macros cannot be exported. Keep them in ordinary headers and include them before imports;
  Catch2 tests follow the same rule for their assertion macros.
- Include the standard-library headers each file uses (for example `<string>` to compare
  `std::string`s). A module's global module fragment does not make those declarations visible
  to importers. `import std;` needs separate toolchain validation.
- Third-party libraries without suitable module support can be included in a global module
  fragment, wrapped behind a module API, or used through headers by consumers that need them.
  Choose the boundary for the dependency and test it on the supported compilers. Keep our
  public declarations in named modules.
- Prefer keeping a dependency private when its types are not part of the API. The example's
  settings implementation uses nlohmann headers privately, while the module exports settings
  values and parsing/serialization functions. Current third-party integration findings are in
  `cmake_template/docs/lab/third_party_in_modules.md`.

In module units, put includes in the global module fragment (`module;` followed by the includes),
before the named module declaration. In ordinary sources, put includes before imports.
Installed packages carry module interface and partition sources along with any macro headers;
consumers build their own compiled module interfaces (BMIs), which are specific to the compiler
and its flags. Package consumption is tested with the same compiler as the package build;
cross-toolchain binary compatibility is not implied.

## Tests

- `tests/unit/<suite>/` or inline `<code dir>/tests/<suite>/`: Catch2 unit tests, created with
  `nom_c2test()` and labeled `unit`. Catch2 runs a suite's test cases in random order, so they
  must not depend on each other; `--order decl` runs them in declaration order while you look for
  such a dependency. The root enables testing and finds Catch2 before processing
  `core/` and `apps/`, when `nom_build_tests` is enabled. Suites import the library's primary
  module; there is one path through the public API.
- `tests/integration/`: checks involving project boundaries or installation, labeled `integration`.
  `library_usage/` is a separate CMake project that compiles, links and runs against this library.
  `nom_library_usage_tests()` installs the main project into
  `build/<preset>/tests/integration/library_usage/install`, then builds and runs the test project
  against that package with `find_package` (`<project>_library_usage_package`), and from source
  through `FetchContent` (`<project>_library_usage_source`). Both modes compile the same importing
  consumer, including rebuilding installed module interfaces in package mode. With `nom_install`
  OFF, only the source-consumption test is registered. With `nom_build_tests` OFF, neither mode
  is registered.
- cmake_template's `tests/kit/`: tests of the kit and its tools themselves, labeled `kit`, such
  as `app_presets/` for app build and install selection. Each builds throwaway projects or repos
  of its own; new repos don't get them.

Labels are CTest's tags: a test can carry several and belongs to each. Give every test exactly
one of the levels `unit`, `integration` and `kit`, plus any tags that help select it, such as
`python_package`. `nom_c2test()` and `nom_library_usage_tests()` label their tests; a direct
`add_test()` needs `set_tests_properties(… LABELS …)`. A test without a label belongs to no level:
it runs in every build, but `ctest -L` never selects it. Keep a fixture's setup test and the tests
that require it on the same level, since a build that leaves out the setup test runs the others
without it.

The `dev` preset sets the cache variable `NOM_SKIP_TEST_LABELS` to `integration;kit`, the
labels of the tests that build other projects and take seconds each. The kit lists the tests
carrying them in the build tree's `CTestCustom.cmake`, so every ctest run in `build/dev` leaves
them out, with or without a test preset. The other presets run every test; a personal preset can
set its own list. The test presets run tests in parallel, one per CPU (they set
`CTEST_PARALLEL_LEVEL` empty); in a build directory, `ctest -j` does the same. Fixtures
(`FIXTURES_SETUP` and `FIXTURES_REQUIRED`) order tests that depend on each other, and
`RUN_SERIAL` keeps a test that needs the whole machine alone.

```bash
ctest --preset dev                        # the fast tests: unit, and any test without those labels
ctest --preset complete                   # every test
ctest --preset complete -L integration    # one label: unit, integration or kit
ctest --preset complete -j1               # one test at a time
```

A `dev` build has no integration or kit tests to select, so `ctest --preset dev -L integration`
finds none.

## Install and use

```bash
cmake --install build/release --prefix /opt/nomisma
```

```cmake
find_package(<project> 0.1 CONFIG REQUIRED)       # with CMAKE_PREFIX_PATH=/opt/nomisma
target_link_libraries(app PRIVATE nom::<project>)
```

The consumer uses `import nom.<project>;` and the supported module-capable compiler, CMake and
generator listed in [Toolchain](#toolchain).

## Versioning

Every repo follows Semantic Versioning (`MAJOR.MINOR.PATCH`) from its first commit, starting at
`0.1.0`: MAJOR for incompatible API changes, MINOR for compatible additions, PATCH for fixes.
While MAJOR is 0 any release may break the API, so an installed `0.y` package only satisfies
requests for the same `0.y`. The version lives only in `project()`, and each release is an
annotated tag `vX.Y.Z` on `master`.

To release `x.y.z`:

1. `CMakeLists.txt`: set `VERSION x.y.z` in `project()`.
2. `CHANGELOG.md`: rename the `[Unreleased]` section to `[x.y.z] - YYYY-MM-DD`, and start a new,
   empty `[Unreleased]` above it.
3. Commit ("Release x.y.z") and merge to `master`.
4. Tag that commit on `master` and push the tag:
   ```bash
   git switch master && git pull
   python3 tools/kit/checks/version.py vx.y.z     # the check CI runs on the tag
   git tag -a vx.y.z -m "Release x.y.z"
   git push origin vx.y.z
   ```

## Checks and CI

| Check | Command |
|---|---|
| Python environment | `python3 tools/kit/checks/python.py` |
| SPDX headers | `python3 tools/kit/checks/headers.py` (`--staged` for the staged files) |
| Formatting | `tools/kit/checks/format.sh` (`--fix` to reformat, `--staged` for the staged files) |
| Naming | `tools/kit/checks/naming.sh` (builds `clang` with `NOM_BUILD_PRESET=complete` to cover every app) |
| Release tag | `python3 tools/kit/checks/version.py <tag>` |
| Dependency updates | `python3 tools/kit/checks/dependency_updates.py` (a report; bootstrap only) |

GitHub Actions (`.github/workflows/ci.yml`) runs the `ci` workflow preset and the first four
checks on pushes to `master`, pull requests, manual runs, and `v*` tags. The version check runs
only on `v*` tags. Build and quality checks run in the CI image `ghcr.io/<owner>/ci`, which
cmake_template builds from `docker/ci/Dockerfile`.

## Git hooks

`./bootstrap.sh` enables the hooks in `tools/kit/hooks/` for the clone: it sets `core.hooksPath`
to `tools/kit/hooks` in the clone's `.git/config`, unless the setting already names other hooks.
Git then runs them on every commit:

| Hook | Checks |
|---|---|
| `pre-commit` | the staged copy of each staged file: whitespace errors and conflict markers (`git diff --cached --check`; Markdown may keep trailing spaces), formatting (`format.sh --staged`) and SPDX headers (`headers.py --staged`) |
| `commit-msg` | the message has no `Co-Authored-By` trailer |

The hooks check what the commit gets, not the working tree: an unformatted change that isn't
staged doesn't stop a commit, and a fix that isn't staged doesn't help. `git commit -a` stages
the working copies of tracked files first, so those are checked, and a rejected commit leaves
nothing staged. For formatting, the hook prints the changes needed and the command that makes
them; run it, stage the fixes (`git add -p` for a file that also has unstaged changes), and
commit again. Naming needs a build, so it stays in bootstrap and CI.

Hooks run only in clones that enable them, and `git commit --no-verify` skips them, so CI stays
the check every change passes. To opt a clone out, point the setting elsewhere, for example
`git config core.hooksPath .git/hooks`; bootstrap leaves it alone from then on.

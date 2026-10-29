# CMake kit

The **kit** is the shared build framework for Nomisma repositories. It provides helpers for
declaring libraries, applications, tests and installable packages, backed by common dependency
and toolchain services. Enable standard-library module detection before `project()`, then load
the main kit after it:

```cmake
cmake_minimum_required(VERSION 4.4)
include(cmake/kit/std_modules.cmake)
project(prices LANGUAGES CXX)
include(cmake/kit/nom_kit.cmake)
nom_project_options()
```

`std_modules.cmake` is the pre-project entry point for CMake 4.4.x `import std` support.
`nom_kit.cmake` loads the remaining files in the required order. Pass `IMPORT_STD` to
`nom_lib`, `nom_app`, `nom_c2test`, or `nom_python_module` for each compiled target importing
`std` or consuming a module using it. Disabled helpers skip module setup too. Interface
libraries cannot use `IMPORT_STD`. For targets created outside the kit, call
`nom_enable_std_modules(target)` after creating the target.
See [standard-library modules](../../docs/kit/build.md#modules) for consumer setup and toolchains.

| Location | Purpose |
|---|---|
| [api/](api/) | Helpers used in repository `CMakeLists.txt` files: project options, targets, tests, C++ installation and optional Python extensions. |
| [deps/](deps/) | Dependency services: submodule checks, Catch2 and Boost discovery. |
| [internal/](internal/) | Implementation details such as compiler warnings, runtime paths, Python library checks and the build tree's test selection. |
| [templates/](templates/) | Templates used to generate installed CMake package metadata. Dependency-specific templates stay with their service in `deps/`. |
| [presets.json](presets.json) | The shared configure, build, test and workflow presets. Each repo's `CMakePresets.json` includes it and adds its own. |

For a library defined in `core/prices/CMakeLists.txt`:

```cmake
nom_lib(prices IMPORT_STD
    MODULES prices.cppm
    SOURCES prices.cpp)

nom_install_package(TARGETS prices)
```

This defines the library and its `nom::prices` alias. `MODULES` lists C++ module interfaces;
`SOURCES` lists compiled implementation files. The install helper lets downstream C++ projects
use `find_package(<project> CONFIG REQUIRED)` and link `nom::prices`.

For an application in `apps/capture/CMakeLists.txt`:

```cmake
nom_app(capture IMPORT_STD SOURCES main.cpp DEPS nom::prices)
```

This creates target `capture`, producing `bin/capture`. Use `capture` directly with
`target_*()` commands and `set_target_properties()`. Choose a name unique across the whole
build, including enabled apps from dependencies; add an explicit prefix when needed. An
adjacent `config/` directory is collected and installed with the application. Add these folders through their parent's
`add_subdirectory()` calls, following the [recipes](../../docs/kit/howto.md).

Both helpers accept `COMPILE_OPTIONS`, a list of compiler flags:

```cmake
nom_app(capture_perf IMPORT_STD SOURCES main.cpp DEPS nom::prices
    COMPILE_OPTIONS -DNOM_CONFIG_APP=capture -Wextra)
```

Use `-DNAME=value` for macros. An unquoted name such as `capture` is a token;
stringify it in C++ when a string is needed. Options are private to apps and compiled
libraries; an interface library passes them to consumers. Pass each option as a separate
CMake argument; normal CMake quoting is needed only for arguments containing spaces.

Other common helpers are `nom_c2test()` for Catch2 tests, labeled `unit` unless `LABELS` says
otherwise, `nom_library_usage_tests()` for C++
source/install consumption checks, and `nom_python_module()` for optional Python extensions.
Dependency services such as `nom_find_catch2()` and `nom_find_boost()` are called before targets that need them. Full signatures and options are in the [build guide](../../docs/kit/build.md#the-cmake-kit).

`nom_project_options()` defines project-specific cache options and directory-local aliases.
The helpers check these aliases themselves: `nom_app()` uses `nom_build_apps`, test helpers
use `nom_build_tests`, `nom_python_module()` uses `nom_build_python`, and
`nom_install_package()` uses `nom_install`. Tests and apps default to OFF in a dependency;
each project controls its own values. Library usage tests keep source consumption when
installation is OFF. Python wheels use the separate `nom_python` component even with native
installation disabled.

Keep guards for optional dependency discovery, direct `add_test()` calls, and commands that
modify a target or test a disabled helper would skip. A helper call alone needs no repeated guard.

The cache variable `NOM_SKIP_TEST_LABELS` lists test labels that ctest leaves out in the build
tree; the `dev` preset sets it to `integration;kit`. The kit writes the build tree's
`CTestCustom.cmake` for it ([Tests](../../docs/kit/build.md#tests)).

The kit is **template-owned**: update it in `cmake_template`, then use `tools/kit/sync_template.py`
in derived repos. A repo's own CMake files go in `cmake/<repo>/`; shared build mechanisms belong
in the kit.

# How to add code

Recipes for the usual changes. A new repo starts empty and grows through them; cmake_template's
example code (`core/example/`, `apps/hello/`, `tests/`) is the worked reference for each one.
`<repo>` stands for the repo's name, the one in `project()`. Names follow
[conventions.md](conventions.md), every new file starts with the SPDX header
([conventions.md](conventions.md#enforcement)), and build commands are in [build.md](build.md).

## A library

The CMake project names the whole repo; library targets have their own names. A repo's main
library normally uses the repo name: target `<repo>`, alias `nom::<repo>`, module `nom.<repo>`.
Module names and CMake aliases do not impose a C++ namespace hierarchy; exported names can
live directly in `nom`.
In `cmake_template`, the illustrative library is instead named `example`: the integration test
uses `find_package(cmake_template)` and links `nom::example`.

1. Put the primary module interface in `core/<repo>/<repo>.cppm`. Define the public API there
   or in interface partitions (`<part>.cppm`); put compiled definitions in implementation units
   (`<file>.cpp`). The [module recipe](#a-module) shows the source layout.
2. Add `core/<repo>/CMakeLists.txt`:
   ```cmake
   nom_lib(<repo>
       MODULES <repo>.cppm thing.cppm
       SOURCES thing.cpp
       HEADERS macros.h         # only when the library needs public macros
       PUBLIC_DEPS <targets>    # dependencies needed by the public interface, if any
       PRIVATE_DEPS <targets>)  # dependencies used only by the implementation, if any
   nom_install_package(TARGETS <repo> DEPENDENCIES <packages>)
   ```

   `DEPENDENCIES` lists the packages needed by exported CMake targets, including private
   dependencies that a static library carries into its link interface. The helper skips
   installation rules when `nom_install` is OFF.
3. Add `add_subdirectory(<repo>)` to `core/CMakeLists.txt`.
4. Add its unit tests and library usage integration tests (below).

Use `STATIC` or `SHARED` in `nom_lib` to select compiled-library linkage explicitly.
They are mutually exclusive and require sources or modules. Omitting both preserves
the `BUILD_SHARED_LIBS` default. A template-only API still has module sources and is a compiled
module target. An interface target without `SOURCES` or `MODULES` is for macro headers or
third-party dependency adapters.

A second library is named `<repo>_<name>`; list it in the same `nom_install_package()` call, which
a repo makes once.

## A module

Nomisma libraries expose their APIs through native named modules. Their declarations follow the
module declaration; apps and tests consume them with `import nom.<repo>;`.

1. Add `core/<repo>/<repo>.cppm`:
   ```cpp
   export module nom.<repo>;

   export import :thing;
   ```
2. Add the interface partition `core/<repo>/thing.cppm`:
   ```cpp
   export module nom.<repo>:thing;

   export namespace nom
   {
       class thing
       {
       public:
           int value() const;
       };
   } // namespace nom
   ```
   Template definitions belong in the interface or partition too, so importers can instantiate
   them. A small library can define its API directly in the primary interface without partitions.
3. Put compiled definitions in `core/<repo>/thing.cpp`:
   ```cpp
   module nom.<repo>;

   namespace nom
   {
       int thing::value() const
       {
           return 42;
       }
   } // namespace nom
   ```
4. Register the interfaces and partitions unconditionally in `MODULES`, and implementation
   units in `SOURCES`:
   ```cmake
   nom_lib(<repo> MODULES <repo>.cppm thing.cppm SOURCES thing.cpp)
   ```
5. For standard-library or third-party includes inside a module unit, start with a global module
   fragment (`module;`, then the includes) before the named module declaration. Keep dependencies
   private when the API does not need their types: `core/example/settings.cpp` includes nlohmann
   privately and exports ordinary settings, parsing and serialization services through its
   interface partition. Macros remain in headers; include those before imports in consumers.

More rules and dependency boundaries: [build.md](build.md#modules).

## An app

1. Put `main.cpp` in `apps/<app>/`; import the Nomisma libraries it uses.
2. Add `apps/<app>/CMakeLists.txt`:
   ```cmake
   nom_app(<app> SOURCES main.cpp DEPS nom::<repo>)
   ```
   When `nom_build_apps` is OFF, the helper creates no target or config collection rules.
   The target is `<app>`; the binary is `build/<preset>/bin/<app>`. Choose a target name unique
   across the whole build, including enabled dependency apps. By default it joins
   every preset (`BUILD_FOR any`). Restrict its normal build and installation with:
   ```cmake
   nom_app(<app> SOURCES main.cpp DEPS nom::<repo> BUILD_FOR dev ci)
   ```
   Other presets can still build it explicitly by target name. The `complete` preset includes every
   app regardless of this list. Use `BUILD_FOR complete` for prototypes. Apply further target
   settings using the same name, guarded because apps can be disabled:
   ```cmake
   if(TARGET <app>)
       target_compile_definitions(<app> PRIVATE NOM_APP_VERBOSE=1)
       set_target_properties(<app> PROPERTIES OUTPUT_NAME <binary_name>)
   endif()
   ```
3. Add `add_subdirectory(<app>)` to `apps/CMakeLists.txt`.
4. Put application configuration files in `apps/<app>/config/` when needed. Building any app
   declared in that directory collects the whole folder into `build/<preset>/config/<app>/`.
   The copy runs on each build and removes stale files from the staged folder. Multiple
   executables in the same app directory share one collection target. With `nom_install` enabled and the app selected by the preset,
   `cmake --install` puts the executable in `<prefix>/bin/` and the collected files in
   `<prefix>/config/<app>/`. Build before installing; config generation will be added later.

## A unit test suite

1. Put Catch2 tests under `tests/unit/<suite>/` or inline beside the code in
   `<code dir>/tests/<suite>/`, as `<file>_test.cxx`. Test-only headers are `<name>_test.h`.
   Include Catch2's macro header, then import the library under test:
   ```cpp
   #include <catch2/catch_test_macros.hpp>

   import nom.<repo>;

   TEST_CASE("a thing has a value")
   {
       REQUIRE(nom::thing{}.value() == 42);
   }
   ```
2. Give the folder a `CMakeLists.txt` with one executable:
   ```cmake
   nom_c2test(tests SOURCES file_test.cxx DEPS nom::<repo>)
   ```
   `nom_c2test(<suite> …)` builds `<repo>_<suite>`, registers it with CTest and labels it `unit`;
   each folder needs its own suite name (`tests`, `relay_tests`, …). `LABELS` replaces the label,
   such as `LABELS integration` for a slow suite that `dev` builds leave out, and `PROPERTIES`
   sets other test properties, such as `PROPERTIES TIMEOUT 60 RESOURCE_LOCK database`. With
   `nom_build_tests` OFF, it creates neither the executable nor the test.
3. For a top-level suite, add `add_subdirectory(<suite>)` to `tests/unit/CMakeLists.txt`. For an
   inline suite, add this to the code directory's `CMakeLists.txt`, after defining the library:
   ```cmake
   if(nom_build_tests)
       add_subdirectory(tests/<suite>)
   endif()
   ```
   Use unique suite names across both layouts. The root `CMakeLists.txt` enables testing and
   finds Catch2 when tests are enabled before processing code directories, so both layouts can
   call `nom_c2test()`. Keep guards around that dependency discovery and direct `add_test()` calls.

## Library usage integration tests

These verify that a separate project can find, compile against, link and use the library. They
build and run a small program twice: against the installed package through `find_package`, and
against the source tree through `FetchContent`. This checks the public API and CMake integration
that other repos rely on. `nom_library_usage_tests()` registers no tests when `nom_build_tests`
is OFF. With `nom_install` OFF it keeps the source-consumption test and omits the installation
fixture and installed-package test.

1. Add `tests/integration/library_usage/CMakeLists.txt`:
   ```cmake
   cmake_minimum_required(VERSION 3.30)
   project(<repo>_library_usage LANGUAGES CXX)

   set(CMAKE_CXX_STANDARD 23)
   set(CMAKE_CXX_STANDARD_REQUIRED ON)
   set(CMAKE_CXX_EXTENSIONS OFF)

   if(LIBRARY_USAGE_MODE STREQUAL "package")
       find_package(<repo> CONFIG REQUIRED)
   else()
       include(FetchContent)
       FetchContent_Declare(<repo> SOURCE_DIR ${LIBRARY_USAGE_SOURCE_DIR})
       FetchContent_MakeAvailable(<repo>)
   endif()

   enable_testing()
   add_executable(use_library use_library_test.cxx)
   target_link_libraries(use_library PRIVATE nom::<repo>)
   add_test(NAME use_library COMMAND use_library)
   ```
2. Add `tests/integration/library_usage/use_library_test.cxx`: a `main()` that imports the
   library's primary module, uses its API and returns 0 when it works. Use that same source in
   both consumption modes; package consumers rebuild module interfaces for their own compiler.
3. Add `nom_library_usage_tests(library_usage)` to `tests/integration/CMakeLists.txt`. The
   helper labels its tests `integration`. Keep other integration checks here too, with the same
   label. `ctest --preset dev` runs the unit tests without them; `ctest --preset complete` runs
   both, and `ctest --preset complete -L integration` runs these alone.

## A Python package

The shared kit supports optional native Linux wheels. The template's `python/` and
`pyproject.toml` are the worked example; initialization removes them while retaining the kit
and packaging tools. Packaging does not choose or generate the Python API.

1. Put binding sources in `python/` and Python files in `python/<package>/`. The binding must
   export the extension name selected below (`_native`); `__init__.py` can expose its public API.
2. After the root's `add_subdirectory(core)`, enable that directory only on request:
   ```cmake
   if(nom_build_python)
       add_subdirectory(python)
   endif()
   ```
3. Add `python/CMakeLists.txt`, using a shared library target in this example:
   ```cmake
   nom_python_module(<repo>_python
       MODULE_NAME _native
       PACKAGE <package>
       SOURCES native.cpp
       DEPS nom::<repo>
       RUNTIME_LIBRARIES nom::<repo>)
   ```
   The helper skips target creation when `nom_build_python` is OFF. Its `nom_python`
   installation rules remain available with `nom_install` OFF, as wheel builds require.
   `DEPS` uses existing source-built or imported CMake targets. List all required shared targets
   in `RUNTIME_LIBRARIES`, including transitive dependencies; the helper installs them beside
   the extension with package-relative loading paths. Omit that argument for dependencies that
   are all static. Source-built static/object targets become position-independent; imported
   static libraries must already contain PIC objects and declare
   `INTERFACE_POSITION_INDEPENDENT_CODE=ON`.

   Imported shared libraries must have a filename-only ELF SONAME and no runtime path or a
   runtime path of exactly `$ORIGIN`; the helper checks them with `readelf`. It copies these
   binaries without repairing them. Declare the complete target dependency graph; conditional
   link expressions other than the supported `LINK_ONLY`/`BUILD_INTERFACE` wrappers are rejected.
   System-library and portable-wheel repair policies are outside this initial helper.
4. Add `pyproject.toml`. Replace `<repo>`, its upper-case `<PROJECT>`, `<distribution>` and
   `<package>`; keep the backend pin aligned with `tools/kit/python_package_requirements.txt`:
   ```toml
   [build-system]
   requires = ["scikit-build-core==0.11.6"]
   build-backend = "scikit_build_core.build"

   [project]
   name = "<distribution>"
   dynamic = ["version"]
   requires-python = ">=3.10"
   license = "LicenseRef-Nomisma-Labs-Proprietary"
   license-files = ["LICENSE"]

   [tool.scikit-build]
   minimum-version = "build-system.requires"
   cmake.version = ">=3.30"
   ninja.version = ">=1.11"
   cmake.build-type = "Release"
   wheel.packages = ["python/<package>"]
   install.components = ["nom_python"]

   [tool.scikit-build.cmake.define]
   <PROJECT>_BUILD_PYTHON = "ON"
   <PROJECT>_BUILD_APPS = "OFF"
   <PROJECT>_BUILD_TESTS = "OFF"
   <PROJECT>_INSTALL = "OFF"

   [tool.scikit-build.metadata.version]
   provider = "scikit_build_core.metadata.regex"
   input = "CMakeLists.txt"
   regex = '(?s)project\s*\(\s*<repo>\s+VERSION\s+(?P<value>[0-9]+\.[0-9]+\.[0-9]+)'
   ```
   Use the repository's license and selected Python minimum. The latter is a source requirement;
   native wheel tags still select the interpreter and platform actually built. The `nom_python`
   component is separate from the C++ SDK install and excluded from ordinary full installs.
5. Initialize submodules, create the [packaging environment](build.md#native-python-wheels)
   and build a wheel from the complete checkout. Add an integration test that installs the
   artifact into a clean environment and calls the underlying C++ API after making build-tree
   and original installed libraries unavailable. Test any bundled dependency chain as well.

Use the same tested compiler for the binding and C++ module libraries, whether built together
or imported. Normal C++ builds leave `<PROJECT>_BUILD_PYTHON=OFF` and do not need the packaging
environment. Change shared mechanisms in the template, then synchronize them into derived repos.

## A dependency

- **One of our repos:** a submodule with a relative URL, pinned to a release tag:
  ```bash
  git submodule add ../<dep>.git external/<dep>
  git -C external/<dep> checkout v<x.y.z>
  git add external/<dep>
  ```
  Then, in the top-level `CMakeLists.txt` before `add_subdirectory(core)`:
  ```cmake
  nom_require_submodule(external/<dep>)
  FetchContent_Declare(<dep> SOURCE_DIR ${PROJECT_SOURCE_DIR}/external/<dep>)
  FetchContent_MakeAvailable(<dep>)
  ```
  Link `nom::<dep>` and import `nom.<dep>`. If exported CMake targets need it, add `<dep>` to
  `nom_install_package(DEPENDENCIES …)`.
- **Third-party code:** a shallow submodule, pinned to a release tag:
  ```bash
  git submodule add --depth 1 <url> external/<dep>
  git config -f .gitmodules submodule.external/<dep>.shallow true
  git -C external/<dep> fetch --depth 1 origin tag v<x.y.z>
  git -C external/<dep> checkout v<x.y.z>
  git add .gitmodules external/<dep>
  ```
  Then decide case by case: the same three CMake lines when its CMake project works as a
  subproject (nlohmann_json in cmake_template), or only its include directory when it doesn't.
  Use a module wrapper when it gives a useful, supported API boundary; otherwise include
  the dependency's headers where needed. Our library APIs belong in named modules.
- **Boost:** the top-level `CMakeLists.txt` calls `nom_find_boost()`. Link `Boost::program_options`,
  `Boost::filesystem` or `Boost::iostreams` where they are used, include their headers before
  imports or in a module unit's global module fragment, and add `Boost` to
  `nom_install_package(DEPENDENCIES …)` when exported targets link them. cmake_template's
  `apps/boost_example/` uses all three.
- **Other system libraries** (OpenSSL, …): `find_package(<Package> REQUIRED)`.

## An experiment

`python3 tools/kit/add_sandbox_app.py <name>` creates `apps/sandbox/<name>/`, built as
`bin/sandbox_<name>` and linked to the main library if the repo has one. It uses `BUILD_FOR complete`:
run `cmake --workflow --preset complete`, or build its target explicitly in another preset. If an
experiment has its own support libraries, mark them `EXCLUDE_FROM_ALL TRUE` so they build only
when a dependent app is requested. Experiments that need third-party code fetch it in their
own `CMakeLists.txt`. Experiments are git-ignored (`/apps/sandbox/*/` in `.gitignore`), so they
stay in the clone; when one is worth sharing, commit it by force with `git add -f`.

## A release

See [build.md](build.md#versioning).

Installed apps locate packaged shared libraries relative to their executable directory on
Unix/macOS. Shared libraries locate sibling packaged libraries in the install library
directory. These runtime paths follow GNUInstallDirs and remain valid when the complete
install prefix is moved; system dependencies still need to be installed on the host.

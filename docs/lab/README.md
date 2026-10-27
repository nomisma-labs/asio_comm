# Lab

`cmake_template` is also the place to try build and C++ features before real repos rely on them.
Experiments live in `apps/sandbox/` (`python3 tools/kit/add_sandbox_app.py <name>` adds one); findings
go here, one file per topic, with the compiler and CMake versions, the verdict and any workaround.
Experiments are git-ignored like in every repo; one that a finding cites gets a `!` exception in
the template-only block of `.gitignore`, as `native_module` has.
Build experiments with `cmake --workflow --preset complete`; for Clang use
`cmake --preset clang -DNOM_BUILD_PRESET=complete` and `cmake --build --preset clang`.
New repos don't get this folder.

Nomisma library APIs use native named modules. The example exercises
interfaces, partitions, implementation units and importing consumers; headers remain for macros
and third-party boundaries.

Native-module validation on 2026-10-03: fresh `dev`, `complete`, `clang`, `asan`, and `ci`
builds pass all eight CTest checks per preset, including importing unit tests, source/package
consumers and installed-library relocation. The full bootstrap passes all 22 checks, including
formatting, SPDX headers and clang-tidy naming checks. A freshly generated repository also
builds the documented interface, partition, implementation unit and importing app on GCC 16
and Clang 19.

On 2026-10-17 the second compiler moved to Clang 22; the validation, here and in assembly and
json_config, is in [toolchain.md](toolchain.md#clang-22).

The Metal baseline follow-up passed `dev` and `clang`, including importing unit tests and
source/package consumers that instantiate the exported typelist alias; separate installed-package
and shared two-project checks also passed. Since 0.7.0 the template no longer carries Metal:
assembly, its only user, does. See [the Metal findings](third_party_in_modules.md#metal-through-a-native-module).

## Checklist

| # | Topic | Status | Where |
|---|---|---|---|
| 1 | Named modules: interfaces, partitions, implementation units, on GCC 16 and Clang 22 | The native main example and generated-repository recipe pass on both compilers. The sandbox exercises a native interface and partition. | `core/example/`, `apps/sandbox/native_module/`, [toolchain.md](toolchain.md) |
| 2 | Headers at macro and dependency boundaries | The example keeps macros in a header and includes JSON privately in a module implementation. | `core/example/macros.h`, `core/example/settings.cpp`, [third_party_in_modules.md](third_party_in_modules.md) |
| 3 | Macros next to a module | Works: the macro header is included beside `import`. | `core/example/macros.h` |
| 4 | Third-party headers in the global module fragment | Exporting a nlohmann 3.12 alias warns on GCC 16 and fails with `-Werror`; JSON stays private. The exported Metal typelist alias passed on GCC 16 and Clang 19. Boost 1.90 headers pass in an implementation unit's global module fragment and before imports. | [third_party_in_modules.md](third_party_in_modules.md) |
| 5 | Template-heavy code through a module (relay-like typelist dispatch on Metal) | Basic native typelist alias instantiation passes in unit, source and installed-package consumers; the alias used Metal until 0.6.0 and now uses the standard library. Relay-style filtering/dispatch on Metal lives in assembly's `nom.assembly`; performance remains to do. | `core/example/types.cppm`, [Metal findings](third_party_in_modules.md#metal-through-a-native-module) |
| 6 | Installed modules: consumer builds its own BMIs from an installed package | The native importing consumer passes in both source and installed-package modes on GCC 16 and Clang 22. | `tests/integration/library_usage/` (`use_library`) |
| 7 | `import std;` toolchain support | Future toolchain investigation. | |
| 8 | Inlining across module boundaries (hot path) | To do | |
| 9 | Tooling with modules: clangd, clang-tidy, gdb | clang-tidy naming checks pass for native module units and importers, including the generated-repository recipe; `clangd-22 --check` reads an importer without errors; clangd in the editor and gdb to do. | [toolchain.md](toolchain.md) |
| 10 | Build time: headers vs modules | To do | |
| 11 | Dependency mechanics: submodules + FetchContent, diamonds, standalone, source override | Submodule + FetchContent and source consumption pass with GCC 16 and Clang 22. Standalone package usage and shared two-project exports passed with Metal's separate package (until 0.6.0). Diamond superbuild and the `FETCHCONTENT_SOURCE_DIR_<NAME>` override to do. | `tests/integration/library_usage/`, [third_party_in_modules.md](third_party_in_modules.md) |
| 12 | macOS (Homebrew LLVM) | To do | |
| 13 | C++26: pack indexing, static reflection | To do (later) | |

## Findings outside the checklist

- **Importers must include the std headers they use.** A module's global module fragment doesn't
  make those declarations visible to importers: comparing `std::string`s in a file that only
  imports `nom.example` fails until it includes `<string>`.

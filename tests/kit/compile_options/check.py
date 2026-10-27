#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Build and run compiler-option consumers, including module and interface libraries."""

import argparse
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


def run(*args: str) -> None:
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(args)}\n{result.stdout}\n{result.stderr}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cmake', 'compiler', 'generator', 'kit'):
        parser.add_argument(f'--{name}', required=True)
    args = parser.parse_args()

    with TemporaryDirectory(prefix='nomisma-compile-options-') as temporary:
        root = Path(temporary)
        source = root / 'source with spaces'
        source.mkdir()
        kit = Path(args.kit).resolve().as_posix()
        (source / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.30)
project(compile_options LANGUAGES CXX)
include("{kit}/nom_kit.cmake")
nom_project_options()
nom_lib(values ${{LINKAGE}} MODULES values.cppm SOURCES values.cpp
    BASE_DIR "${{CMAKE_CURRENT_SOURCE_DIR}}"
    COMPILE_OPTIONS -DNOM_LIBRARY_VALUE=42 -include "${{CMAKE_CURRENT_SOURCE_DIR}}/forced.h")
nom_lib(flags COMPILE_OPTIONS -DNOM_INTERFACE_VALUE=7)
nom_app(check SOURCES main_test.cxx DEPS nom::values nom::flags
    COMPILE_OPTIONS -DNOM_CONFIG_APP=mm_strategy -Werror)
target_compile_definitions(check PRIVATE NOM_DIRECT_APP_VALUE=11)
set_target_properties(check PROPERTIES OUTPUT_NAME configured_check)
''')
        # -include is a general compiler option with a separate argument containing spaces.
        (source / 'forced.h').write_text('#define NOM_FORCED_VALUE 1\n')
        (source / 'values.cppm').write_text('''export module nom.option_values;
static_assert(NOM_LIBRARY_VALUE == 42 && NOM_FORCED_VALUE == 1);
export int option_value();
''')
        (source / 'values.cpp').write_text('''module nom.option_values;
#if defined(NOM_CONFIG_APP) || defined(NOM_INTERFACE_VALUE)
#error Consumer options must not affect the separately compiled library
#endif
int option_value() { return NOM_LIBRARY_VALUE + NOM_FORCED_VALUE; }
''')
        (source / 'main_test.cxx').write_text('''#include <string_view>
import nom.option_values;
#if defined(NOM_LIBRARY_VALUE) || defined(NOM_FORCED_VALUE)
#error Private library options must not leak to consumers
#endif
#define NOM_STRINGIFY_IMPL(Value) #Value
#define NOM_STRINGIFY(Value) NOM_STRINGIFY_IMPL(Value)
static_assert(NOM_INTERFACE_VALUE == 7);
static_assert(NOM_DIRECT_APP_VALUE == 11);
static_assert(std::string_view(NOM_STRINGIFY(NOM_CONFIG_APP)) == "mm_strategy");
int main() { return option_value() == 43 ? 0 : 1; }
''')
        for linkage in ('STATIC', 'SHARED'):
            build = root / linkage.lower()
            run(args.cmake, '-S', str(source), '-B', str(build), '-G', args.generator,
                f'-DCMAKE_CXX_COMPILER={args.compiler}', f'-DLINKAGE={linkage}',
                '-DCOMPILE_OPTIONS_BUILD_TESTS=OFF', '-DCOMPILE_OPTIONS_INSTALL=OFF')
            run(args.cmake, '--build', str(build), '--target', 'check', '--parallel', '2')
            run(str(build / 'bin/configured_check'))
            print(f'PASS: {linkage.lower()} library, interface options, app stringification and direct target settings')


if __name__ == '__main__':
    main()

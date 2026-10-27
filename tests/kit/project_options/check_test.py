#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Exercise project option guards through native CMake builds and CTest consumers."""

import argparse
import json
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory


def run(*args: str) -> str:
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(args)}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def write(root: Path, files: dict[str, str]) -> None:
    for filename, content in files.items():
        path = root / filename
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)


def configure(args: argparse.Namespace, source: Path, build: Path, *options: str) -> None:
    run(args.cmake, '-S', str(source), '-B', str(build), '-G', args.generator,
        f'-DCMAKE_CXX_COMPILER={args.compiler}', f'-DNOM_KIT_PATH={args.kit}', *options)


def test_names(args: argparse.Namespace, build: Path) -> set[str]:
    document = json.loads(run(args.ctest, '--test-dir', str(build), '--show-only=json-v1'))
    return {test['name'] for test in document['tests']}


def check_disabled_helpers(args: argparse.Namespace, root: Path) -> None:
    source, build = root / 'source', root / 'build'
    write(source, {'CMakeLists.txt': '''cmake_minimum_required(VERSION 3.30)
project(disabled VERSION 1.0.0 LANGUAGES CXX)
include("${NOM_KIT_PATH}/nom_kit.cmake")
nom_project_options()
enable_testing()
# Invalid inputs are intentional: disabled helpers must return before using them.
nom_app(app SOURCES absent.cpp DEPS absent::dependency)
nom_python_module(python)
nom_c2test(unit SOURCES absent_test.cxx)
nom_library_usage_tests(absent_consumer)
nom_install_package(TARGETS absent_library)
foreach(target IN ITEMS app python disabled_unit)
    if(TARGET ${target})
        message(FATAL_ERROR "Disabled helper created target ${target}")
    endif()
endforeach()
'''})
    configure(args, source, build, '-DDISABLED_BUILD_APPS=OFF', '-DDISABLED_BUILD_TESTS=OFF',
              '-DDISABLED_BUILD_PYTHON=OFF', '-DDISABLED_INSTALL=OFF',
              '-DCMAKE_DISABLE_FIND_PACKAGE_Python=ON')
    run(args.cmake, '--build', str(build))
    assert test_names(args, build) == set()
    prefix = root / 'install'
    run(args.cmake, '--install', str(build), '--prefix', str(prefix))
    assert not prefix.exists() or not any(prefix.rglob('*'))
    assert not (build / 'disabled-config.cmake').exists()
    print('PASS: disabled helpers skip targets, tests, dependencies and invalid inputs')


def check_project_scopes(args: argparse.Namespace, root: Path) -> None:
    source = root / 'source'
    write(source, {
        'CMakeLists.txt': '''cmake_minimum_required(VERSION 3.30)
project(host VERSION 1.0.0 LANGUAGES CXX)
include("${NOM_KIT_PATH}/nom_kit.cmake")
nom_project_options()
enable_testing()
# This local link target isolates nom_c2test's executable/CTest behavior from Catch2 discovery.
add_library(catch_link INTERFACE)
add_library(Catch2::Catch2WithMain ALIAS catch_link)
nom_lib(host_library SOURCES value.cpp)
nom_app(host_app SOURCES main_test.cxx DEPS nom::host_library)
set_target_properties(host_app PROPERTIES OUTPUT_NAME app)
nom_c2test(unit SOURCES main_test.cxx DEPS nom::host_library)
nom_install_package(TARGETS host_library)
add_subdirectory(child)
if(NOT nom_build_apps OR NOT nom_build_tests OR NOT nom_install OR nom_build_python)
    message(FATAL_ERROR "Child options changed the host's directory variables")
endif()
nom_c2test(after_child SOURCES main_test.cxx DEPS nom::host_library)
''',
        'value.cpp': 'int option_value() { return 42; }\n',
        'main_test.cxx': 'int option_value();\nint main() { return option_value() == 42 ? 0 : 1; }\n',
        'child/CMakeLists.txt': '''project(child VERSION 1.0.0 LANGUAGES CXX)
nom_project_options()
nom_app(child_app SOURCES ../main_test.cxx DEPS nom::host_library)
if(TARGET child_app)
    set_target_properties(child_app PROPERTIES OUTPUT_NAME app)
endif()
nom_c2test(unit SOURCES ../main_test.cxx DEPS nom::host_library)
# Python remains disabled even though this helper is called unconditionally.
nom_python_module(child_python)
nom_install_package(TARGETS host_library)
''',
    })
    # The child install rules would duplicate the host export; disable them independently.
    for mode, options, expected in (
        ('default', [], {'host_unit', 'host_after_child'}),
        ('child_enabled', ['-DCHILD_BUILD_TESTS=ON', '-DCHILD_BUILD_APPS=ON'],
         {'host_unit', 'host_after_child', 'child_unit'}),
    ):
        build = root / mode
        configure(args, source, build, '-DCHILD_INSTALL=OFF', *options)
        run(args.cmake, '--build', str(build), '--parallel', '2')
        assert test_names(args, build) == expected
        run(args.ctest, '--test-dir', str(build), '--output-on-failure')
        assert (build / 'bin/app').is_file()
        assert (build / 'child/bin/app').is_file() == (mode == 'child_enabled')
        prefix = root / f'{mode}_install'
        run(args.cmake, '--install', str(build), '--prefix', str(prefix))
        assert list(prefix.rglob('host-config.cmake'))
        assert not list(prefix.rglob('child-config.cmake'))
        print(f'PASS: host option scope and {mode.replace("_", " ")} child controls')


def check_library_usage(args: argparse.Namespace, root: Path) -> None:
    source = root / 'source'
    write(source, {
        'CMakeLists.txt': '''cmake_minimum_required(VERSION 3.30)
project(source_usage VERSION 1.0.0 LANGUAGES CXX)
include("${NOM_KIT_PATH}/nom_kit.cmake")
nom_project_options()
nom_lib(value SOURCES value.cpp)
nom_install_package(TARGETS value)
enable_testing()
nom_library_usage_tests(consumer OPTIONS "-DNOM_KIT_PATH=${NOM_KIT_PATH}" -DSOURCE_USAGE_INSTALL=OFF)
''',
        'value.cpp': 'int source_value() { return 7; }\n',
        'consumer/CMakeLists.txt': '''cmake_minimum_required(VERSION 3.30)
project(source_consumer LANGUAGES CXX)
include("${NOM_KIT_PATH}/nom_kit.cmake")
nom_project_options()
if(LIBRARY_USAGE_MODE STREQUAL "package")
    find_package(source_usage CONFIG REQUIRED)
else()
    include(FetchContent)
    FetchContent_Declare(source_usage SOURCE_DIR "${LIBRARY_USAGE_SOURCE_DIR}")
    FetchContent_MakeAvailable(source_usage)
endif()
enable_testing()
nom_app(consumer SOURCES main_test.cxx DEPS nom::value)
add_test(NAME consumer COMMAND consumer)
''',
        'consumer/main_test.cxx': 'int source_value();\nint main() { return source_value() == 7 ? 0 : 1; }\n',
    })
    for install in ('OFF', 'ON'):
        build = root / install.lower()
        configure(args, source, build, f'-DSOURCE_USAGE_INSTALL={install}')
        run(args.cmake, '--build', str(build), '--parallel', '2')
        expected = {'source_usage_library_usage_source'}
        if install == 'ON':
            expected |= {'source_usage_library_usage_install', 'source_usage_library_usage_package'}
        assert test_names(args, build) == expected
        run(args.ctest, '--test-dir', str(build), '--output-on-failure')
        print(f'PASS: library usage builds and runs with installation {install.lower()}')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cmake', 'ctest', 'compiler', 'generator', 'kit'):
        parser.add_argument(f'--{name}', required=True)
    args = parser.parse_args()
    args.kit = Path(args.kit).resolve().as_posix()
    with TemporaryDirectory(prefix='nomisma-project-options-') as temporary:
        root = Path(temporary)
        check_disabled_helpers(args, root / 'disabled')
        check_project_scopes(args, root / 'scope')
        check_library_usage(args, root / 'usage')


if __name__ == '__main__':
    main()

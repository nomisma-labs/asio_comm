#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Check the labels that select tests in a build tree.

nom_c2test() labels its suite unit unless LABELS replaces that, and sets PROPERTIES on the test.
NOM_SKIP_TEST_LABELS leaves the tests carrying a listed label out of every ctest run in the build
tree, wherever they are registered and whatever other labels they carry.
"""

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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cmake', 'ctest', 'compiler', 'generator', 'kit'):
        parser.add_argument(f'--{name}', required=True)
    args = parser.parse_args()

    with TemporaryDirectory(prefix='nomisma-test-selection-') as temporary:
        root = Path(temporary)
        source, build = root / 'source', root / 'build'
        (source / 'child').mkdir(parents=True)
        # Tests in two directories: labeled by nom_c2test() or by hand, with two labels, and unlabeled.
        (source / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.30)
project(selection LANGUAGES CXX)
include("{Path(args.kit).resolve().as_posix()}/nom_kit.cmake")
nom_project_options()
enable_testing()
# A local link target stands in for Catch2, so the suites are plain programs.
add_library(catch_link INTERFACE)
add_library(Catch2::Catch2WithMain ALIAS catch_link)
nom_c2test(fast SOURCES main_test.cxx)
nom_c2test(slow SOURCES main_test.cxx LABELS integration gpu PROPERTIES TIMEOUT 7 ENVIRONMENT "A=1;B=2")
add_test(NAME unlabeled COMMAND "${{CMAKE_COMMAND}}" -E true)
add_subdirectory(child)
''')
        (source / 'main_test.cxx').write_text('int main() { return 0; }\n')
        (source / 'child/CMakeLists.txt').write_text('''add_test(NAME consumer COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(consumer PROPERTIES LABELS integration)
add_test(NAME tooling COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(tooling PROPERTIES LABELS "kit;python_package")
''')
        everything = {'selection_fast', 'selection_slow', 'unlabeled', 'consumer', 'tooling'}
        # Reconfiguring the same build tree changes the selection both ways.
        for skipped, expected in (('integration;kit', {'selection_fast', 'unlabeled'}), ('', everything),
                                  ('kit', everything - {'tooling'})):
            run(args.cmake, '-S', str(source), '-B', str(build), '-G', args.generator,
                f'-DCMAKE_CXX_COMPILER={args.compiler}', f'-DNOM_SKIP_TEST_LABELS={skipped}')
            run(args.cmake, '--build', str(build))
            listing = json.loads(run(args.ctest, '--test-dir', str(build), '--show-only=json-v1'))
            tests = {test['name']: {item['name']: item['value'] for item in test.get('properties', [])}
                     for test in listing['tests']}
            assert set(tests) == expected, f'NOM_SKIP_TEST_LABELS={skipped!r} selected {sorted(tests)}'
            run(args.ctest, '--test-dir', str(build), '--no-tests=error')
            print(f'PASS: NOM_SKIP_TEST_LABELS={skipped!r} runs {", ".join(sorted(expected))}')
            if not skipped:
                fast, slow = tests['selection_fast'], tests['selection_slow']
                assert fast['LABELS'] == ['unit'], fast
                assert sorted(slow['LABELS']) == ['gpu', 'integration'], slow
                assert slow['TIMEOUT'] == 7 and slow['ENVIRONMENT'] == ['A=1', 'B=2'], slow
                print('PASS: nom_c2test() labels a suite unit; LABELS replaces that; PROPERTIES sets the rest')


if __name__ == '__main__':
    main()

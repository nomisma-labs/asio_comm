#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Check app preset selection through real builds and installations."""

import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


def run(*args: str) -> None:
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{' '.join(args)}\n{result.stdout}\n{result.stderr}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--generator', required=True)
    parser.add_argument('--kit', required=True)
    args = parser.parse_args()

    with TemporaryDirectory(prefix='nomisma-app-presets-') as temporary:
        root = Path(temporary)
        source = root / 'source'
        source.mkdir()
        (source / 'main_test.cxx').write_text('int main() { return 0; }\n')
        (source / 'CMakeLists.txt').write_text(
            'cmake_minimum_required(VERSION 3.30)\n'
            'project(selection LANGUAGES CXX)\n'
            f'include("{Path(args.kit).as_posix()}/nom_kit.cmake")\n'
            'nom_project_options()\n'
            'add_subdirectory(shared)\n'
            'add_subdirectory(prototype)\n')
        fixtures = {
            # An excluded app comes first: later selected apps must still install shared config.
            'shared': [('release_only', 'release'), ('default_app', ''),
                       ('explicit_any', 'any'), ('dev_ci', 'dev ci'), ('future_app', 'research')],
            'prototype': [('prototype', 'complete')],
        }
        for folder, apps in fixtures.items():
            app_dir = source / folder
            (app_dir / 'config/nested').mkdir(parents=True)
            (app_dir / 'config/nested/settings.json').write_text('{"example": true}\n')
            (app_dir / 'CMakeLists.txt').write_text(''.join(
                f'nom_app({name} SOURCES ../main_test.cxx{f" BUILD_FOR {presets}" if presets else ""})\n'
                for name, presets in apps))

        common = {'default_app', 'explicit_any'}
        cases = {
            'default': common | {'dev_ci'},
            'dev': common | {'dev_ci'},
            'ci': common | {'dev_ci'},
            'release': common | {'release_only'},
            'complete': common | {'dev_ci', 'release_only', 'future_app', 'prototype'},
        }
        for preset, expected in cases.items():
            build = root / preset
            options = [] if preset == 'default' else [f'-DNOM_BUILD_PRESET={preset}']
            run(args.cmake, '-S', str(source), '-B', str(build), '-G', args.generator,
                f'-DCMAKE_CXX_COMPILER={args.compiler}', *options)
            run(args.cmake, '--build', str(build))
            actual = {p.stem for p in (build / 'bin').iterdir()}
            assert actual == expected, (preset, actual, expected)

            def check_install() -> None:
                prefix = build / 'install'
                if prefix.exists():
                    shutil.rmtree(prefix)
                run(args.cmake, '--install', str(build), '--prefix', str(prefix))
                assert {p.stem for p in (prefix / 'bin').iterdir()} == expected, preset
                for folder in fixtures:
                    installed = prefix / 'config' / folder / 'nested/settings.json'
                    if folder == 'shared' or preset == 'complete':
                        assert installed.read_bytes() == (source / folder / 'config/nested/settings.json').read_bytes()
                        manifest = (build / 'install_manifest.txt').read_text().splitlines()
                        assert manifest.count(str(installed)) == 1, (preset, folder)
                    else:
                        assert not installed.exists(), preset

            check_install()
            if preset == 'dev':
                run(args.cmake, '--build', str(build), '--target', 'prototype')
                assert (build / 'bin/prototype').exists()
                assert (build / 'config/prototype/nested/settings.json').is_file()
                check_install()  # Explicitly built excluded apps still stay out of installation.
            print(f'PASS: {preset} build, install, and config selection')


if __name__ == '__main__':
    main()

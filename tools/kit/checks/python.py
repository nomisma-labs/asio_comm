#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Check Python compatibility and imports without executing repository scripts."""

import sys

# Keep this guard usable by older Python interpreters too.
if sys.version_info < (3, 10):
    sys.exit("Python 3.10 or newer is required (scripts use annotations such as str | None); "
             "found " + sys.version.split()[0] + " at " + sys.executable)

import ast
import importlib
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def is_local(module: str, script: Path) -> bool:
    relative = Path(*module.split('.'))
    return any((base / relative).with_suffix('.py').is_file() or (base / relative).is_dir()
               for base in (script.parent, ROOT))


def main() -> int:
    print(f"Python {sys.version.split()[0]}: {sys.executable}")
    listed = subprocess.run(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', '*.py'],
        cwd=ROOT, capture_output=True, text=True, check=True).stdout.split('\0')
    scripts = sorted({ROOT / name for name in listed if name and (ROOT / name).is_file()})
    # Bootstrap also imports json in its inline CTest result reader.
    modules = {'json': {'bootstrap.sh'}}
    failures = []
    for script in scripts:
        relative = str(script.relative_to(ROOT))
        try:
            source = script.read_bytes()
            compile(source, str(script), 'exec')
            tree = ast.parse(source, filename=str(script))
        except (SyntaxError, UnicodeError, OSError) as error:
            failures.append(f'{relative}: {error}')
            continue
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                names = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
                names = [node.module]
            else:
                continue
            for name in names:
                if not is_local(name, script):
                    modules.setdefault(name, set()).add(relative)

    for name, users in sorted(modules.items()):
        try:
            importlib.import_module(name)
        except Exception as error:
            failures.append(f"cannot import {name} (used by {', '.join(sorted(users))}): {error}")
    for failure in failures:
        print(f'FAIL: {failure}', file=sys.stderr)
    if failures:
        return 1
    print(f'PASS: {len(scripts)} scripts compile; {len(modules)} required imports are available')
    return 0


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Check tools/kit/hooks on a scratch repo that has the hooks, the checks they run and .clang-format.

The scratch repo points core.hooksPath at tools/kit/hooks, as ./bootstrap.sh does, so each commit
runs the hooks. Formatting uses CLANG_FORMAT, or clang-format-22, like the checks.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

IDENTITY = ['-c', 'user.name=Hooks Test', '-c', 'user.email=hooks@example.com', '-c', 'init.defaultBranch=master']
HEADER = ('SPDX-FileCopyrightText: 2026 Alexey Tkachenko\n'
          'SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary\n')
COPIED = ['.clang-format', 'tools/kit/hooks/pre-commit', 'tools/kit/hooks/commit-msg',
          'tools/kit/checks/format.sh', 'tools/kit/checks/headers.py']


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(['git', *IDENTITY, *args], cwd=repo, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"git {' '.join(args)} in {repo}\n{result.stdout}\n{result.stderr}")
    return result.stdout.strip()


def commit(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(['git', *IDENTITY, 'commit', '--quiet', *args], cwd=repo, capture_output=True, text=True)


def check(condition: bool, message: str, result: subprocess.CompletedProcess) -> None:
    if not condition:
        raise RuntimeError(f'{message}\n--- stdout\n{result.stdout}\n--- stderr\n{result.stderr}')


def said(result: subprocess.CompletedProcess, *texts: str) -> bool:
    return all(text in result.stdout + result.stderr for text in texts)


def cpp(code: str, clang_format: list[str] | None = None) -> str:
    """A C++ file with the SPDX header; formatted when a clang-format command is given."""
    if clang_format:
        try:
            code = subprocess.run([*clang_format, '--assume-filename=file.cpp'], input=code,
                                  capture_output=True, text=True, check=True).stdout
        except FileNotFoundError:
            raise RuntimeError(f'{clang_format[0]} not found: install clang-format 22 or set CLANG_FORMAT') from None
    return ''.join(f'// {line}\n' for line in HEADER.splitlines()) + '\n' + code


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, help='the repo whose tools/kit/hooks to test')
    args = parser.parse_args()

    with TemporaryDirectory(prefix='nomisma-git-hooks-') as temporary:
        repo = Path(temporary)
        # The scratch repo's style, which the hooks check against, wherever the build directory is.
        clang_format = [os.environ.get('CLANG_FORMAT', 'clang-format-22'), f'--style=file:{repo / ".clang-format"}']
        git(repo, 'init', '--quiet')
        for name in COPIED:
            (repo / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(Path(args.source) / name, repo / name)  # keeps the executable bits
        git(repo, 'add', '--all')
        git(repo, 'commit', '--quiet', '-m', 'Add the hooks')
        git(repo, 'config', 'core.hooksPath', 'tools/kit/hooks')
        (repo / 'core').mkdir()
        one, two = repo / 'core/one.cpp', repo / 'core/two.cpp'

        # A formatted file with its header commits.
        one.write_text(cpp('int one( ){return 1;}\n', clang_format))
        git(repo, 'add', '--all')
        result = commit(repo, '-m', 'Add one')
        check(result.returncode == 0, 'a formatted file was rejected', result)

        # An unformatted file is rejected with the change it needs and the command that fixes it.
        two.write_text(cpp('int  two( ){return 2;}\n'))
        git(repo, 'add', '--all')
        result = commit(repo, '-m', 'Add two')
        check(result.returncode == 1 and git(repo, 'log', '-1', '--format=%s') == 'Add one',
              'an unformatted file was committed', result)
        check(said(result, '-int  two( ){return 2;}', '+int two()', '-i core/two.cpp && git add core/two.cpp',
                   'pre-commit (formatting)'), 'the rejection lacks the diff or the fix', result)

        # Fixing only the working copy doesn't help: the staged copy is what the commit gets.
        two.write_text(cpp('int two( ){return 2;}\n', clang_format))
        result = commit(repo, '-m', 'Add two')
        check(result.returncode == 1 and said(result, 'git add -p core/two.cpp'),
              'a commit passed on its working copy, or the fix ignores the unstaged changes', result)
        git(repo, 'add', '--all')
        result = commit(repo, '-m', 'Add two')
        check(result.returncode == 0, 'the restaged fix was rejected', result)

        # An unformatted change that isn't staged doesn't stop the staged one.
        one.write_text(cpp('int one( ){return 1;}\nint three( ){return 3;}\n', clang_format))
        git(repo, 'add', '--all')
        one.write_text(one.read_text() + 'int  four( ){return 4;}\n')
        result = commit(repo, '-m', 'Add three')
        check(result.returncode == 0, 'an unstaged change stopped the commit', result)
        check(git(repo, 'diff', '--name-only') == 'core/one.cpp', 'the unstaged change is gone', result)

        # commit -a stages the working copies first, and a rejection leaves nothing staged.
        result = commit(repo, '-a', '-m', 'Add four')
        check(result.returncode == 1 and said(result, '-int  four( ){return 4;}'),
              'commit -a skipped the working copy', result)
        check(not git(repo, 'diff', '--cached', '--name-only') and 'int  four' in one.read_text(),
              'a rejected commit -a left files staged or changed', result)
        one.write_text(cpp(one.read_text().split('\n\n', 1)[1], clang_format))
        result = commit(repo, '-a', '-m', 'Add four')
        check(result.returncode == 0, 'commit -a of formatted files was rejected', result)

        # Deleted files have nothing to check.
        git(repo, 'rm', '--quiet', 'core/two.cpp')
        result = commit(repo, '-m', 'Remove two')
        check(result.returncode == 0, 'a deletion was rejected', result)

        # A file without its SPDX header is rejected.
        (repo / 'tool.py').write_text("print('tool')\n")
        git(repo, 'add', '--all')
        result = commit(repo, '-m', 'Add a tool')
        check(result.returncode == 1 and said(result, 'missing SPDX header: tool.py', 'pre-commit (SPDX headers)'),
              'a file without its header was committed', result)
        git(repo, 'rm', '--quiet', '--cached', 'tool.py')
        (repo / 'tool.py').unlink()

        # Trailing whitespace is rejected, except in Markdown.
        (repo / 'extra.cmake').write_text(''.join(f'# {line}\n' for line in HEADER.splitlines()) + 'set(x 1)  \n')
        git(repo, 'add', '--all')
        result = commit(repo, '-m', 'Add extra.cmake')
        check(result.returncode == 1 and said(result, 'extra.cmake:3: trailing whitespace', 'pre-commit (whitespace)'),
              'trailing whitespace was committed', result)
        git(repo, 'rm', '--quiet', '--cached', 'extra.cmake')
        (repo / 'extra.cmake').unlink()
        (repo / 'notes.md').write_text('A hard line break  \nin Markdown.\n')
        git(repo, 'add', '--all')
        result = commit(repo, '-m', 'Add notes')
        check(result.returncode == 0, 'trailing spaces in Markdown were rejected', result)

        # Commit messages carry no Co-Authored-By trailers, in any case.
        (repo / 'notes.md').write_text('More notes.\n')
        git(repo, 'add', '--all')
        for trailer in ('Co-Authored-By: Someone <someone@example.com>', 'co-authored-by: Someone <someone@example.com>'):
            result = commit(repo, '-m', 'Update notes', '-m', trailer)
            check(result.returncode == 1 and said(result, 'commit-msg'), f'a message with "{trailer}" was accepted', result)
        result = commit(repo, '-m', 'Update notes')
        check(result.returncode == 0, 'a plain message was rejected', result)


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Check tools/kit/sync_template.py on a scratch template and scratch repos made from it.

The template's v1.1.0 updates, moves, deletes and adds template-owned files and has an upgrade
step. Each repo starts at v1.0.0 and syncs to the newest release.
"""

import argparse
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

IDENTITY = ['-c', 'user.name=Sync Test', '-c', 'user.email=sync@example.com', '-c', 'init.defaultBranch=master']


def git(cwd: Path, *args: str) -> str:
    result = subprocess.run(['git', *IDENTITY, *args], cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"git {' '.join(args)} in {cwd}\n{result.stdout}\n{result.stderr}")
    return result.stdout.strip()


def write(root: Path, files: dict[str, str]) -> None:
    for path, text in files.items():
        file = root / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(text)
        file.chmod(0o755 if path.endswith('.sh') else 0o644)


def release(template: Path, version: str) -> None:
    git(template, 'add', '--all')
    git(template, 'commit', '--quiet', '-m', f'Release {version}')
    git(template, 'tag', '-a', f'v{version}', '-m', f'Release {version}')


def make_template(root: Path, script: str) -> Path:
    template = root / 'template'
    template.mkdir()
    git(template, 'init', '--quiet')
    write(template, {
        'tools/kit/sync_template.py': script,
        'tools/kit/checks/format.sh': '#!/bin/sh\necho format\n',
        'cmake/kit/nom_kit.cmake': '# kit 1.0\n',
        'cmake/kit/helpers.cmake': ''.join(f'set(helper_{number} {number})\n' for number in range(20)),
        'tools/kit/tool.py': "print('tool 1.0')\n",
        'docs/kit/build.md': '# Build\n',
        'bootstrap.sh': '#!/bin/sh\necho 1.0\n',
        'CMakeLists.txt': 'project(template\n    VERSION 1.0.0\n    LANGUAGES CXX)\n',
        'CHANGELOG.md': '# Changelog\n\n## [1.0.0] - 2026-01-01\n',
        '.template_ref': 'url=\nsha=\n',
    })
    release(template, '1.0.0')
    write(template, {
        'cmake/kit/nom_kit.cmake': '# kit 1.1\n',
        'bootstrap.sh': '#!/bin/sh\necho 1.1\n',
        'tools/kit/added.py': "print('added')\n",
        'tools/kit/checks/tool.py': 'import sys\n\nsys.exit(0)\n',
        'CMakeLists.txt': 'project(template\n    VERSION 1.1.0\n    LANGUAGES CXX)\n',
        'CHANGELOG.md': '# Changelog\n\n## [1.1.0] - 2026-02-01\n\n### Upgrade\n\n- Call the moved helpers.\n\n'
                        '## [1.0.0] - 2026-01-01\n',
    })
    (template / 'cmake/kit/api').mkdir()
    git(template, 'mv', 'cmake/kit/helpers.cmake', 'cmake/kit/api/helpers.cmake')
    git(template, 'rm', '--quiet', 'docs/kit/build.md', 'tools/kit/tool.py')
    release(template, '1.1.0')
    return template


def make_repo(root: Path, template: Path, name: str) -> Path:
    """A repo made from the template's v1.0.0, with its own AGENTS.md naming two template paths."""
    repo = root / name
    repo.mkdir()
    git(repo, 'init', '--quiet')
    archive = subprocess.run(['git', 'archive', 'v1.0.0'], cwd=template, capture_output=True, check=True).stdout
    subprocess.run(['tar', '-x', '-C', str(repo)], input=archive, check=True)
    (repo / '.template_ref').write_text(f"url={template}\nsha={git(template, 'rev-parse', 'v1.0.0^{commit}')}\n")
    (repo / 'AGENTS.md').write_text('Helpers: cmake/kit/helpers.cmake. Build: docs/kit/build.md.\n')
    git(repo, 'add', '--all')
    git(repo, 'commit', '--quiet', '-m', 'Initialize')
    return repo


def sync(repo: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, 'tools/kit/sync_template.py', *args],
                          cwd=repo, capture_output=True, text=True)


def check(condition: bool, message: str, result: subprocess.CompletedProcess) -> None:
    if not condition:
        raise RuntimeError(f'{message}\n--- stdout\n{result.stdout}\n--- stderr\n{result.stderr}')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--script', required=True, help='the tools/kit/sync_template.py to test')
    args = parser.parse_args()

    with TemporaryDirectory(prefix='nomisma-template-sync-') as temporary:
        root = Path(temporary)
        template = make_template(root, Path(args.script).read_text())
        target = git(template, 'rev-parse', 'v1.1.0^{commit}')

        # A clean repo syncs: files updated, moved, deleted and added, and everything staged.
        repo = make_repo(root, template, 'clean')
        write(repo, {'tools/kit/mine.py': "print('mine')\n"})
        git(repo, 'add', '--all')
        git(repo, 'commit', '--quiet', '-m', 'Add a tool inside the kit')
        result = sync(repo)
        check(result.returncode == 0, 'syncing a clean repo failed', result)
        check((repo / 'cmake/kit/nom_kit.cmake').read_text() == '# kit 1.1\n', 'kit not updated', result)
        check((repo / 'cmake/kit/api/helpers.cmake').is_file() and not (repo / 'cmake/kit/helpers.cmake').exists(),
              'helper not moved', result)
        check(not (repo / 'docs/kit/build.md').exists(), 'removed doc kept', result)
        check((repo / 'tools/kit/added.py').is_file(), 'new tool missing', result)
        check(os.access(repo / 'bootstrap.sh', os.X_OK), 'bootstrap.sh lost its executable bit', result)
        check(f'sha={target}' in (repo / '.template_ref').read_text(), '.template_ref not updated', result)
        staged = set(git(repo, 'diff', '--cached', '--name-only', '--no-renames').splitlines())
        expected = {'.template_ref', 'bootstrap.sh', 'cmake/kit/nom_kit.cmake', 'cmake/kit/helpers.cmake',
                    'cmake/kit/api/helpers.cmake', 'docs/kit/build.md', 'tools/kit/added.py', 'tools/kit/tool.py',
                    'tools/kit/checks/tool.py'}
        check(staged == expected, f'staged {sorted(staged)}, expected {sorted(expected)}', result)
        check(not git(repo, 'diff', '--name-only'), 'unstaged changes left', result)
        for line in ('moved    cmake/kit/helpers.cmake -> cmake/kit/api/helpers.cmake',
                     'moved    tools/kit/tool.py -> tools/kit/checks/tool.py',
                     'AGENTS.md:1: cmake/kit/helpers.cmake -> cmake/kit/api/helpers.cmake',
                     'AGENTS.md:1: docs/kit/build.md -> removed',
                     '  tools/kit/mine.py',
                     '- Call the moved helpers.'):
            check(line in result.stdout, f'missing from the report: {line}', result)
        git(repo, 'commit', '--quiet', '-m', 'Sync')
        result = sync(repo)
        check(result.returncode == 0 and 'up to date' in result.stdout, 'a synced repo is not up to date', result)

        # Edited or deleted template-owned files stop the sync before anything changes.
        repo = make_repo(root, template, 'edited')
        write(repo, {'cmake/kit/nom_kit.cmake': '# kit 1.0, edited here\n'})
        (repo / 'tools/kit/checks/format.sh').unlink()
        git(repo, 'add', '--all')
        git(repo, 'commit', '--quiet', '-m', 'Edit template-owned files')
        head = git(repo, 'rev-parse', 'HEAD')
        result = sync(repo)
        check(result.returncode == 1, 'syncing over local edits succeeded', result)
        for line in ('cmake/kit/nom_kit.cmake: edited here', 'tools/kit/checks/format.sh: deleted here', '--overwrite'):
            check(line in result.stderr, f'missing from the refusal: {line}', result)
        check(not git(repo, 'status', '--porcelain') and git(repo, 'rev-parse', 'HEAD') == head,
              'a refused sync changed the repo', result)
        result = sync(repo, '--overwrite')
        check(result.returncode == 0, 'sync --overwrite failed', result)
        check((repo / 'cmake/kit/nom_kit.cmake').read_text() == '# kit 1.1\n', '--overwrite kept the edit', result)
        check((repo / 'tools/kit/checks/format.sh').is_file(), '--overwrite did not restore the deleted file', result)

        # Uncommitted changes, and the template itself, are refused.
        repo = make_repo(root, template, 'dirty')
        (repo / 'AGENTS.md').write_text('Changed.\n')
        result = sync(repo)
        check(result.returncode == 1 and 'uncommitted changes' in result.stderr, 'a dirty repo was synced', result)
        result = sync(template)
        check(result.returncode == 1 and 'records no template commit' in result.stderr,
              'the template synced itself', result)


if __name__ == '__main__':
    main()

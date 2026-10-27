#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Check tools/kit/checks/dependency_updates.py on scratch repos, without a network.

An upstream repo has releases v1.0.0 (an annotated tag) and v1.1.0, a pre-release and a newer
untagged commit. A scratch repo with a copy of the check records it as a submodule at each of them.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

IDENTITY = ['-c', 'user.name=Updates Test', '-c', 'user.email=updates@example.com', '-c', 'init.defaultBranch=master']
SCRIPT = 'tools/kit/checks/dependency_updates.py'


def git(repo: Path, *args: str) -> str:
    result = subprocess.run(['git', *IDENTITY, *args], cwd=repo, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"git {' '.join(args)} in {repo}\n{result.stdout}\n{result.stderr}")
    return result.stdout.strip()


def commit(repo: Path, message: str, *tag: str) -> str:
    git(repo, 'commit', '--quiet', '--allow-empty', '-m', message)
    if tag:
        git(repo, 'tag', *tag)
    return git(repo, 'rev-parse', 'HEAD')


def expect(repo: Path, status: int, last_line: str, *texts: str) -> None:
    result = subprocess.run([sys.executable, SCRIPT], cwd=repo, capture_output=True, text=True)
    lines = result.stdout.strip().splitlines()
    if result.returncode != status or not lines or lines[-1] != last_line or not all(text in result.stdout for text in texts):
        raise RuntimeError(f'expected exit {status} and "{last_line}"; got exit {result.returncode}\n'
                           f'--- stdout\n{result.stdout}\n--- stderr\n{result.stderr}')
    print(f'PASS: exit {status}, {last_line}')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, help='the repo whose dependency check to test')
    args = parser.parse_args()

    with TemporaryDirectory(prefix='nomisma-dependency-updates-') as temporary:
        root = Path(temporary)
        upstream, repo = root / 'upstream', root / 'repo'
        for path in (upstream, repo):
            path.mkdir()
            git(path, 'init', '--quiet')
        first = commit(upstream, 'one', '-a', 'v1.0.0', '-m', 'Release 1.0.0')
        second = commit(upstream, 'two', 'v1.1.0')
        commit(upstream, 'three', 'v1.2.0-rc1')
        untagged = commit(upstream, 'four')

        (repo / SCRIPT).parent.mkdir(parents=True)
        shutil.copy2(Path(args.source) / SCRIPT, repo / SCRIPT)
        expect(repo, 0, 'No git submodules to check')

        # Recording a gitlink in the index is all the check reads; nothing is cloned.
        git(repo, 'config', '--file', '.gitmodules', 'submodule.dep.path', 'external/dep')
        git(repo, 'config', '--file', '.gitmodules', 'submodule.dep.url', str(upstream))
        for recorded, status, last_line, text in (
            (first, 3, 'Newer: dep v1.1.0', 'dep (external/dep): v1.0.0; the newest release is v1.1.0'),
            (second, 0, 'Current: dep v1.1.0', 'dep (external/dep): v1.1.0, the newest release'),
            (untagged, 4, 'Not checked: dep', f'{untagged[:12]} is not a release; the newest release is v1.1.0'),
        ):
            git(repo, 'update-index', '--add', '--cacheinfo', f'160000,{recorded},external/dep')
            expect(repo, status, last_line, text)

        git(repo, 'update-index', '--add', '--cacheinfo', f'160000,{second},external/dep')
        git(repo, 'config', '--file', '.gitmodules', 'submodule.dep.url', str(root / 'missing'))
        expect(repo, 4, 'Not checked: dep')


if __name__ == '__main__':
    main()

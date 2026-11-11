#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT
"""Checks that every source file starts with the SPDX header (after an optional shebang).

Covers the C++, CMake, Python, shell, YAML and Docker files and the git hooks that git knows
about (tracked, or new and not ignored). Submodule contents are not ours and are skipped. JSON
can't carry comments and Markdown is documentation, so both are exempt. With --staged, checks
the staged copy of each staged file instead, as tools/kit/hooks/pre-commit does.
"""

import argparse
from collections.abc import Iterator
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tools.kit.project_license import POLICY_FILE, expected_license, load_policy, validate_notices

HEADER_TAGS = ["SPDX-FileCopyrightText", "SPDX-License-Identifier"]

MARKER_BY_SUFFIX = {
    ".h": "//", ".hpp": "//", ".ipp": "//", ".cpp": "//", ".cxx": "//", ".cppm": "//",
    ".cmake": "#", ".in": "#", ".py": "#", ".sh": "#", ".yml": "#", ".yaml": "#",
}
MARKER_BY_NAME = {"CMakeLists.txt": "#", "Dockerfile": "#", ".clang-tidy": "#", ".clangd": "#",
                  "pre-commit": "#", "commit-msg": "#"}


def comment_marker(path: Path) -> str | None:
    return MARKER_BY_NAME.get(path.name) or MARKER_BY_SUFFIX.get(path.suffix)


def has_header(text: str, marker: str) -> bool:
    lines = text.splitlines()
    if lines and lines[0].startswith("#!"):
        lines = lines[1:]
    return len(lines) >= len(HEADER_TAGS) and all(
        line.startswith(f"{marker} {tag}:") and line.split(":", 1)[1].strip()
        for line, tag in zip(lines, HEADER_TAGS))


def git(root: Path, *args: str) -> str:
    return subprocess.run(["git", *args], cwd=root, capture_output=True, encoding="utf-8", check=True).stdout


def working_files(root: Path) -> Iterator[tuple[str, str, str]]:
    """(path, comment marker, content) of each checked file in the working tree."""
    for name in git(root, "ls-files", "--cached", "--others", "--exclude-standard").splitlines():
        path = root / name
        marker = comment_marker(path)
        if marker and path.is_file():
            yield name, marker, path.read_text(encoding="utf-8")


def staged_files(root: Path) -> Iterator[tuple[str, str, str]]:
    """(path, comment marker, staged content) of each checked file that is staged."""
    # Raw entries are ":<old mode> <new mode> <old blob> <new blob> <status>" NUL <path> NUL.
    fields = git(root, "diff", "--cached", "--raw", "-z", "--no-abbrev", "--no-renames",
                 "--diff-filter=d").split("\0")
    for meta, name in zip(fields[0::2], fields[1::2]):
        _, mode, _, blob, _ = meta.split()
        marker = comment_marker(Path(name))
        if marker and mode in ("100644", "100755"):  # not a symlink or a submodule
            yield name, marker, git(root, "cat-file", "blob", blob)


def indexed_files(root: Path) -> Iterator[tuple[str, str, str]]:
    """A policy change must also validate unchanged sources against the new selection."""
    for entry in git(root, "ls-files", "--stage", "-z").split("\0"):
        if not entry:
            continue
        metadata, name = entry.split("\t", 1)
        mode, blob, stage = metadata.split()
        marker = comment_marker(Path(name))
        if marker and mode in ("100644", "100755") and stage == "0":
            yield name, marker, git(root, "cat-file", "blob", blob)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--staged", action="store_true", help="check the staged copy of each staged file")
    args = parser.parse_args()
    try:
        policy = load_policy(ROOT, args.staged)
        validate_notices(ROOT, policy, args.staged)
    except (OSError, ValueError) as error:
        print(f"license policy: {error}", file=sys.stderr)
        return 1
    if args.staged:
        changed_policy = git(ROOT, "diff", "--cached", "--name-only", "--", POLICY_FILE, "LICENSE").strip()
        files = list(indexed_files(ROOT) if changed_policy else staged_files(ROOT))
    else:
        files = list(working_files(ROOT))
    missing = [name for name, marker, text in files if not has_header(text, marker)]
    for name in missing:
        print(f"missing SPDX header: {name}")
    invalid = []
    for name, marker, text in files:
        if name in missing:
            continue
        lines = text.splitlines()[1:] if text.startswith("#!") else text.splitlines()
        actual = lines[1].split(":", 1)[1].strip()
        expected = expected_license(name, policy)
        if actual != expected:
            invalid.append(name)
            print(f"wrong SPDX license: {name}: expected {expected}, found {actual}")
    return 1 if missing or invalid else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT
"""Reports git submodules recorded at an older release than their upstream's newest.

    python3 tools/kit/checks/dependency_updates.py

Repos pin third-party code and our own repos as submodules in external/ (docs/kit/build.md#dependencies).
./bootstrap.sh runs this check in every repo and shows its result as a note. It asks each
submodule's remote for its release tags (vX.Y.Z or X.Y.Z, never pre-releases) and compares the
newest with the release the recorded commit is. A remote it can't reach, or a recorded commit that
isn't a release, is reported unchecked.

The last line of output is a summary. Exit status: 0 when nothing is newer, 3 when something is,
4 when nothing could be checked, 1 on errors. cmake_template's tools/toolchain_updates.py reports
compilers and other tools with the same module.
"""

import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
NEWER = 3  # exit status when something is newer
UNCHECKED = 4  # exit status when nothing could be checked
RELEASE_TAG = re.compile(r"v?(\d+(?:\.\d+)+)")  # v3.12.0 or 4.4.3; never v4.5.0-rc1

Version = tuple[int, ...]


def git_config(*args: str) -> str:
    """A git config value, or "" when it isn't set."""
    return subprocess.run(["git", "config", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def output(*command: str, timeout: float | None = None) -> str:
    # A remote that wants a password or a host key confirmation fails instead of waiting for one.
    environment = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
    if "GIT_SSH_COMMAND" not in environment and not git_config("--get", "core.sshCommand"):
        environment["GIT_SSH_COMMAND"] = "ssh -o BatchMode=yes"
    return subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=True,
                          timeout=timeout, env=environment).stdout


def version(text: str) -> Version:
    return tuple(int(part) for part in text.split("."))


class Report:
    """Prints one line per finding, then a summary line, and gives the exit status."""

    def __init__(self) -> None:
        self.current_items: list[str] = []
        self.newer: list[str] = []
        self.unchecked: list[str] = []

    def line(self, status: str, text: str) -> None:
        print(f"  {status:<10}{text}")

    def current(self, summary: str, text: str) -> None:
        self.current_items.append(summary)
        self.line("current", text)

    def update(self, summary: str, text: str, *details: str) -> None:
        self.newer.append(summary)
        self.line("newer", text)
        for detail in details:
            self.line("", detail)

    def skip(self, summary: str, text: str) -> None:
        self.unchecked.append(summary)
        self.line("unchecked", text)

    def finish(self) -> int:
        unchecked = f"; not checked: {', '.join(self.unchecked)}" if self.unchecked else ""
        if self.newer:
            print(f"\nNewer: {', '.join(self.newer)}{unchecked}")
            return NEWER
        if not self.current_items:
            print(f"\nNot checked: {', '.join(self.unchecked)}")
            return UNCHECKED
        print(f"\nCurrent: {', '.join(self.current_items)}{unchecked}")
        return 0


def release_tags(url: str) -> dict[Version, tuple[str, str]]:
    """The release tags of a remote repository: version -> (tag, commit)."""
    commits: dict[str, str] = {}
    versions: dict[Version, str] = {}
    for line in output("git", "ls-remote", "--tags", url, timeout=30).splitlines():
        sha, ref = line.split("\t")
        name = ref.removeprefix("refs/tags/")
        peeled = name.endswith("^{}")  # an annotated tag's commit follows the tag itself
        name = name.removesuffix("^{}")
        if match := RELEASE_TAG.fullmatch(name):
            if peeled or name not in commits:
                commits[name] = sha
            versions[version(match[1])] = name
    return {number: (name, commits[name]) for number, name in versions.items()}


def failure(error: subprocess.CalledProcessError | subprocess.TimeoutExpired) -> str:
    """The last line git printed, such as: Could not resolve host: github.com."""
    text = error.stderr.decode(errors="replace") if isinstance(error.stderr, bytes) else error.stderr or ""
    lines = text.strip().splitlines()
    return lines[-1] if lines else str(error)


def check_releases(report: Report, checks: list[tuple[str, str, str, str]]) -> None:
    """Compare each (name, label, remote URL, what the repo uses: a commit or a version) with the
    remote's newest release."""
    with ThreadPoolExecutor() as pool:
        futures = [pool.submit(release_tags, url) for _, _, url, _ in checks]
    for (name, label, _, used), future in zip(checks, futures):
        try:
            tags = future.result()
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
            report.skip(name, f"{label}: {failure(error)}")
            continue
        if not tags:
            report.skip(name, f"{label}: the remote has no release tags")
            continue
        newest_version = max(tags)
        newest = tags[newest_version][0]
        if match := RELEASE_TAG.fullmatch(used):
            current, release = version(match[1]), used
        else:
            # A submodule records a commit; the release tag that points at it names the version.
            current, release = next(((number, tag) for number, (tag, commit) in tags.items() if commit == used),
                                    ((), ""))
            if not release:
                report.skip(name, f"{label}: {used[:12]} is not a release; the newest release is {newest}")
                continue
        if current >= newest_version:
            report.current(f"{name} {release}", f"{label}: {release}, the newest release")
        else:
            report.update(f"{name} {newest}", f"{label}: {release}; the newest release is {newest}")


def submodules() -> list[tuple[str, str, str, str]]:
    """(name, label, URL, recorded commit) for each git submodule of the repo."""
    if not (ROOT / ".gitmodules").is_file():
        return []
    entries = git_config("--file", ".gitmodules", "--get-regexp", r"^submodule\..*\.(path|url)$").splitlines()
    settings = dict(entry.split(" ", 1) for entry in entries)
    checks = []
    for key, path in settings.items():
        if not key.endswith(".path"):
            continue
        section = key.removesuffix(".path")
        # .gitmodules may hold a relative URL; initializing the submodule recorded the resolved one.
        url = git_config("--get", f"{section}.url") or settings.get(f"{section}.url", "")
        staged = output("git", "ls-files", "--stage", "--", path).split()
        if url and len(staged) > 1:
            checks.append((Path(path).name, f"{Path(path).name} ({path})", url, staged[1]))
    return checks


def main() -> int:
    print("Dependency updates: each git submodule against its upstream's releases\n")
    checks = submodules()
    if not checks:
        print("No git submodules to check")
        return 0
    report = Report()
    check_releases(report, checks)
    return report.finish()


if __name__ == "__main__":
    sys.exit(main())

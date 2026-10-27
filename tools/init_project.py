#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Turns a fresh copy of cmake_template into a new, empty repo.

    python3 tools/init_project.py <name> [--template-url <url>] [--no-commit]

Run it once, right after creating the repo from the template (docs/kit/new_repo.md). It deletes the
template's example code, lab and template-only files instead of renaming them, so the new repo
starts empty and grows through the recipes in docs/kit/howto.md. In the files that remain it removes
the lines and blocks marked template-only, sets the name and version 0.1.0 in project(), puts
the name and a TODO description into README.md and AGENTS.md, and starts an empty CHANGELOG.md.
It checks everything first, and stops without changing anything if the repo doesn't look like
a fresh copy of the template.
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

from kit.sync_template import TEMPLATE_OWNED, read_template_ref, write_template_ref

ROOT = Path(__file__).resolve().parent.parent
TEMPLATE_PROJECT = "cmake_template"
# The example code, the lab and the template-only files. Every path must exist.
REMOVED = [
    "core/example",
    "apps/hello",
    "apps/boost_example",
    "tests/unit/example",
    "tests/integration/library_usage",
    "tests/kit",
    "python",
    "pyproject.toml",
    "docs/lab",
    "docker",
    ".github/workflows/ci_image.yml",
    ".github/workflows/ci_template.yml",
    "tools/toolchain_updates.py",
    "tools/init_project.py",
]
REMOVED_SUBMODULES = ["external/nlohmann_json"]
SANDBOX = "apps/sandbox"  # every experiment in it is removed too
# `# template-only` or `<!-- template-only -->` at the end of a line removes the line; a
# `template-only:begin` ... `template-only:end` pair of such comments removes the lines between.
MARKER = re.compile(r"(?:#|<!--)\s*template-only(?::(begin|end))?\s*(?:-->)?\s*$")
DESCRIPTION = re.compile(r"(<!-- repo-description:begin -->\n).*?(<!-- repo-description:end -->)", re.DOTALL)


class InitError(Exception):
    pass


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()


def is_removed(relative: str) -> bool:
    return any(relative == path or relative.startswith(path + "/")
               for path in [*REMOVED, *REMOVED_SUBMODULES, SANDBOX + "/"])


def is_template_owned(relative: str) -> bool:
    return any(relative == path or relative.startswith(path + "/") for path in TEMPLATE_OWNED)


def check_fresh_copy(name: str) -> None:
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name) or name == TEMPLATE_PROJECT:
        raise InitError(f"'{name}' is not a snake_case name other than '{TEMPLATE_PROJECT}'")
    missing = [path for path in REMOVED if not (ROOT / path).exists()]
    missing += [path for path in REMOVED_SUBMODULES if not git("ls-files", "--", path)]
    if missing:
        raise InitError("not a fresh copy of cmake_template (already initialized?): missing " + ", ".join(missing))
    origin = subprocess.run(["git", "remote", "get-url", "origin"], cwd=ROOT, capture_output=True, text=True).stdout
    if re.search(r"[/:]cmake_template(\.git)?/?$", origin.strip()):
        raise InitError("this is cmake_template itself (see its origin remote); run the script in a repo created from it")
    if git("status", "--porcelain"):
        raise InitError("the work tree has uncommitted changes; commit or stash them first")


def strip_markers(relative: str, text: str) -> str:
    kept, begin = [], None
    for number, line in enumerate(text.splitlines(keepends=True), start=1):
        match = MARKER.search(line)
        if match and match.group(1) == "begin":
            if begin:
                raise InitError(f"{relative}:{number}: template-only:begin inside the block from line {begin}")
            begin = number
        elif match and match.group(1) == "end":
            if not begin:
                raise InitError(f"{relative}:{number}: template-only:end without a begin")
            begin = None
        elif not match and not begin:
            kept.append(line)
    if begin:
        raise InitError(f"{relative}:{begin}: template-only:begin without an end")
    return re.sub(r"\n{3,}", "\n\n", "".join(kept)).rstrip("\n") + "\n"


def set_project(text: str, name: str) -> str:
    calls = list(re.finditer(rf"^project\(\s*{TEMPLATE_PROJECT}\b(?P<args>[^)]*)\)", text, re.MULTILINE))
    if len(calls) != 1:
        raise InitError(f"CMakeLists.txt: expected one project({TEMPLATE_PROJECT} ...) call")
    args, versions = re.subn(r"\bVERSION\s+\S+", "VERSION 0.1.0", calls[0]["args"])
    args, descriptions = re.subn(r'\bDESCRIPTION\s+"[^"]*"', f'DESCRIPTION "{name}"', args)
    if versions != 1 or descriptions != 1:
        raise InitError("CMakeLists.txt: project() needs one VERSION and one DESCRIPTION")
    return text[:calls[0].start()] + f"project({name}{args})" + text[calls[0].end():]


def set_description(relative: str, text: str, description: str) -> str:
    text, count = DESCRIPTION.subn(lambda match: match[1] + description + "\n" + match[2], text)
    if count != 1:
        raise InitError(f"{relative}: expected one repo-description block")
    return text


def set_title(text: str, name: str) -> str:
    if not text.startswith("# "):
        raise InitError("README.md: expected a '# ' title on the first line")
    return f"# {name}\n" + text.split("\n", 1)[1]


def start_changelog(text: str) -> str:
    preamble, sections = text.split("\n## ", 1) if "\n## " in text else (text, "")
    if not sections:
        raise InitError("CHANGELOG.md: expected a preamble followed by '## ' sections")
    return preamble.rstrip("\n") + "\n\n## [Unreleased]\n"


def planned_edits(name: str) -> dict[str, str]:
    """The new text of every file that changes, worked out before anything is touched."""
    edits = {}
    for relative in git("ls-files").splitlines():
        path = ROOT / relative
        if is_removed(relative) or not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue  # binary
        if any(MARKER.search(line) for line in text.splitlines()):
            if is_template_owned(relative):
                raise InitError(f"{relative}: template-owned files must not carry template-only markers")
            edits[relative] = strip_markers(relative, text)

    def edit(relative: str, change) -> None:
        edits[relative] = change(edits.get(relative) or (ROOT / relative).read_text(encoding="utf-8"))

    edit("CMakeLists.txt", lambda text: set_project(text, name))
    edit("README.md", lambda text: set_description("README.md", set_title(text, name),
                                                   f"TODO: what {name} is, in one paragraph."))
    edit("AGENTS.md", lambda text: set_description("AGENTS.md", text,
                                                   f"TODO: what `{name}` is and what it depends on."))
    edit("CHANGELOG.md", start_changelog)
    return edits


def remove_template_content() -> None:
    for relative in REMOVED_SUBMODULES:
        git("rm", "-r", "--quiet", relative)
    gitmodules = ROOT / ".gitmodules"
    if gitmodules.exists() and not gitmodules.read_text().strip():
        git("rm", "--quiet", "--force", ".gitmodules")  # its only change is the staged removal above
    for relative in REMOVED:
        path = ROOT / relative
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
    for experiment in (ROOT / SANDBOX).iterdir():
        if experiment.is_dir():
            shutil.rmtree(experiment)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name", help="snake_case repo name, for example assembly")
    parser.add_argument("--template-url", help="template repo (default: url in .template_ref)")
    parser.add_argument("--no-commit", action="store_true", help="leave the changes uncommitted")
    args = parser.parse_args()

    try:
        check_fresh_copy(args.name)
        edits = planned_edits(args.name)
    except InitError as error:
        print(f"error: {error}; nothing was changed", file=sys.stderr)
        return 1

    url = args.template_url or read_template_ref(ROOT).get("url", "")
    if not url:
        print("error: set url in .template_ref to the hosted cmake_template clone URL, "
              "or pass --template-url; nothing was changed", file=sys.stderr)
        return 1
    try:
        sha = git("ls-remote", url, "HEAD").split()[0]
    except (subprocess.CalledProcessError, IndexError):
        print(f"error: could not resolve the template commit at '{url}'; "
              "nothing was changed", file=sys.stderr)
        return 1

    try:
        remove_template_content()
        for relative, text in edits.items():
            (ROOT / relative).write_text(text, encoding="utf-8")
        write_template_ref(ROOT, url, sha)
        if not args.no_commit:
            git("add", "--all")
            git("commit", "--quiet", "-m", f"Initialize {args.name} from cmake_template@{sha[:12] or 'unknown'}")
    except (OSError, subprocess.CalledProcessError) as error:
        detail = (getattr(error, "stderr", None) or str(error)).strip()
        # The work tree was clean when the script started, so this restores it exactly.
        print(f"error: {detail}\nthe repo is half-initialized; restore it with "
              "`git reset --hard && git clean -fd`, then run the script again", file=sys.stderr)
        return 1
    print(f"initialized '{args.name}', an empty repo: add code with docs/kit/howto.md, "
          "build with cmake --workflow --preset dev")
    return 0


if __name__ == "__main__":
    sys.exit(main())

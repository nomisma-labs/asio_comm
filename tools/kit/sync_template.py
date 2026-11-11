#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT
"""Brings this repo's template-owned files up to a cmake_template release.

    python3 tools/kit/sync_template.py [--to <tag or commit>] [--overwrite] [--template-url <url>]

Syncs the repo of the current directory from the template commit recorded in .template_ref to
the newest release tag, or --to. Each template-owned file (TEMPLATE_OWNED) that is unchanged
here since the recorded commit is replaced, added or deleted to match the target; nothing else
in the repo is touched. If any of them was edited here, nothing changes: the script lists them
and the ways forward. Otherwise it stages the result together with .template_ref, lists files of
this repo that still mention moved or removed template paths, and prints the template's upgrade
steps for the releases in between. Review with `git diff --cached`, run ./bootstrap.sh, commit.

The target release's copy of this script does the work, so the file list and the rules always
come from the release being synced to (docs/kit/new_repo.md#keeping-repos-in-sync).
"""

import argparse
import ast
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# The template-owned paths: identical in every repo and changed only in cmake_template. Keep the
# list literal; newer copies of this script read it from older template commits.
TEMPLATE_OWNED = [
    "bootstrap.sh",
    "cmake/kit",
    "tools/kit",
    "docs/kit",
    ".clang-format",
    ".clang-tidy",
    ".clangd",
    ".editorconfig",
    ".github/workflows/ci.yml",
]
TEMPLATE_REF = ".template_ref"
# Repo files that may mention old template paths as history.
HISTORY = ["CHANGELOG.md", TEMPLATE_REF]

# A file's state as git records it: (mode, blob sha), or None when the file doesn't exist.
State = tuple[str, str] | None


class SyncError(Exception):
    pass


def git(*args: str, cwd: Path) -> str:
    result = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    if result.returncode:
        raise SyncError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout.strip()


def show(clone: Path, commit: str, path: str) -> str | None:
    result = subprocess.run(["git", "show", f"{commit}:{path}"], cwd=clone, capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else None


def read_template_ref(root: Path) -> dict[str, str]:
    values = {}
    for line in (root / TEMPLATE_REF).read_text().splitlines():
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def write_template_ref(root: Path, url: str, sha: str) -> None:
    path = root / TEMPLATE_REF
    comments = [line for line in path.read_text().splitlines() if line.startswith("#")]
    path.write_text("\n".join([*comments, f"url={url}", f"sha={sha}"]) + "\n")


def is_under(path: str, entries: list[str]) -> bool:
    return any(path == entry or path.startswith(entry + "/") for entry in entries)


def check_clean(root: Path) -> None:
    if git("status", "--porcelain", "--untracked-files=no", cwd=root):
        raise SyncError("the work tree has uncommitted changes; commit or stash them first")
    untracked = git("status", "--porcelain", "--untracked-files=all", "--", *TEMPLATE_OWNED, cwd=root)
    if untracked:
        raise SyncError("untracked files in template-owned paths; commit, move or delete them first:\n" + untracked)


def release_label(clone: Path, sha: str) -> str:
    tags = git("tag", "--list", "v*", "--points-at", sha, cwd=clone).split()
    return f"{tags[0]} ({sha[:12]})" if tags else sha[:12]


def resolve_target(clone: Path, to: str | None) -> str:
    if not to:
        tags = git("tag", "--list", "v*", "--sort=-v:refname", cwd=clone).split()
        if not tags:
            raise SyncError("the template has no release tags; pass --to <commit>")
        to = tags[0]
    result = subprocess.run(["git", "rev-parse", "--verify", "--quiet", f"{to}^{{commit}}"],
                            cwd=clone, capture_output=True, text=True)
    if result.returncode:
        raise SyncError(f"'{to}' is not a tag or commit of the template")
    return result.stdout.strip()


def files_at(clone: Path, commit: str) -> dict[str, tuple[str, str]]:
    """Every file of the template at commit: path -> (mode, blob sha)."""
    files = {}
    for record in git("ls-tree", "-r", "-z", commit, cwd=clone).split("\0"):
        if record:
            meta, path = record.split("\t", 1)
            mode, kind, sha = meta.split()
            if kind == "blob":
                files[path] = (mode, sha)
    return files


def owned_at(clone: Path, commit: str) -> tuple[list[str], list[str]]:
    """The template-owned paths, and the template-only files among them, as commit defined them."""
    for script in ("tools/kit/sync_template.py", "tools/sync_template.py"):
        source = show(clone, commit, script)
        if source is not None:
            break
    else:
        raise SyncError(f"template commit {commit[:12]} has no sync script")
    lists = {node.targets[0].id: node.value for node in ast.parse(source).body
             if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)}
    owned = ast.literal_eval(lists["TEMPLATE_OWNED"])
    return owned, ast.literal_eval(lists["TEMPLATE_ONLY"]) if "TEMPLATE_ONLY" in lists else []


def local_state(root: Path, path: str) -> State:
    file = root / path
    if not file.is_file():
        return None
    content = file.read_bytes()
    mode = "100755" if os.access(file, os.X_OK) else "100644"
    return mode, hashlib.sha1(b"blob %d\0" % len(content) + content).hexdigest()


def conflict(before: State, after: State, here: State) -> str:
    if here is None:
        return "deleted here"
    if before is None:
        return "this repo has its own file where the template adds one"
    if after is None:
        return "edited here, and the template removes it"
    return "file mode changed here" if here[1] == before[1] else "edited here"


def plan(root: Path, clone: Path, base: str, target: str):
    """Compares this repo with the template at base and target, without changing anything.

    Returns the changes to make, as (path, state at base, state at target), and the conflicts,
    with a reason added. A path the template owned at base but now keeps as an ordinary file
    belongs to the repo from then on, so it is left alone.
    """
    base_owned, base_only = owned_at(clone, base)
    before_all, after_all = files_at(clone, base), files_at(clone, target)
    paths = {path for path in after_all if is_under(path, TEMPLATE_OWNED)}
    paths |= {path for path in before_all if is_under(path, base_owned)
              and path not in base_only and path not in after_all}
    changes, conflicts = [], []
    for path in sorted(paths):
        before, after, here = before_all.get(path), after_all.get(path), local_state(root, path)
        if here == after:
            continue
        if here == before:
            changes.append((path, before, after))
        else:
            conflicts.append((path, before, after, conflict(before, after, here)))
    return changes, conflicts


def apply(root: Path, clone: Path, changes) -> None:
    for path, _, after in changes:
        file = root / path
        if after is None:
            file.unlink(missing_ok=True)
            folder = file.parent
            while folder != root and not any(folder.iterdir()):
                folder.rmdir()
                folder = folder.parent
        else:
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(subprocess.run(["git", "cat-file", "blob", after[1]], cwd=clone,
                                            capture_output=True, check=True).stdout)
            file.chmod(0o755 if after[0] == "100755" else 0o644)


def moves(clone: Path, base: str, target: str) -> dict[str, str]:
    """Template-owned files that the template moved between base and target: old -> new.

    Git's rename detection pairs moved files. A file that was also rewritten pairs with the only
    added file of the same name, if there is one.
    """
    base_owned, _ = owned_at(clone, base)
    renamed, deleted, added = {}, [], []
    for line in git("diff", "--name-status", "-M", base, target, "--", *base_owned, *TEMPLATE_OWNED,
                    cwd=clone).splitlines():
        status, *paths = line.split("\t")
        if status.startswith("R"):
            renamed[paths[0]] = paths[1]
        elif status == "D":
            deleted.append(paths[0])
        elif status == "A":
            added.append(paths[0])
    for path in deleted:
        namesakes = [new for new in added if Path(new).name == Path(path).name]
        if len(namesakes) == 1:
            renamed[path] = namesakes[0]
    return renamed


def stale_references(root: Path, removed: list[str], renamed: dict[str, str]) -> list[str]:
    """Lines in this repo's own files that mention template-owned paths the sync removed."""
    if not removed:
        return []
    patterns = [arg for path in removed for arg in ("-e", path)]
    excluded = [f":(exclude){path}" for path in [*TEMPLATE_OWNED, *HISTORY]]
    found = subprocess.run(["git", "grep", "-n", "-F", *patterns, "--", ".", *excluded],
                           cwd=root, capture_output=True, text=True).stdout
    lines = []
    for match in found.splitlines():
        file, number, text = match.split(":", 2)
        for path in removed:
            if path in text:
                lines.append(f"{file}:{number}: {path} -> {renamed.get(path, 'removed')}")
    return lines


def project_version(clone: Path, commit: str) -> tuple[int, ...]:
    call = re.search(r"^project\(([^)]*)\)", show(clone, commit, "CMakeLists.txt") or "", re.MULTILINE)
    version = re.search(r"\bVERSION\s+(\d+)\.(\d+)\.(\d+)", call[1]) if call else None
    return tuple(int(part) for part in version.groups()) if version else (0, 0, 0)


def upgrade_steps(clone: Path, base: str, target: str) -> list[tuple[str, str]]:
    """The `### Upgrade` sections of the template's changelog after base, up to target, oldest first."""
    first, last = project_version(clone, base), project_version(clone, target)
    steps = []
    for section in re.split(r"^## \[", show(clone, target, "CHANGELOG.md") or "", flags=re.MULTILINE)[1:]:
        version, _, body = section.partition("]")
        if re.fullmatch(r"\d+\.\d+\.\d+", version) and first < tuple(map(int, version.split("."))) <= last:
            upgrade = re.search(r"^### Upgrade\n(.*?)(?=^##|\Z)", body, re.MULTILINE | re.DOTALL)
            if upgrade and upgrade[1].strip():
                steps.append((version, upgrade[1].strip()))
    return steps[::-1]


def sync(root: Path, clone: Path, target: str, url: str | None, overwrite: bool) -> int:
    """Syncs root to target, using a clone of the template. Run by the target's copy of the script."""
    check_clean(root)
    ref = read_template_ref(root)
    base, url = ref.get("sha", ""), url or ref.get("url", "")
    if subprocess.run(["git", "cat-file", "-e", f"{base}^{{commit}}"], cwd=clone, capture_output=True).returncode:
        raise SyncError(f"the template commit in {TEMPLATE_REF}, {base[:12]}, is not in {url}")
    changes, conflicts = plan(root, clone, base, target)
    if conflicts and not overwrite:
        print(f"Nothing was changed. These template-owned files differ here from cmake_template "
              f"{release_label(clone, base)}:", file=sys.stderr)
        for path, _, _, reason in conflicts:
            print(f"  {path}: {reason}", file=sys.stderr)
        print("Move those changes into cmake_template, or into files that belong to this repo, then "
              "rerun.\nTo take the template's versions instead, rerun with --overwrite; the current "
              "content stays in git history.", file=sys.stderr)
        return 1
    changes += [(path, before, after) for path, before, after, _ in conflicts]
    apply(root, clone, changes)
    write_template_ref(root, url, target)
    written = [path for path, _, after in changes if after is not None]
    removed = [path for path, _, after in changes if after is None]
    if written:
        git("add", "--", *written, cwd=root)
    if removed:
        git("rm", "--cached", "--quiet", "--ignore-unmatch", "--", *removed, cwd=root)
    git("add", "--", TEMPLATE_REF, cwd=root)

    renamed = moves(clone, base, target)
    replaced = {path for path, *_ in conflicts}
    moved = {path: renamed[path] for path in removed if path not in replaced and renamed.get(path) in written}
    print(f"Synced cmake_template {release_label(clone, base)} -> {release_label(clone, target)}:")
    for path, before, after in changes:
        if path in moved:
            print(f"  moved    {path} -> {moved[path]}")
        elif path not in moved.values() or path in replaced:
            action = ("replaced" if path in replaced else "added" if before is None
                      else "deleted" if after is None else "updated")
            print(f"  {action:<8} {path}")
    if not changes:
        print("  no template-owned file changed")
    print(f"Staged {len(changes)} file(s) and {TEMPLATE_REF}.")

    after_all = files_at(clone, target)
    owned_dirs = [entry for entry in TEMPLATE_OWNED if any(path.startswith(entry + "/") for path in after_all)]
    foreign = [path for path in git("ls-files", "--", *owned_dirs, cwd=root).splitlines() if path not in after_all]
    if foreign:
        print("\nThese files of this repo are inside template-owned folders; move them out, for example to tools/:")
        print("".join(f"  {path}\n" for path in foreign), end="")
    references = stale_references(root, removed, renamed)
    if references:
        print("\nThese files of this repo still mention template paths that moved or were removed:")
        print("".join(f"  {line}\n" for line in references), end="")
    steps = upgrade_steps(clone, base, target)
    print("\nUpgrade steps from cmake_template's changelog:" if steps else
          "\nNo upgrade steps in cmake_template's changelog.")
    for version, text in steps:
        print(f"  {version}:\n" + "".join(f"    {line}\n" for line in text.splitlines()), end="")
    print("\nReview with `git diff --cached`, run ./bootstrap.sh, then commit.")
    return 0


def hand_off(root: Path, to: str | None, template_url: str | None, overwrite: bool) -> int:
    """Clones the template and runs the target release's copy of this script on this repo."""
    ref = read_template_ref(root)
    url = template_url or ref.get("url", "")
    if not ref.get("sha"):
        raise SyncError(f"{TEMPLATE_REF} records no template commit: this is cmake_template itself, "
                        "or a repo that tools/init_project.py didn't initialize")
    if not url:
        raise SyncError(f"{TEMPLATE_REF} has no url; pass --template-url")
    check_clean(root)
    with tempfile.TemporaryDirectory(prefix="sync_template-") as temporary:
        clone = Path(temporary) / "template"
        git("clone", "--quiet", "--no-checkout", url, str(clone), cwd=root)
        target = resolve_target(clone, to)
        if target == ref["sha"]:
            print(f"up to date with cmake_template {release_label(clone, target)}")
            return 0
        script = show(clone, target, "tools/kit/sync_template.py")
        if script is None:
            raise SyncError(f"{release_label(clone, target)} predates tools/kit/sync_template.py; "
                            "sync to v0.5.0 or later")
        worker = Path(temporary) / "sync_template.py"
        worker.write_text(script)
        # Keep these arguments stable: older copies of the script pass them to newer ones.
        command = [sys.executable, str(worker), "--from-clone", str(clone), "--to", target]
        command += ["--overwrite"] if overwrite else []
        command += ["--template-url", url] if template_url else []
        return subprocess.run(command, cwd=root).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--to", help="template release tag or commit (default: the newest release tag)")
    parser.add_argument("--overwrite", action="store_true",
                        help="also replace template-owned files that were edited here")
    parser.add_argument("--template-url", help="template repo (default: url in .template_ref)")
    parser.add_argument("--from-clone", help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        root = Path(git("rev-parse", "--show-toplevel", cwd=Path.cwd()))
        if args.from_clone:
            return sync(root, Path(args.from_clone), args.to, args.template_url, args.overwrite)
        return hand_off(root, args.to, args.template_url, args.overwrite)
    except SyncError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

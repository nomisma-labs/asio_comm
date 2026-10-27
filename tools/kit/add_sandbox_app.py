#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Adds a lab experiment.

    python3 tools/kit/add_sandbox_app.py <name>

Creates apps/sandbox/<name>/ with a main.cpp and a CMakeLists.txt that builds it as
bin/sandbox_<name>, linked to the repo's main library if it has one. apps/sandbox/CMakeLists.txt
registers every experiment it finds. Experiments use BUILD_FOR complete, so the complete preset builds them.
They are git-ignored, so they stay in the clone; git add -f commits one that is worth sharing.
Write down what an experiment shows (in cmake_template: docs/lab/).
"""

import argparse
import re
import sys
from pathlib import Path

from checks.headers import HEADER_LINES

ROOT = Path(__file__).resolve().parents[2]


def header(marker: str) -> str:
    return "".join(f"{marker} {line}\n" for line in HEADER_LINES)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("name", help="snake_case experiment name")
    args = parser.parse_args()

    name = args.name
    if not re.fullmatch(r"[a-z][a-z0-9_]*", name):
        print(f"error: '{name}' is not a snake_case name", file=sys.stderr)
        return 1
    sandbox = ROOT / "apps" / "sandbox"
    experiment = sandbox / name
    if experiment.exists():
        print(f"error: {experiment.relative_to(ROOT)} already exists", file=sys.stderr)
        return 1
    project = re.search(r"^project\(\s*(\w+)", (ROOT / "CMakeLists.txt").read_text(), re.MULTILINE).group(1)
    # The main library, when the repo has one, is core/<project> (docs/kit/howto.md#a-library).
    deps = f" DEPS nom::{project}" if (ROOT / "core" / project).is_dir() else ""

    experiment.mkdir()
    (experiment / "CMakeLists.txt").write_text(
        header("#") + f"\nnom_app(sandbox_{name} SOURCES main.cpp{deps} BUILD_FOR complete)\n")
    (experiment / "main.cpp").write_text(
        header("//") + "\n#include <cstdio>\n\nint main()\n{\n"
        f'    std::puts("sandbox: {name}");\n    return 0;\n}}\n')
    print(f"added {experiment.relative_to(ROOT)} (git-ignored): build/complete/bin/sandbox_{name} "
          "after cmake --workflow --preset complete")
    return 0


if __name__ == "__main__":
    sys.exit(main())

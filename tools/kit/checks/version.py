#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Prints the repo's version, or checks a release tag against it.

    python3 tools/kit/checks/version.py            # prints the version in project()
    python3 tools/kit/checks/version.py v1.2.0     # fails unless the tag is v<version>

The version lives only in the project() call of the top-level CMakeLists.txt, and releases are
tagged v<version> (docs/kit/build.md#versioning). CI runs the check on every tag push.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def project_version() -> str:
    calls = re.findall(r"^project\(([^)]*)\)", (ROOT / "CMakeLists.txt").read_text(), re.MULTILINE)
    version = re.search(r"\bVERSION\s+(\d+\.\d+\.\d+)\s", calls[0] + " ") if len(calls) == 1 else None
    if not version:
        sys.exit("error: CMakeLists.txt needs one project() call with VERSION <major>.<minor>.<patch>")
    return version[1]


def main() -> int:
    version = project_version()
    if len(sys.argv) < 2:
        print(version)
        return 0
    tag = sys.argv[1]
    if tag != f"v{version}":
        print(f"error: tag {tag} doesn't match project() version {version}; expected v{version}", file=sys.stderr)
        return 1
    print(f"tag {tag} matches project() version {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

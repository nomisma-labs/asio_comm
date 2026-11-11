#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: MIT
"""Repository-owned license selection; copied infrastructure keeps its MIT notices."""

import json
from pathlib import Path, PurePosixPath
import subprocess

from tools.kit.sync_template import TEMPLATE_OWNED, is_under

POLICY_FILE = "project_license.json"
LICENSES = {"MIT": "MIT", "SUL-1.0": "LicenseRef-Sustainable-Use-1.0",
            "proprietary": "LicenseRef-Nomisma-Labs-Proprietary"}
KIT_COPYRIGHT = "2026 Alexey Tkachenko"


def read_file(root: Path, name: str, staged: bool = False) -> str:
    if not staged:
        return (root / name).read_text(encoding="utf-8")
    result = subprocess.run(["git", "show", f":{name}"], cwd=root, capture_output=True, text=True)
    if result.returncode:
        raise ValueError(f"{name} is missing from the index; stage it with the license changes")
    return result.stdout


def parse_policy(text: str) -> dict:
    policy = json.loads(text)
    if not isinstance(policy, dict) or set(policy) != {"license", "copyright", "mit_files"}:
        raise ValueError(f"{POLICY_FILE} needs license, copyright and mit_files")
    if policy["license"] not in LICENSES.values():
        raise ValueError(f"unsupported project license: {policy['license']}")
    owner = policy["copyright"]
    if not isinstance(owner, str) or not owner.strip() or "\n" in owner or "\r" in owner:
        raise ValueError("copyright must be a nonempty single line")
    paths = policy["mit_files"]
    if not isinstance(paths, list):
        raise ValueError("mit_files must be a list of repository-relative file paths")
    for name in paths:
        if (not isinstance(name, str) or not name or "\\" in name
                or PurePosixPath(name).is_absolute() or ".." in PurePosixPath(name).parts
                or PurePosixPath(name).as_posix() != name or name == "."):
            raise ValueError(f"invalid mit_files path: {name!r}")
    return policy


def load_policy(root: Path, staged: bool = False) -> dict:
    try:
        return parse_policy(read_file(root, POLICY_FILE, staged))
    except FileNotFoundError:
        raise ValueError(f"missing {POLICY_FILE}; select this repository's license explicitly "
                         "(docs/kit/new_repo.md#licensing)") from None


def expected_license(name: str, policy: dict) -> str:
    return "MIT" if is_under(name, TEMPLATE_OWNED) or name in policy["mit_files"] else policy["license"]


def header(marker: str, policy: dict) -> str:
    return (f"{marker} SPDX-FileCopyrightText: {policy['copyright']}\n"
            f"{marker} SPDX-License-Identifier: {policy['license']}\n")


def project_license_text(root: Path, policy: dict) -> str:
    license_id, owner = policy["license"], policy["copyright"]
    if license_id == "MIT":
        terms = (root / "cmake/kit/LICENSE").read_text().replace(KIT_COPYRIGHT, owner)
    elif license_id == LICENSES["SUL-1.0"]:
        terms = (f"Copyright (c) {owner}\n\n"
                 + (root / "tools/kit/licenses/LicenseRef-Sustainable-Use-1.0.txt").read_text())
    else:
        terms = (f"Copyright (c) {owner}. All rights reserved.\n\n"
                 "This software and its documentation are proprietary. No permission is granted "
                 "to use, copy, modify, or distribute them without prior written consent from "
                 "the copyright holder.\n")
    if license_id != "MIT":
        terms = ("These terms apply to project-owned material. Copied toolchain files and the\n"
                 "scaffold files listed in project_license.json retain their MIT license;\n"
                 "see cmake/kit/LICENSE. Third-party material retains its own terms.\n\n" + terms)
    return terms


def validate_notices(root: Path, policy: dict, staged: bool = False) -> None:
    """Check the project notice and the separately distributed toolchain notice."""
    notice = read_file(root, "LICENSE", staged)
    if policy["copyright"] not in notice:
        raise ValueError("LICENSE does not name the configured copyright holder")
    marker = {"MIT": "Permission is hereby granted", LICENSES["SUL-1.0"]: "Sustainable Use License",
              LICENSES["proprietary"]: "proprietary"}[policy["license"]]
    if marker not in notice:
        raise ValueError("LICENSE does not match the configured project license")
    mit = read_file(root, "cmake/kit/LICENSE", staged)
    if KIT_COPYRIGHT not in mit or "Permission is hereby granted" not in mit:
        raise ValueError("cmake/kit/LICENSE must retain the toolchain's MIT notice")

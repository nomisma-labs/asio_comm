#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
"""Reports compilers and tools newer than the ones cmake_template uses.

    python3 tools/toolchain_updates.py

cmake_template moves the toolchain first, so ./bootstrap.sh runs this template-only script and
shows its result as a note; docs/lab/toolchain.md#keeping-the-toolchain-current is what to do
about it. The script reads what the repo uses from the presets and the kit, and compares it with:

- Ubuntu packages, from apt's package lists: newer g++-N and clang-N packages than the presets
  name, a newer Boost than the one installed, and newer candidates for installed toolchain packages;
- upstream release tags, through git ls-remote: the Catch2 release the kit requires, and CMake.
  Without a network these are reported unchecked;
- the Catch2 builds here use: an installed one the kit accepts, or else a download per build tree;
- the CMake and Catch2 the CI image pins in docker/ci/Dockerfile, which should match.

Every repo's bootstrap checks its git submodules with tools/kit/checks/dependency_updates.py, whose
report this script shares. The last line of output is a summary. Exit status: 0 when nothing is
newer, 3 when something is, 4 when nothing could be checked, 1 on errors.
"""

import glob
import json
import re
import shutil
import sys
import time
from pathlib import Path

from kit.checks.dependency_updates import ROOT, Report, Version, check_releases, output, version

APT_UPDATED = Path("/var/lib/apt/periodic/update-success-stamp")
CMAKE_REPOSITORY = "https://github.com/Kitware/CMake.git"


def dotted(numbers: Version) -> str:
    return ".".join(map(str, numbers))


def upstream(package_version: str) -> str:
    """A Debian version without its epoch and revision: 1:22.1.2-1ubuntu1 is 22.1.2."""
    return re.sub(r"^\d+:", "", package_version).rsplit("-", 1)[0]


def preset_compilers() -> dict[str, int]:
    """The oldest major version of each compiler the presets name: {'g++': 16, 'clang++': 19}."""
    presets = json.loads((ROOT / "cmake/kit/presets.json").read_text())
    majors: dict[str, int] = {}
    for preset in presets["configurePresets"]:
        compiler = preset.get("cacheVariables", {}).get("CMAKE_CXX_COMPILER", "")
        if match := re.fullmatch(r"(g\+\+|clang\+\+)-(\d+)", compiler):
            majors[match[1]] = min(int(match[2]), majors.get(match[1], int(match[2])))
    return majors


def files_naming(pattern: re.Pattern[str]) -> list[str]:
    """The tracked files that mention pattern, except the changelog's history."""
    names = output("git", "ls-files").splitlines()
    return [name for name in names if name != "CHANGELOG.md" and (ROOT / name).is_file()
            and pattern.search((ROOT / name).read_text(errors="ignore"))]


def apt_versions(prefix: str, pattern: str) -> list[Version]:
    """The versions in the names of the packages apt knows that match pattern, oldest first."""
    names = output("apt-cache", "pkgnames", prefix).split()
    return sorted(version(match[1]) for name in names if (match := re.fullmatch(pattern, name)))


def apt_policy(*packages: str) -> dict[str, tuple[str | None, str | None]]:
    """The installed and candidate versions of each package apt knows; None where it has none."""
    versions: dict[str, tuple[str | None, str | None]] = {}
    name = None
    for line in output("apt-cache", "policy", *packages).splitlines():
        if not line.startswith(" ") and line.endswith(":"):
            name = line[:-1]
        elif name and (match := re.match(r"\s+(Installed|Candidate): (\S+)", line)):
            installed, candidate = versions.get(name, (None, None))
            value = None if match[2] == "(none)" else match[2]
            versions[name] = (value, candidate) if match[1] == "Installed" else (installed, value)
    return versions


def check_compiler(report: Report, name: str, package: str, pinned: int, mentions: str) -> None:
    """Report packages of a newer major version than the presets name, such as clang-22.

    mentions matches the ways files name the pinned version, to list the files an update changes.
    """
    majors = [major for (major,) in apt_versions(f"{package}-", rf"{re.escape(package)}-(\d+)")]
    newer = [major for major in majors if major > pinned]
    candidates = apt_policy(*(f"{package}-{major}" for major in [pinned, *newer]))
    newer = [major for major in newer if candidates.get(f"{package}-{major}", (None, None))[1]]
    if not newer:
        installed = candidates.get(f"{package}-{pinned}", (None, None))[0]
        report.current(f"{name} {pinned}", f"{name} {pinned}, the newest {package} package"
                       + (f" ({package}-{pinned} {upstream(installed)} installed)" if installed else ""))
        return
    newest = f"{package}-{newer[-1]}"
    files = files_naming(re.compile(mentions))
    report.update(f"{name} {newer[-1]}",
                  f"{name}: the presets use {pinned}; Ubuntu packages {', '.join(map(str, newer))} "
                  f"({newest} {upstream(candidates[newest][1] or '')})",
                  f"files naming {name} {pinned}: {', '.join(files)}")


def check_packages(report: Report, compilers: dict[str, int]) -> None:
    if not shutil.which("apt-cache"):
        report.skip("Ubuntu packages", "Ubuntu packages: apt-cache is missing")
        return
    stamp = APT_UPDATED if APT_UPDATED.exists() else Path("/var/lib/apt/lists")
    days = int((time.time() - stamp.stat().st_mtime) // 86400)
    updated = time.strftime("%Y-%m-%d", time.localtime(stamp.stat().st_mtime))
    print(f"Ubuntu packages (apt package lists from {updated}"
          + (f", {days} days old; sudo apt update refreshes them)" if days > 7 else ")"))
    if gcc := compilers.get("g++"):
        check_compiler(report, "GCC", "g++", gcc, rf"\bg\+\+-{gcc}\b|\bgcc-{gcc}\b|\bGCC {gcc}\b")
    if clang := compilers.get("clang++"):
        # clang-N, clang++-N, clangd-N, clang-tidy-N, run-clang-tidy-N and so on.
        check_compiler(report, "Clang", "clang", clang, rf"\bclang[a-z+-]*-{clang}\b|\bClang {clang}\b")

    boost = apt_versions("libboost1.", r"libboost(1\.\d+)-dev")
    installed = apt_policy("libboost-dev").get("libboost-dev", (None, None))[0]
    if boost and installed:
        current = version(".".join(upstream(installed).split(".")[:2]))
        if boost[-1] > current:
            report.update(f"Boost {dotted(boost[-1])}",
                          f"Boost: {dotted(current)} installed; Ubuntu packages {dotted(boost[-1])} "
                          f"(libboost{dotted(boost[-1])}-dev)")
        else:
            report.current(f"Boost {dotted(current)}", f"Boost {dotted(current)}, the newest Boost package")

    # The installed packages of the toolchain table in docs/kit/build.md.
    tools = [f"g++-{gcc}"] if gcc else []
    if clang:
        tools += [f"{tool}-{clang}" for tool in ("clang", "clang-tools", "clang-tidy", "clang-format", "clangd")]
    tools += ["cmake", "ninja-build", "libboost-dev", "python3"]
    upgrades = [f"{name} {upstream(installed)} -> {upstream(candidate)}"
                for name, (installed, candidate) in apt_policy(*tools).items()
                if installed and candidate and installed != candidate]
    if upgrades:
        report.update("apt upgrades", f"apt upgrades for installed tools: {'; '.join(upgrades)}")
    else:
        report.current("apt packages", "installed tools match apt's candidates (" + ", ".join(tools) + ")")


def installed_cmake() -> str | None:
    """The version of the cmake on PATH, such as 4.4.4."""
    cmake = shutil.which("cmake")
    installed = re.search(r"cmake version (\d+(?:\.\d+)+)", output(cmake, "--version")) if cmake else None
    return installed[1] if installed else None


def kit_catch2() -> tuple[str, str, str] | None:
    """(repository, release tag, minimum version) of the Catch2 that cmake/kit/deps/catch2.cmake requires."""
    kit = (ROOT / "cmake/kit/deps/catch2.cmake").read_text()
    url = re.search(r"GIT_REPOSITORY\s+(\S+)", kit)
    tag = re.search(r"GIT_TAG\s+(\S+)", kit)
    minimum = re.search(r"FIND_PACKAGE_ARGS\s+(\d+(?:\.\d+)*)", kit)
    return (url[1], tag[1], minimum[1]) if url and tag and minimum else None


def check_tools(report: Report, cmake: str | None, catch2: tuple[str, str, str] | None) -> None:
    print("Upstream releases (git ls-remote)")
    checks = []  # (name, label, url, the version the repo uses)
    if catch2:
        checks.append(("Catch2", "Catch2 the kit requires (cmake/kit/deps/catch2.cmake)", catch2[0], catch2[1]))
    if cmake:
        checks.append(("CMake", "CMake installed", CMAKE_REPOSITORY, cmake))
    check_releases(report, checks)


def check_catch2(report: Report, catch2: tuple[str, str, str] | None) -> None:
    """The Catch2 builds use: an installed package the kit accepts, or else a download per build tree."""
    if not catch2:
        return
    print("Catch2 builds use")
    minimum = version(catch2[2])
    installed = []
    for directory in glob.glob("/usr/local/lib/cmake/Catch2") + glob.glob("/usr/local/lib/*/cmake/Catch2") + \
            glob.glob("/usr/lib/cmake/Catch2") + glob.glob("/usr/lib/*/cmake/Catch2"):
        config = Path(directory) / "Catch2ConfigVersion.cmake"
        if match := re.search(r'set\(PACKAGE_VERSION "([\d.]+)"\)', config.read_text() if config.is_file() else ""):
            installed.append((version(match[1]), directory))
    # The package accepts the same major version at or above the requested one.
    usable = sorted((number, directory) for number, directory in installed
                    if number >= minimum and number[0] == minimum[0])
    if usable:
        number, directory = usable[-1]
        report.current(f"Catch2 {dotted(number)} installed", f"Catch2 {dotted(number)} from {directory}")
    else:
        found = ", ".join(f"{dotted(number)} in {directory}" for number, directory in sorted(installed)) or "none"
        report.update(f"Catch2 {catch2[2]} not installed",
                      f"no installed Catch2 {catch2[2]} or newer (found: {found}); each build tree downloads and "
                      f"builds {catch2[1]}", "docs/kit/build.md#toolchain installs it")


def check_ci_image(report: Report, cmake: str | None, catch2: tuple[str, str, str] | None) -> None:
    """The CI image pins its CMake package and builds its Catch2, which should match here and the kit."""
    dockerfile = (ROOT / "docker/ci/Dockerfile").read_text()
    print("CI image (docker/ci/Dockerfile)")
    if (pin := re.search(r"^ARG CMAKE_VERSION=(\S+)", dockerfile, re.MULTILINE)) and cmake:
        pinned = upstream(pin[1])
        if pinned == cmake:
            report.current(f"CI image CMake {pinned}", f"CMake {pinned}, as installed here")
        else:
            report.update(f"CI image CMake {cmake}", f"CMake {pinned} ({pin[1]}); installed here: {cmake}")
    if (pin := re.search(r"^ARG CATCH2_VERSION=(\S+)", dockerfile, re.MULTILINE)) and catch2:
        wanted = catch2[1].removeprefix("v")
        if pin[1] == wanted:
            report.current(f"CI image Catch2 {wanted}", f"Catch2 {wanted}, as the kit requires")
        else:
            report.update(f"CI image Catch2 {wanted}", f"Catch2 {pin[1]}; the kit requires {wanted}")


def main() -> int:
    print("Toolchain updates for cmake_template (docs/lab/toolchain.md#keeping-the-toolchain-current)\n")
    report = Report()
    check_packages(report, preset_compilers())
    cmake = installed_cmake()
    catch2 = kit_catch2()
    check_tools(report, cmake, catch2)
    check_catch2(report, catch2)
    check_ci_image(report, cmake, catch2)
    return report.finish()


if __name__ == "__main__":
    sys.exit(main())

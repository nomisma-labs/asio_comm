# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary

"""Build and install native wheels without access to their original native libraries."""

import argparse
from email.parser import Parser
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile


def run(*command, **kwargs):
    subprocess.run(command, check=True, **kwargs)


def clean_environment():
    environment = os.environ.copy()
    for name in tuple(environment):
        if name.startswith(("PYTHON", "PIP_", "SKBUILD_", "CMAKE_")) or name in (
                "LD_LIBRARY_PATH", "LD_PRELOAD", "DYLD_LIBRARY_PATH",
                "DYLD_FALLBACK_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES", "VIRTUAL_ENV"):
            environment.pop(name)
    environment["CMAKE_BUILD_PARALLEL_LEVEL"] = "2"
    environment["PIP_DISABLE_PIP_VERSION_CHECK"] = "1"
    environment["PIP_NO_CACHE_DIR"] = "1"
    return environment


def build_wheel(args, source, output, environment, *definitions):
    run(args.python, "-m", "build", "--wheel", "--no-isolation", "--outdir", str(output),
        f"-Ccmake.define.CMAKE_CXX_COMPILER={args.compiler}",
        f"-Ccmake.define.CMAKE_MAKE_PROGRAM={args.ninja}",
        *[f"-Ccmake.define.{definition}" for definition in definitions],
        str(source), env=environment)
    wheels = list(output.glob("*.whl"))
    if len(wheels) != 1:
        raise AssertionError(f"Expected one wheel in {output}, found {wheels}")
    return wheels[0]


def check_artifact(wheel, distribution, package, library, version):
    with zipfile.ZipFile(wheel) as archive:
        names = archive.namelist()
        if f"{package}/__init__.py" not in names:
            raise AssertionError(f"Missing Python package {package} in {wheel}")
        if not any(name.startswith(f"{package}/_native.") and name.endswith(".so")
                   for name in names):
            raise AssertionError(f"Missing native extension in {wheel}")
        if not any(name.startswith(f"{package}/lib{library}.so") for name in names):
            raise AssertionError(f"Missing bundled shared library {library} in {wheel}")
        unexpected = [name for name in names
                      if not name.startswith(f"{package}/")
                      and ".dist-info/" not in name]
        if unexpected:
            raise AssertionError(f"Wheel contains unrelated C++ installation files: {unexpected}")
        metadata_name, = [name for name in names if name.endswith(".dist-info/METADATA")]
        wheel_name, = [name for name in names if name.endswith(".dist-info/WHEEL")]
        metadata = Parser().parsestr(archive.read(metadata_name).decode())
        wheel_metadata = Parser().parsestr(archive.read(wheel_name).decode())
        if metadata["Name"] != distribution or metadata["Version"] != version:
            raise AssertionError(f"Unexpected package metadata: {metadata}")
        if wheel_metadata["Root-Is-Purelib"] != "false":
            raise AssertionError("A native wheel must not be marked as pure Python")
        _, python_tag, abi_tag, platform_tag = wheel.stem.rsplit("-", 3)
        if not python_tag.startswith("cp") or abi_tag == "none" or platform_tag == "any":
            raise AssertionError(f"Native wheel has unsuitable compatibility tags: {wheel.name}")
        filename_tag = f"{python_tag}-{abi_tag}-{platform_tag}"
        if filename_tag not in wheel_metadata.get_all("Tag", []):
            raise AssertionError("Wheel metadata compatibility tags do not match its filename")


def install_and_check(args, wheel, root, environment, package, distribution, version, expression):
    target = root / "installed-env"
    run(args.python, "-m", "venv", "--without-pip", str(target), env=environment)
    interpreter = target / "bin/python"
    run(args.python, "-m", "pip", "--python", str(interpreter), "install", "--no-index",
        "--no-deps", "--no-compile", str(wheel), env=environment)
    outside = root / "outside-checkout"
    outside.mkdir()
    code = f'''
import importlib
import importlib.metadata
from pathlib import Path
import sys

package = importlib.import_module({package!r})
assert Path(package.__file__).resolve().is_relative_to(Path(sys.prefix).resolve())
assert importlib.metadata.version({distribution!r}) == {version!r}
{expression}
'''
    run(str(interpreter), "-I", "-c", code, cwd=outside, env=environment)


def check_example(args, root, environment):
    source = root / "source"
    shutil.copytree(args.source, source,
                    ignore=shutil.ignore_patterns(".git", "build", ".cache", "__pycache__", "*.pyc"))
    project = (source / "CMakeLists.txt").read_text()
    version = re.search(r"project\s*\(\s*cmake_template\s+VERSION\s+([0-9.]+)", project)[1]
    wheel = build_wheel(args, source, root / "dist", environment)
    check_artifact(wheel, "nomisma-cmake-example", "nomisma_example", "example", version)
    # Delete the copied sources, CMake build tree and original shared libraries before loading.
    shutil.rmtree(source)
    install_and_check(args, wheel, root, environment, "nomisma_example", "nomisma-cmake-example",
                      version, '''
assert package.counter_value(0) == 0
assert package.counter_value(7) == 7
try:
    package.counter_value(-1)
except ValueError:
    pass
else:
    raise AssertionError("Negative counter values must be rejected")
''')


def check_imported(args, root, environment):
    source = root / "source"
    shutil.copytree(Path(__file__).parent / "imported", source)
    kit = root / "kit"
    shutil.copytree(Path(args.source) / "cmake/kit", kit)
    prefix = root / "provider-install"
    build = root / "provider-build"
    run(args.cmake, "-S", str(source / "provider"), "-B", str(build), "-G", "Ninja",
        f"-DCMAKE_CXX_COMPILER={args.compiler}", f"-DCMAKE_MAKE_PROGRAM={args.ninja}",
        f"-DNOM_KIT_PATH={kit}", f"-DCMAKE_INSTALL_PREFIX={prefix}", env=environment)
    run(args.cmake, "--build", str(build), "--parallel", "2", env=environment)
    run(args.cmake, "--install", str(build), env=environment)
    check_rejected_import(args, source, root / "missing-runtime", kit, prefix, environment,
                          "must be listed in RUNTIME_LIBRARIES", "-DWHEEL_TEST_OMIT_RUNTIME=ON")
    wheel = build_wheel(args, source / "wrapper", root / "dist", environment,
                        f"NOM_KIT_PATH={kit}", f"CMAKE_PREFIX_PATH={prefix}")
    check_artifact(wheel, "nomisma-imported-example", "nom_imported_example", "wheel_provider", "1.0.0")
    # An independently installed dependency must not retain paths from the build machine.
    run(args.cmake, "-S", str(source / "provider"), "-B", str(build),
        f"-DCMAKE_INSTALL_RPATH={prefix / 'lib'}", env=environment)
    run(args.cmake, "--build", str(build), "--parallel", "2", env=environment)
    run(args.cmake, "--install", str(build), env=environment)
    check_rejected_import(args, source, root / "absolute-rpath", kit, prefix, environment,
                          "unsupported runtime path")
    for directory in (source, kit, prefix, build):
        shutil.rmtree(directory)
    install_and_check(args, wheel, root, environment, "nom_imported_example",
                      "nomisma-imported-example", "1.0.0", "assert package.answer() == 42")


def check_rejected_import(args, source, build, kit, prefix, environment, diagnostic, *definitions):
    result = subprocess.run(
        [args.cmake, "-S", str(source / "wrapper"), "-B", str(build), "-G", "Ninja",
         f"-DCMAKE_CXX_COMPILER={args.compiler}", f"-DCMAKE_MAKE_PROGRAM={args.ninja}",
         f"-DPython_EXECUTABLE={args.python}", f"-DNOM_KIT_PATH={kit}",
         "-DIMPORTED_WHEEL_BUILD_PYTHON=ON", "-DIMPORTED_WHEEL_INSTALL=OFF",
         f"-DCMAKE_PREFIX_PATH={prefix}", *definitions],
        capture_output=True, text=True, env=environment)
    output = result.stdout + result.stderr
    if result.returncode == 0 or diagnostic not in " ".join(output.split()):
        raise AssertionError(f"Expected configure failure containing {diagnostic!r}:\n{output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "python", "cmake", "compiler", "ninja"):
        parser.add_argument(f"--{name}", required=True)
    args = parser.parse_args()
    environment = clean_environment()
    with tempfile.TemporaryDirectory(prefix="nom-python-wheel-") as directory:
        for name, check in (("example", check_example), ("imported", check_imported)):
            root = Path(directory) / name
            root.mkdir()
            check(args, root, environment)
            print(f"PASS: {name} wheel builds, installs and runs without its original native libraries",
                  flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

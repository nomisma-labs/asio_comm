# cmake_template

<!-- repo-description:begin -->
The template every Nomisma C++ repo is created from, and the lab where build-system and C++
features (modules first) are tried before real repos rely on them.
<!-- repo-description:end -->

## Build

Install the tools listed in [docs/kit/build.md](docs/kit/build.md#toolchain), then:

```bash
./bootstrap.sh                   # fresh builds, tests and checks for every preset but release
cmake --workflow --preset dev    # configure, build and test one preset
```

Everyday commands, the presets and what bootstrap checks: [docs/kit/build.md](docs/kit/build.md).

## Docs

Agents start at [AGENTS.md](AGENTS.md), people at [docs/](docs/README.md). Adding a library, an
app, tests or a dependency: [docs/kit/howto.md](docs/kit/howto.md). Releases: [CHANGELOG.md](CHANGELOG.md).

<!-- template-only:begin -->
## A new repo from the template

On GitHub, "Use this template" (or `gh repo create --template`), then
`python3 tools/init_project.py <name>`, which removes the example code and the lab. See
[docs/kit/new_repo.md](docs/kit/new_repo.md).

## What's inside

The CMake project and installed package are named `cmake_template`. The illustrative library
is named `example` (`nom::example`); its code and tests exercise the build system.

- **Example library `example`** (`core/example/`): the native named module `nom.example`, with
  interface partitions, a template, compiled implementation units, a config-like type, and a
  macro header. JSON parsing uses a third-party header behind the module API.
- **Apps:** `hello` through `import nom.example;`
  (`build/dev/bin/hello build/dev/config/hello/settings.json`); `boost_example`, which adds
  Boost's program options, filesystem and gzip streams (`build/dev/bin/boost_example --help`);
  and lab experiments in `apps/sandbox/`.
- **Tests:** `tests/unit/` holds Catch2 suites that import the module; `tests/integration/`
  checks library usage from a separate project and config installation; `tests/kit/` checks the
  kit and its tools with throwaway projects and repos. They are labeled `unit`, `integration` and
  `kit`: `ctest --preset dev` runs the unit tests, `ctest --preset complete` all of them, and
  `ctest --preset complete -L kit` one label.
- **Python package:** the optional `nomisma_example` wheel exposes the example C++ API and
  bundles its shared library. [Opt-in checks](#example-wheel-checks) cover source and
  installed-library builds.
- **Build system:** presets, the CMake kit (`cmake/kit/`), dependencies as submodules in
  `external/`, and Boost from system packages.
- **Checks and CI:** SPDX headers, formatting, naming and the release-tag check, on a shared CI
  image. Validate a newly initialized repo locally with `./bootstrap.sh`.
- **Lab:** [docs/lab/](docs/lab/README.md).

## Example wheel checks

The checks build the `nomisma_example` wheels, install them in fresh virtual environments and
call the native API outside the source and build trees, without the original native artifacts.
They also check metadata and bundled runtime libraries. The `ci-template` workflow runs them;
locally, create the [packaging environment](docs/kit/build.md#native-python-wheels), then:

```bash
cmake --preset ci -DCMAKE_TEMPLATE_TEST_PYTHON_PACKAGE=ON \
  -DNOM_PYTHON_EXECUTABLE="$PWD/build/python-env/bin/python"
ctest --preset ci -R python_package --output-on-failure
```
<!-- template-only:end -->

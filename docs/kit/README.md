# Shared docs

The docs every Nomisma repo gets from `cmake_template`, which owns them;
`tools/kit/sync_template.py` keeps them in step. A repo's own docs go in `docs/<repo>/`,
outside the kit and without a layer directory.

- [agents.md](agents.md): the rules for agents in every repo; each `AGENTS.md` imports it.
- [conventions.md](conventions.md): naming, files, includes, modules, CMake names and how they are
  enforced.
- [build.md](build.md): build commands, toolchain, presets, the CMake kit, dependencies, modules,
  optional native Python wheels, tests, install, versioning, checks and CI.
- [howto.md](howto.md): recipes for adding a native module library, an app, importing tests, a
  Python package, a dependency or an experiment.
- [new_repo.md](new_repo.md): creating a repo from `cmake_template` and keeping it in sync.

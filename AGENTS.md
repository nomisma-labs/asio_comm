# AGENTS.md

<!-- repo-description:begin -->
This is **cmake_template**: the template every Nomisma C++ repo is created from, and the lab
where build-system and C++ features (modules first) are tried before real repos rely on them.
Its example code (library `example`, apps `hello` and `boost_example`, their tests and the
library usage test project) shows every mechanism working; `tools/init_project.py` removes it
from new repos.
<!-- repo-description:end -->

Read and follow the rules every Nomisma repo shares: @docs/kit/agents.md

Rules for this repo alone follow below.

<!-- template-only:begin -->
## This repo

- Lab work: experiments go in `apps/sandbox/`, findings in `docs/lab/`. An experiment a finding
  cites gets a `!` exception in the template-only block of `.gitignore`.
- End a line that new repos must not get with a `template-only` comment, or wrap a block in
  `template-only:begin`/`template-only:end` comments; `tools/init_project.py` removes them.
  Template-owned files carry none.
- The example's wheel checks: [README.md](README.md#example-wheel-checks).
- `tests/kit/` tests the kit and its tools, labeled `kit`; `ctest --preset complete -L kit` runs them.
- Bootstrap's last step here reports newer compilers, Catch2 and CMake as a `NOTE`
  (`tools/toolchain_updates.py`); every repo's bootstrap reports its submodules' newer releases.
  [docs/lab/toolchain.md](docs/lab/toolchain.md#keeping-the-toolchain-current) says how to adopt one.
<!-- template-only:end -->

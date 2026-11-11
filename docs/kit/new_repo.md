# New repos from the template

New Nomisma C++ repos are created from `cmake_template` with GitHub's *template repository*
feature, then emptied by `tools/init_project.py`.

## How the feature works

- The new repo gets the template's files as **one initial commit**: no history, and no link back
  to the template.
- Only the default branch is copied. Leave "Include all branches" off.
- Submodules come across as they are (`.gitmodules` and the pinned commits). Relative URLs such as
  `../config.git` resolve within the same organization.
- Files stored in Git LFS are not supported (we don't use LFS).
- Repo settings are not copied: branch protection, Actions secrets and variables, access, webhooks.

## One-time setup

1. Create the GitHub organization. Pick a lowercase name: container image names must be lowercase.
2. Push `cmake_template` as a private repo. Set `url` in `.template_ref` to its clone URL.
   The template leaves `url` and `sha` blank until then; do not put a placeholder URL there.
3. **Settings → General → Template repository** (needs admin rights on the repo).
4. Run the `ci-image` workflow (Actions tab, or push a change under `docker/ci/`). Then make the
   `ci` package public (the organization's Packages page → `ci` → Package settings → Change
   visibility). It holds only public tools, and a public image needs no per-repo access.
5. Check that the template's `ci` workflow is green.
6. Tag the first release, `v0.1.0` ([build.md](build.md#versioning)).

## Creating a repo

```bash
gh repo create <org>/<name> --private --template <org>/cmake_template --clone
cd <name>
python3 tools/init_project.py <name> --license MIT
./bootstrap.sh
git push
```

(In the browser: "Use this template" → "Create a new repository", then clone and continue from
`python3 tools/init_project.py`.)

Choose `--license MIT`, `--license SUL-1.0` or `--license proprietary` explicitly.
`tools/init_project.py <name> --license <choice>` turns the copy into an empty repo. It removes the template's example
code instead of renaming it:

- deletes the example code (`src/example/`, `apps/hello/`, `apps/boost_example/`, the example
  unit tests and library usage test project, root `pyproject.toml`, `python/`, the nlohmann
  submodule), the lab (the sandbox experiments, `docs/lab/`) and the template-only files
  (`docker/`, the `ci-image` and `ci-template` workflows, the kit's own tests in `tests/kit/`,
  `tools/toolchain_updates.py`, and itself);
- retains an empty `src/` source container, registered in the root `CMakeLists.txt`;
  put each library under `src/<component>/`;
- retains optional Python packaging helpers and pinned build-tool requirements; generated repos
  start without a Python package and add one using [howto.md](howto.md#a-python-package);
- deletes the lines and blocks the template marks as template-only;
- sets the name and version `0.1.0` in `project()`; the options and the package name derive from it;
- puts the name and a TODO description into `README.md` and `AGENTS.md`, and starts an empty
  `CHANGELOG.md`;
- writes the selected project's `LICENSE` and `project_license.json`, retaining MIT notices
  on the copied toolchain and scaffold files;
- resolves the template URL and commit, records both in `.template_ref`, and commits. If the
  URL is unset or unreachable, it stops without changing the new repo. You can provide
  `--template-url <url>` instead of setting the URL in the template.

It checks everything first and stops without changing anything if the repo doesn't look like a
fresh copy of the template. `./bootstrap.sh` validates the initialized repo locally and enables
its [git hooks](build.md#git-hooks); its regular `ci` workflow validates it on GitHub. The new
repo starts without example tests; add your own code and tests with the recipes in
[howto.md](howto.md), then run bootstrap again.
Define new Nomisma library APIs in named modules and import them in apps and tests.
Keep macro headers and third-party dependency headers where needed.

Each derived repo declares its dependencies in its own `.gitmodules`, CMake target setup and
installed-package dependency lists. Follow the [dependency recipe](howto.md#a-dependency) in
the consuming repo. `cmake_template` supplies shared build mechanisms; it is not a registry of
dependencies between derived repos.

## Licensing

The template and synchronized kit are MIT licensed. A derived project's new code can use MIT,
Sustainable Use License 1.0 (source available), or the Nomisma proprietary notice. Repository
visibility is a separate choice; this command does not change GitHub visibility or settings.

`project_license.json` is owned by the derived repository. It records the default SPDX license
identifier and copyright notice for new files, plus `mit_files`: the copied scaffold files
that retain MIT after initialization. The shared kit retains MIT independently of that list.
The sandbox generator reads the project default. The initializer accepts `--copyright "2026 Name"`
when the project holder differs; copied MIT notices remain attributed to Alexey Tkachenko.

`LICENSE` records the project terms and identifies the MIT exceptions. Keep `cmake/kit/LICENSE`
with copied infrastructure; source packages must preserve the corresponding notices. Installed
CMake packages include `LICENSE-MIT.txt` beside their generated package config. Project code,
documentation and third-party notices still need to be included according to the release's contents.

The header check validates the project notice, the MIT kit notice, and each checked source's
license against this policy. `--staged` reads the staged policy and source contents; changing
the policy or `LICENSE` also checks unchanged indexed sources. Existing repositories must add
an explicit policy when adopting this kit; synchronization does not choose or change their license.
For a repository whose own files still use the existing proprietary notice:

```json
{
  "license": "LicenseRef-Nomisma-Labs-Proprietary",
  "copyright": "2026 Alexey Tkachenko",
  "mit_files": []
}
```

Record any separately MIT-licensed, repository-owned files in `mit_files`. Review the project's
actual `LICENSE` and source notices when selecting a different default; changing JSON alone
does not perform relicensing. See the [license catalog](../../tools/kit/licenses/README.md).

### Test creation locally

Run this in the template checkout, including before committing toolchain changes:

```bash
python3 tests/kit/project_license/check.py --source .
```

It snapshots the working tree into a temporary local upstream, creates a fresh repository for
each choice, runs the real initializer, generates and builds a small app, and tests a subsequent
template sync. It also checks refusal of missing/unknown choices, incorrect and staged license
notices, and preservation of the project's license and code. Temporary repositories are removed
on completion, including failures. It creates no GitHub repositories, does not register workspace
projects, and does not modify the real repository's Git history. Git transport is restricted to
local files. Pass `--compiler`, `--cmake` or `--generator` to use another installed toolchain.
The same check is registered as `cmake_template_project_license` in the `complete` kit tests.

## Per-repo settings

- **Branch protection** on `master` (pull requests only, CI must pass): private repos need the
  GitHub Team plan. On the Free plan, keep to the same discipline by hand.
- **Private submodules in CI:** the token GitHub Actions gives each job can read only its own
  repo, so checking out one of our private repos (for example `external/config`) fails. Use an
  organization-owned GitHub App with read-only access to repository contents:
  - when private submodules are needed in CI, add generic token support in `cmake_template`
    and sync it into the consuming repos: each workflow gets a short-lived token with
    `actions/create-github-app-token` and passes it to `actions/checkout` (`token:`, with
    `submodules: recursive`);
  - the App's ID and private key are Actions secrets. On the Free plan private repos can't use
    organization secrets, so each repo needs its own copy.

  This authentication is shared CI infrastructure; individual dependencies stay in the
  consuming repos. The template itself has only public submodules, so it doesn't need this yet.

## Keeping repos in sync

Template-owned paths are the kits (`cmake/kit/`, `tools/kit/`, `docs/kit/`), `bootstrap.sh`,
`.clang-format`, `.clang-tidy`, `.clangd`, `.editorconfig` and `.github/workflows/ci.yml`.
The MIT notice in `cmake/kit/LICENSE` is synchronized with the kit. The root `LICENSE`,
`project_license.json` and repository-owned source notices are preserved.
Change them only in `cmake_template`; a repo keeps its own CMake files in `cmake/<repo>/`, and
its tools in `tools/<repo>/` and docs in `docs/<repo>/`, outside the kits. These directories
have no additional layer grouping. To bring a repo up to date:

```bash
python3 tools/kit/sync_template.py      # to the newest release, or --to <release tag>
git diff --cached                       # review the staged changes
./bootstrap.sh
git commit -m "Sync cmake_template vX.Y.Z"
```

The script syncs from the template commit recorded in `.template_ref`:

- Each template-owned file that is unchanged since that commit is replaced, added or deleted to
  match the target release. Nothing is merged, and no other file of the repo changes.
- If a template-owned file was edited or deleted in the repo, nothing changes at all. The script
  lists those files: move the change into `cmake_template`, or into a file the repo owns, and
  rerun; or rerun with `--overwrite` to take the template's version. The work tree must be
  clean, so replaced content stays in git history.
- It stages the result together with `.template_ref`. It then lists the repo's own files that
  still mention moved or removed template paths, the repo's files inside the kit's folders, and
  the `### Upgrade` steps from the template's `CHANGELOG.md` for the releases in between: the
  changes a repo makes by hand.

The target release's copy of the script does the work, so the file list and the rules come from
the release being synced to. The script syncs the repo of the current directory, so any copy of
it can run a sync, for example `python3 /path/to/cmake_template/tools/kit/sync_template.py`.

The sync transfers shared build rules and guidance, not repository-owned code. A change to the
composition policy therefore has two parts: sync the template-owned paths, then migrate the
repo's `src/`, apps, tests and CMake target declarations to that policy. For the native module
transition, move library declarations into interfaces or partitions, and make all consumers
import the primary module. Keep macro and dependency
headers where their boundary requires them, and validate source and installed-package usage.

When adopting the `src/` layout, move the repository's library sources and update its root
`CMakeLists.txt` and any explicit source paths. Coordinate internal dependency pins: the
outermost kit supplies helpers for the whole build, so a dependency still using `core/`
must also migrate or declare its file-set `BASE_DIR` explicitly. Recreate build trees and
compilation databases after the moves; kit synchronization does not perform these steps.

The script syncs committed template revisions, not uncommitted working-tree changes. While
reviewing an unpublished template change, keep its shared files in the template checkout;
assess the repo-specific migration separately and perform the recorded sync once the template
revision is available.

Dependency changes also update repository-owned files: `.gitmodules`, the top-level
`CMakeLists.txt`, helpers in `cmake/<repo>/` and the dependencies of installed packages. The
release's upgrade steps list them.

Python packaging follows the same boundary: synchronize the kit, tools and shared guidance,
then add repository-owned `pyproject.toml`, binding sources and target declarations. Keep the
backend, environment and runtime-library installation mechanisms in the template; repositories
declare their package names, dependencies and contents through those facilities.

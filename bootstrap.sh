#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
#
# Template-owned. Validate this checkout using the existing presets and quality checks.
set -uo pipefail

if [[ $# -gt 1 ]]; then
    printf 'Expected at most one preset. Usage: %s [preset | --help]\n' "$0" >&2
    exit 2
fi
cd -- "$(dirname -- "${BASH_SOURCE[0]}")" || exit 1

# Let CMake resolve includes, inheritance, conditions and CMakeUserPresets.json.
# Bootstrap needs matching configure/build/test names and the repo's build/<preset> layout.
if ! command -v cmake > /dev/null 2>&1; then
    printf 'Missing required tool: cmake (needed to discover presets).\n' >&2
    exit 1
fi
if ! preset_listing=$(cmake --list-presets=all); then
    printf 'Could not discover CMake presets; no build outputs were changed.\n' >&2
    exit 1
fi
configure_presets=()
declare -A build_presets=() test_presets=()
preset_kind=
while IFS= read -r line; do
    case "$line" in
        'Available configure presets:') preset_kind=configure ;;
        'Available build presets:') preset_kind=build ;;
        'Available test presets:') preset_kind=test ;;
        'Available '*':') preset_kind= ;;
    esac
    if [[ $line =~ ^[[:space:]]+\"([^\"]+)\" ]]; then
        preset_name=${BASH_REMATCH[1]}
        case "$preset_kind" in
            configure) configure_presets+=("$preset_name") ;;
            build) build_presets["$preset_name"]=1 ;;
            test) test_presets["$preset_name"]=1 ;;
        esac
    fi
done <<< "$preset_listing"

available_presets=()
presets=()
for preset in "${configure_presets[@]}"; do
    if [[ -n ${build_presets["$preset"]:-} && -n ${test_presets["$preset"]:-} ]]; then
        available_presets+=("$preset")
        # Release validation remains opt-in; new paired presets join the default run.
        if [[ $preset != release ]]; then
            presets+=("$preset")
        fi
    fi
done

show_help() {
    cat <<'HELP'
Usage: ./bootstrap.sh [preset]
       ./bootstrap.sh --help

With no preset, validate all discovered presets except release (which is opt-in).
With a preset, validate only that preset (for example: ./bootstrap.sh release).
Each run clears the selected build/<preset> directories and previous bootstrap logs,
initializes missing submodules, then configures/builds/tests from scratch.
Other build directories are preserved.

Tests run in parallel. A dev build leaves out the tests labeled integration and
kit (NOM_SKIP_TEST_LABELS); the other presets run every test.
Python compatibility/imports, SPDX headers and formatting are checked on every run.
Naming is included when clang is selected.
Git submodules with a newer upstream release are reported as a note (network needed).
Where tools/toolchain_updates.py exists (cmake_template), a last step also reports
newer compilers, Catch2 and CMake.
The git hooks in tools/kit/hooks are enabled for this clone (core.hooksPath), unless
core.hooksPath already names other hooks.
Each step reports PASS, FAIL, SKIP or NOTE; failures return exit code 1, notes don't.
Current logs are kept in build/bootstrap/.

Install the tools listed in docs/kit/build.md first. No packages are installed.
Build concurrency defaults to available CPU cores minus one, with a minimum of one.
Set CMAKE_BUILD_PARALLEL_LEVEL to override it.
CLANG_FORMAT and RUN_CLANG_TIDY overrides are respected by the existing checks.

Available presets (matching configure/build/test names, discovered by CMake):
HELP
    if (( ${#available_presets[@]} )); then
        printf '  %s\n' "${available_presets[@]}"
    else
        printf '  (none)\n'
    fi
}

if [[ $# -eq 1 && ( $1 == --help || $1 == -h ) ]]; then
    show_help
    exit 0
fi
if [[ $# -eq 1 ]]; then
    preset_found=false
    for available in "${available_presets[@]}"; do
        if [[ $1 == "$available" ]]; then
            preset_found=true
            break
        fi
    done
    if [[ $preset_found != true ]]; then
        printf 'Unknown preset: %s. Use --help to list available presets.\n' "$1" >&2
        exit 2
    fi
    presets=("$1")
fi

if (( ${#presets[@]} == 0 )); then
    printf 'No presets selected. Use --help to list available presets.\n' >&2
    exit 2
fi
for preset in "${presets[@]}"; do
    # Preset names become directory and log names; keep cleanup within build/.
    if [[ ! $preset =~ ^[a-zA-Z0-9][a-zA-Z0-9_.-]*$ || $preset == bootstrap ]]; then
        printf 'Unsupported bootstrap preset name: %s; no outputs were changed.\n' "$preset" >&2
        exit 2
    fi
done
next_preset=${presets[0]}
if [[ -z ${CMAKE_BUILD_PARALLEL_LEVEL:-} ]]; then
    bootstrap_cores=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
    CMAKE_BUILD_PARALLEL_LEVEL=$((bootstrap_cores > 1 ? bootstrap_cores - 1 : 1))
fi
export CMAKE_BUILD_PARALLEL_LEVEL

results=()
failed=0
passed=0
skipped=0
noted=0
step_number=0
step_log=

record() {
    local status=$1 label=$2 detail=${3:-}
    results+=("[$status] $label${detail:+ — $detail}")
    printf '[%s] %s%s\n' "$status" "$label" "${detail:+ — $detail}"
    case "$status" in
        PASS) passed=$((passed + 1)) ;;
        FAIL) failed=$((failed + 1)) ;;
        SKIP) skipped=$((skipped + 1)) ;;
        NOTE) noted=$((noted + 1)) ;;
    esac
}

# Run a command with its output in the next step log, which step_log names.
log_step() {
    local label=$1
    shift
    step_number=$((step_number + 1))
    step_log="$log_dir/step-$step_number.log"
    printf '\n[RUN] %s\n' "$label"
    printf 'Command:' > "$step_log"
    printf ' %q' "$@" >> "$step_log"
    printf '\n' >> "$step_log"
    "$@" >> "$step_log" 2>&1
}

run_step() {
    local label=$1 status
    if log_step "$@"; then
        record PASS "$label"
        return 0
    else
        status=$?
        record FAIL "$label" "exit $status; $step_log"
        tail -n 20 "$step_log"
        return "$status"
    fi
}

# A report that doesn't fail validation: exit 0 passes, exit 3 records a NOTE and exit 4, nothing
# checked, a SKIP, each with the report's last line; any other exit fails.
run_report() {
    local label=$1 status=0
    log_step "$@" || status=$?
    case "$status" in
        0) record PASS "$label" "$(tail -n 1 "$step_log")" ;;
        3) record NOTE "$label" "$(tail -n 1 "$step_log"); $step_log" ;;
        4) record SKIP "$label" "$(tail -n 1 "$step_log")" ;;
        *) record FAIL "$label" "exit $status; $step_log"
           tail -n 20 "$step_log" ;;
    esac
}

have_tools() {
    local tool missing=0
    for tool in "$@"; do
        if ! command -v "$tool" > /dev/null 2>&1; then
            printf 'Missing required tool: %s\n' "$tool" >&2
            missing=1
        fi
    done
    return "$missing"
}

check_python_environment() {
    local preset
    python3 tools/kit/checks/python.py || return
    for preset in "${presets[@]}"; do
        if [[ $preset == clang ]]; then
            # This runner may use a different interpreter through its shebang.
            "${RUN_CLANG_TIDY:-run-clang-tidy-22}" --help > /dev/null || return
            printf 'Clang-tidy runner starts successfully: %s\n' "${RUN_CLANG_TIDY:-run-clang-tidy-22}"
        fi
    done
}

prepare_dependencies() {
    local status
    status=$(git submodule status --recursive) || return
    # Do not reset a developer's submodule checkout to a different commit.
    if [[ $status == +* || $status == U* || $status == *$'\n+'* || $status == *$'\nU'* ]]; then
        printf '%s\n' "$status"
        printf 'Submodules differ from their recorded commits or have conflicts. Resolve them before rerunning.\n'
        return 1
    fi
    git submodule update --init --recursive
}

run_tests() {
    local preset=$1 manifest="$log_dir/$1-tests.json" selection count
    # A freshly initialized repository legitimately has no tests yet, and a dev build leaves out
    # some labels; report an empty selection explicitly.
    if ! ctest --preset "$preset" --show-only=json-v1 > "$manifest" 2> "$manifest.err"; then
        record FAIL "$preset: discover tests" "see $manifest.err"
        return
    fi
    # The count, then how many tests carry each label: "15 7 integration, 7 kit, 1 unit".
    if ! selection=$(python3 -c '
import collections, json, sys
tests = json.load(open(sys.argv[1]))["tests"]
labels = collections.Counter(label for test in tests for label in next(
    (property["value"] for property in test.get("properties", []) if property["name"] == "LABELS"), ["unlabeled"]))
print(len(tests), ", ".join(f"{count} {label}" for label, count in sorted(labels.items())))' "$manifest"); then
        record FAIL "$preset: discover tests" "invalid test listing in $manifest"
        return
    fi
    count=${selection%% *}
    if [[ $count == 0 ]]; then
        record SKIP "$preset: tests" 'no tests run in this build; add tests using docs/kit/howto.md'
    else
        run_step "$preset: tests ($count: ${selection#* })" \
            ctest --preset "$preset" --output-on-failure --no-tests=error
    fi
}

# Remove only this script's outputs. Refuse a symlinked parent so cleanup cannot
# follow build/ into another directory; rm removes individual symlinks themselves.
printf '[RUN] Clean previous bootstrap results and validation builds\n'
if [[ -L build ]]; then
    record FAIL 'Clean previous bootstrap results and validation builds' 'build/ is a symlink; use a local build directory'
    exit 1
fi
build_dirs=()
for preset in "${presets[@]}"; do
    build_dirs+=("build/$preset")
done
if rm -rf -- build/bootstrap "${build_dirs[@]}"; then
    record PASS 'Clean previous bootstrap results and validation builds'
else
    record FAIL 'Clean previous bootstrap results and validation builds' 'cleanup failed; no builds were started'
    exit 1
fi
mkdir -p build/bootstrap || exit 1
log_dir=$(mktemp -d "$PWD/build/bootstrap/run.XXXXXX") || exit 1
printf 'Validating %s\nLogs: %s\n' "$PWD" "$log_dir"
printf 'Presets: %s\n' "${presets[*]}"
required_tools=(git cmake ctest python3 "${CLANG_FORMAT:-clang-format-22}")
for preset in "${presets[@]}"; do
    if [[ $preset == clang ]]; then
        required_tools+=(clang-tidy-22 "${RUN_CLANG_TIDY:-run-clang-tidy-22}")
    fi
done
run_step 'Required tools' have_tools "${required_tools[@]}"

# The hooks check each commit's staged content (docs/kit/build.md#git-hooks). The setting lives
# in this clone's .git/config; the relative path resolves in each worktree. Hooks chosen
# another way, including an opt-out to .git/hooks, are left alone.
if have_tools git && git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    hooks_path=$(git config --get core.hooksPath)
    if [[ -z $hooks_path || $hooks_path == tools/kit/hooks ]]; then
        run_step 'Git hooks (core.hooksPath = tools/kit/hooks)' git config --local core.hooksPath tools/kit/hooks
    else
        record SKIP 'Git hooks' "core.hooksPath is $hooks_path; left as it is"
    fi
else
    record SKIP 'Git hooks' 'not a git checkout'
fi

python_ready=false
if have_tools python3 git; then
    if run_step 'Python environment' check_python_environment; then
        python_ready=true
    fi
else
    record SKIP 'Python environment' 'requires python3 and git'
fi

dependencies_ready=false
if have_tools git; then
    if run_step 'Dependencies (initialize missing submodules)' prepare_dependencies; then
        dependencies_ready=true
    fi
else
    record SKIP 'Dependencies' 'git is unavailable'
fi

clang_selected=false
clang_ready=false
for preset in "${presets[@]}"; do
    if [[ $preset == clang ]]; then
        clang_selected=true
    fi
    # CMake checks the compiler and generator specified by the actual preset.
    if [[ $dependencies_ready != true || $python_ready != true ]] || ! have_tools cmake ctest; then
        record SKIP "$preset: configure, build and tests" 'required tools, Python environment or dependencies failed'
        continue
    fi
    if ! run_step "$preset: configure" cmake --preset "$preset"; then
        record SKIP "$preset: build and tests" 'configuration failed'
        continue
    fi
    if ! run_step "$preset: build" cmake --build --preset "$preset"; then
        record SKIP "$preset: tests" 'build failed'
        continue
    fi
    if [[ $preset == clang ]]; then
        clang_ready=true
    fi
    run_tests "$preset"
done

if [[ $python_ready == true ]]; then
    run_step 'SPDX headers' python3 tools/kit/checks/headers.py
else
    record SKIP 'SPDX headers' 'Python environment check did not pass'
fi
if have_tools git "${CLANG_FORMAT:-clang-format-22}"; then
    run_step 'Formatting' bash tools/kit/checks/format.sh
else
    record SKIP 'Formatting' 'required tools failed'
fi
if [[ $clang_selected == true ]]; then
    if [[ $clang_ready == true ]] && have_tools clang-tidy-22 "${RUN_CLANG_TIDY:-run-clang-tidy-22}"; then
        run_step 'Naming' bash tools/kit/checks/naming.sh
    else
        record SKIP 'Naming' 'requires a successful Clang build and clang-tidy tools'
    fi
fi
# Submodules with newer releases (docs/kit/build.md#dependencies); a note never fails the run.
if [[ $python_ready == true ]]; then
    run_report 'Dependency updates' python3 tools/kit/checks/dependency_updates.py
else
    record SKIP 'Dependency updates' 'Python environment check did not pass'
fi
# cmake_template moves the toolchain first; its own script reports what is newer than the repo
# uses (docs/kit/build.md#toolchain). Other repos don't have the script.
if [[ -f tools/toolchain_updates.py ]]; then
    if [[ $python_ready == true ]]; then
        run_report 'Toolchain updates' python3 tools/toolchain_updates.py
    else
        record SKIP 'Toolchain updates' 'Python environment check did not pass'
    fi
fi

printf '\nSummary\n'
printf '%s\n' "${results[@]}"
printf '\n%s passed, %s failed, %s skipped, %s noted.\nLogs: %s\n' "$passed" "$failed" "$skipped" "$noted" "$log_dir"
if (( failed > 0 )); then
    printf '\nValidation failed. Fix the failures above, then rerun ./bootstrap.sh%s.\n' "${1:+ $1}"
    printf 'Toolchain and build instructions: docs/kit/build.md\n'
    printf 'Formatting fixes: tools/kit/checks/format.sh --fix\n'
    exit 1
fi

printf '\nValidation passed for the checks run.\n'
if (( skipped > 0 )); then
    printf 'Review the skipped checks above.\n'
fi
if (( noted > 0 )); then
    printf 'Read the notes above; they need no fix to pass.\n'
fi
printf '\nNext steps:\n'
printf '  Add a library, app or tests: docs/kit/howto.md\n'
printf '  Build and tests: cmake --workflow --preset %s\n' "$next_preset"
printf '  Executables: build/%s/bin/\n' "$next_preset"

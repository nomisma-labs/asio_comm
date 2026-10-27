#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Nomisma-Labs-Proprietary
#
# Checks C++ formatting against .clang-format. With --fix, reformats the files instead. With
# --staged, checks the staged copy of each staged file, the content a commit would get; the
# pre-commit hook (tools/kit/hooks/pre-commit) runs it that way.
set -euo pipefail
cd "$(dirname "$0")/../../.."

clang_format=${CLANG_FORMAT:-clang-format-22}
patterns=('*.h' '*.hpp' '*.ipp' '*.cpp' '*.cxx' '*.cppm')

if [[ "${1:-}" == "--staged" ]]; then
    files=()
    blobs=()
    # Raw entries are ":<old mode> <new mode> <old blob> <new blob> <status>" NUL <path> NUL.
    while IFS= read -r -d '' meta && IFS= read -r -d '' file; do
        read -r _ mode _ blob _ <<< "$meta"
        if [[ $mode == 100644 || $mode == 100755 ]]; then # not a symlink or a submodule
            files+=("$file")
            blobs+=("$blob")
        fi
    done < <(git diff --cached --raw -z --no-abbrev --no-renames --diff-filter=d -- "${patterns[@]}")
    if (( ${#files[@]} == 0 )); then
        exit 0
    fi
    if ! command -v "$clang_format" > /dev/null; then
        echo "$clang_format not found: install clang-format 22 (docs/kit/build.md#toolchain) or set CLANG_FORMAT" >&2
        exit 1
    fi

    temporary=$(mktemp -d)
    trap 'rm -rf "$temporary"' EXIT
    unformatted=()
    for i in "${!files[@]}"; do
        git cat-file blob "${blobs[i]}" > "$temporary/staged"
        "$clang_format" --assume-filename="${files[i]}" < "$temporary/staged" > "$temporary/formatted"
        if ! diff -u --label "a/${files[i]}" --label "b/${files[i]}" "$temporary/staged" "$temporary/formatted"; then
            unformatted+=("${files[i]}")
        fi
    done
    if (( ${#unformatted[@]} == 0 )); then
        exit 0
    fi

    # Formatting rewrites the working copy, so a file with unstaged changes needs git add -p.
    whole=()
    partial=()
    for file in "${unformatted[@]}"; do
        if git diff --quiet -- "$file"; then
            whole+=("$file")
        else
            partial+=("$file")
        fi
    done
    echo "These staged files need formatting. To fix them:" >&2
    if (( ${#whole[@]} )); then
        printf '    %s -i%s && git add%s\n' "$clang_format" "$(printf ' %q' "${whole[@]}")" \
            "$(printf ' %q' "${whole[@]}")" >&2
    fi
    for file in "${partial[@]}"; do
        printf '    %s -i %q, then git add -p %q to stage only the fixes (it has unstaged changes)\n' \
            "$clang_format" "$file" "$file" >&2
    done
    exit 1
fi

# Skip paths git still lists but that are gone from the working tree (deleted or renamed, not yet staged).
mapfile -t files < <(git ls-files --cached --others --exclude-standard -- "${patterns[@]}" |
    while read -r file; do [[ -f "$file" ]] && echo "$file"; done)

if (( ${#files[@]} == 0 )); then
    exit 0 # a new repo has no C++ yet; clang-format would read stdin
elif [[ "${1:-}" == "--fix" ]]; then
    "$clang_format" -i "${files[@]}"
else
    "$clang_format" --dry-run --Werror "${files[@]}"
fi

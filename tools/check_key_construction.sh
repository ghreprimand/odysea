#!/usr/bin/env bash
set -euo pipefail

# Keeps row-key construction in the directory model to a single function.
#
# The model counts how many row keys an update builds, and a large-directory
# gate reads that count to hold the shape of the update's cost. The count is
# only meaningful while every key is built in one place: a second spelling of
# the same formula produces a key that compares equal, costs the same, and
# goes uncounted, so a regression could restore a discarded search and still
# read as healthy. That is not hypothetical — it was demonstrated against this
# very counter before this guard existed.
#
# The rule: within the directory-model sources, `lexically_normal` may appear
# only inside the functions named below. Every other path normalization in
# those files has to go through one of them.

readonly source_glob='app/src/directory_list_model*'
readonly -a permitted_functions=(entryKey normalizedFilesystemPath)

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "key_construction_guard: skipped because Git metadata is unavailable"
    exit 77
fi

repository_root="$(git rev-parse --show-toplevel)"
readonly repository_root
cd "$repository_root"

status=0
inspected_files=0
permitted_sightings=0

is_permitted_function() {
    local name="$1"
    local permitted
    for permitted in "${permitted_functions[@]}"; do
        if [[ "$name" == "$permitted" ]]; then
            return 0
        fi
    done
    return 1
}

# Reports the enclosing `DirectoryListModel::` member for a line number, or an
# empty string when the line sits outside any member definition. Definitions
# are recognized at column zero, which is where clang-format puts them, so an
# occurrence inside a nested lambda still resolves to the member that contains
# it.
enclosing_function() {
    local file="$1"
    local line="$2"
    awk -v target="$line" '
        NR > target { exit }
        /^[A-Za-z_][A-Za-z0-9_:<>, ]*[ *&]DirectoryListModel::[A-Za-z_][A-Za-z0-9_]*\(/ {
            match($0, /DirectoryListModel::[A-Za-z_][A-Za-z0-9_]*\(/)
            name = substr($0, RSTART + length("DirectoryListModel::"), RLENGTH - length("DirectoryListModel::") - 1)
        }
        END { print name }
    ' "$file"
}

while IFS= read -r -d '' path; do
    inspected_files=$((inspected_files + 1))
    while IFS=: read -r line _; do
        [[ -n "$line" ]] || continue
        enclosing="$(enclosing_function "$path" "$line")"
        if [[ -z "$enclosing" ]]; then
            printf 'key_construction_guard: %s:%s normalizes a path outside any member function\n' \
                "$path" "$line" >&2
            status=1
            continue
        fi
        if ! is_permitted_function "$enclosing"; then
            printf 'key_construction_guard: %s:%s normalizes a path in %s; only %s may\n' \
                "$path" "$line" "$enclosing" "${permitted_functions[*]}" >&2
            status=1
            continue
        fi
        permitted_sightings=$((permitted_sightings + 1))
    done < <(grep -n 'lexically_normal' "$path" | cut -d: -f1 | sed 's/$/:/')
done < <(git ls-files -z -- "$source_glob")

# Two vacuity checks. A guard that inspected no files, or that found the
# formula nowhere at all, would pass while enforcing nothing — and both are
# reachable by an ordinary rename.
if ((inspected_files == 0)); then
    echo "key_construction_guard: no directory-model sources matched $source_glob" >&2
    exit 1
fi
if ((permitted_sightings == 0)); then
    printf 'key_construction_guard: no permitted normalization found; %s may have been renamed\n' \
        "${permitted_functions[*]}" >&2
    exit 1
fi

if ((status != 0)); then
    exit "$status"
fi

printf 'key_construction_guard: %d directory-model sources passed, %d permitted normalizations\n' \
    "$inspected_files" "$permitted_sightings"

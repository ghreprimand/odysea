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
#
# WHAT THIS GUARD DOES NOT DO, stated because it was once claimed to. It pins
# a formula, not a cost. A regression that builds an equal key by a different
# spelling passes it untouched: scanned paths are already absolute and normal,
# so `QString::fromStdString(entry.path.string())` yields a byte-identical key
# with no normalization to find, and a linear rescan restored that way was
# measured at 12.9 times the healthy load with the key count unchanged to the
# digit. Cost is bounded separately, by measurements that do not depend on
# this function being called at all: the large-directory case expresses a
# load's cost in key constructions the same process performs, and bounds how
# that cost grows when the directory doubles.
#
# Widening the token set was considered and rejected. Adding cleanPath,
# weakly_canonical, canonical, and absolute would drag in the navigation-path,
# operation-destination, and thumbnail-key uses that legitimately live in
# these files, each of which would then have to be permitted — and every
# permitted function is another place a hand-spelled key could sit. The narrow
# rule is strong precisely because `lexically_normal` has exactly two honest
# uses here.

# Which files the rule covers. The union of the model's own sources and any
# other source under app/src that includes the model header: a new file
# holding a hand-spelled key would otherwise be invisible for as long as it
# was named something else. A union rather than a replacement, so the covered
# set can only ever grow.
readonly source_glob='app/src/directory_list_model*'
readonly include_glob='app/src/*'
readonly model_header='directory_list_model.hpp'
readonly -a permitted_functions=(entryKey normalizedFilesystemPath)

# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"
guard_corpus_init key_construction_guard

covered_paths="$(mktemp)"
readonly covered_paths
trap 'rm -f -- "$covered_paths"' EXIT

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

> "$covered_paths"
guard_corpus_list "$source_glob" | tr '\0' '\n' >> "$covered_paths"
while IFS= read -r candidate; do
    [[ -n "$candidate" ]] || continue
    if grep -q "$model_header" "$candidate"; then
        printf '%s\n' "$candidate" >> "$covered_paths"
    fi
done < <(guard_corpus_list "$include_glob" | tr '\0' '\n')
sort -u -o "$covered_paths" "$covered_paths"

while IFS= read -r path; do
    [[ -n "$path" ]] || continue
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
done < "$covered_paths"

# Two vacuity checks. A guard that inspected no files, or that found the
# formula nowhere at all, would pass while enforcing nothing — and both are
# reachable by an ordinary rename.
#
# A third was written and removed rather than shipped: asserting that the
# covered set is at least as large as the named sources cannot fail, because
# the covered set is their union with another set. A check that cannot fail
# reports assurance it never established.
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

printf 'key_construction_guard: %d covered sources passed, %d permitted normalizations\n' \
    "$inspected_files" "$permitted_sightings"

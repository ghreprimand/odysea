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
# Two rules, one for each spelling a key has ever been written in.
#
# NORMALIZATION. Within the directory-model sources, `lexically_normal` may
# appear only inside the functions named for it below. Every other path
# normalization in those files has to go through one of them.
#
# AN ENTRY'S PATH AS TEXT. Within the same sources, converting a listed
# entry's `path` member to text may appear only inside the functions named for
# it. This is the second spelling, and it was live for as long as this guard
# had only the first: scanned paths are already absolute and normal, so
# `QString::fromStdString(entry.path.string())` yields a key byte-identical to
# the counted one with no normalization to find, and a linear rescan restored
# that way was measured at 12.9 times the healthy load with the key count
# unchanged to the digit.
#
# That hole used to be covered by a clock instead — the large-directory case
# denominated a load's elapsed time in key constructions and bounded how the
# elapsed time grew when the directory doubled, neither of which depended on
# the key builder being called. Both readings were measured against a machine
# under load and neither survived it: a shared machine becomes superlinear in
# directory size on its own, so a growth ratio of any clock mixes the model's
# exponent with the machine's. The clock bounds are gone and this rule stands
# where they stood. The three functions permitted here are the ones that hand
# a path to the interface — a role, a launch, a selection report — and none of
# them is a reconciliation site.
#
# WHAT THIS GUARD STILL DOES NOT DO, stated plainly because a rule about
# spellings can only ever cover the spellings it names. A comparison written
# against `std::filesystem::path` values directly builds no string at all, so
# it is invisible to both rules and to the counter, and no static rule here
# would find it. What holds that case is the shape of the update itself: the
# reconciliation identifies rows through an index built once per update, and a
# search per delivered entry is a visible change to that structure rather than
# a spelling inside it.
#
# Widening the normalization token set was considered and rejected. Adding
# cleanPath, weakly_canonical, canonical, and absolute would drag in the
# navigation-path, operation-destination, and thumbnail-key uses that
# legitimately live in these files, each of which would then have to be
# permitted — and every permitted function is another place a hand-spelled key
# could sit. The narrow rule is strong precisely because `lexically_normal`
# has exactly two honest uses here.

# Which files the rule covers. The union of the model's own sources and any
# other source under app/src that includes the model header: a new file
# holding a hand-spelled key would otherwise be invisible for as long as it
# was named something else. A union rather than a replacement, so the covered
# set can only ever grow.
readonly source_glob='app/src/directory_list_model*'
readonly include_glob='app/src/*'
readonly model_header='directory_list_model.hpp'

# The two rules, each with the pattern it looks for, the functions allowed to
# hold a match, and the wording used to report one that is not.
readonly normalization_pattern='lexically_normal'
readonly -a normalization_permitted=(entryKey normalizedFilesystemPath)
readonly normalization_subject='normalizes a path'

# An entry's own path member spelled as text, in any of the conversions the
# standard offers for it. Listed rather than reduced to a wildcard so that a
# conversion added to a future standard is reported as unrecognized by this
# guard's self-test rather than silently admitted.
readonly entry_path_pattern='\.path\.(string|native|c_str|u8string|generic_string)\(\)'
readonly -a entry_path_permitted=(data activate selectedPaths)
readonly entry_path_subject="spells an entry's path as text"

# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"
guard_corpus_init key_construction_guard

covered_paths="$(mktemp)"
readonly covered_paths
trap 'rm -f -- "$covered_paths"' EXIT

status=0
inspected_files=0
normalization_sightings=0
entry_path_sightings=0

is_permitted_function() {
    local name="$1"
    shift
    local permitted
    for permitted in "$@"; do
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

# Applies one rule to one file, reporting every match that sits somewhere it
# may not. Counts permitted matches on stdout so the caller can require the
# rule to have found something; a rule that matches nowhere is enforcing
# nothing, and that is reachable by an ordinary rename.
apply_rule() {
    local file="$1"
    local pattern="$2"
    local subject="$3"
    shift 3
    local -a permitted=("$@")
    local sightings=0
    local rule_status=0
    local line enclosing

    while IFS=: read -r line _; do
        [[ -n "$line" ]] || continue
        enclosing="$(enclosing_function "$file" "$line")"
        if [[ -z "$enclosing" ]]; then
            printf 'key_construction_guard: %s:%s %s outside any member function\n' \
                "$file" "$line" "$subject" >&2
            rule_status=1
            continue
        fi
        if ! is_permitted_function "$enclosing" "${permitted[@]}"; then
            printf 'key_construction_guard: %s:%s %s in %s; only %s may\n' \
                "$file" "$line" "$subject" "$enclosing" "${permitted[*]}" >&2
            rule_status=1
            continue
        fi
        sightings=$((sightings + 1))
    done < <(grep -nE "$pattern" "$file" | cut -d: -f1 | sed 's/$/:/')

    printf '%d\n' "$sightings"
    return "$rule_status"
}

while IFS= read -r path; do
    [[ -n "$path" ]] || continue
    inspected_files=$((inspected_files + 1))

    # Each rule's status is read from its own call rather than through a
    # pipeline, and the sighting count is returned on stdout, so a rule that
    # reports a violation cannot have that status swallowed by the assignment
    # around it.
    seen=0
    if ! seen="$(apply_rule "$path" "$normalization_pattern" "$normalization_subject" \
        "${normalization_permitted[@]}")"; then
        status=1
    fi
    normalization_sightings=$((normalization_sightings + seen))

    seen=0
    if ! seen="$(apply_rule "$path" "$entry_path_pattern" "$entry_path_subject" \
        "${entry_path_permitted[@]}")"; then
        status=1
    fi
    entry_path_sightings=$((entry_path_sightings + seen))
done < "$covered_paths"

# Three vacuity checks. A guard that inspected no files, or that found either
# spelling nowhere at all, would pass while enforcing nothing — and all three
# are reachable by an ordinary rename.
#
# A fourth was written and removed rather than shipped: asserting that the
# covered set is at least as large as the named sources cannot fail, because
# the covered set is their union with another set. A check that cannot fail
# reports assurance it never established.
if ((inspected_files == 0)); then
    echo "key_construction_guard: no directory-model sources matched $source_glob" >&2
    exit 1
fi
if ((normalization_sightings == 0)); then
    printf 'key_construction_guard: no permitted normalization found; %s may have been renamed\n' \
        "${normalization_permitted[*]}" >&2
    exit 1
fi
if ((entry_path_sightings == 0)); then
    printf 'key_construction_guard: no permitted entry-path spelling found; %s may have been renamed\n' \
        "${entry_path_permitted[*]}" >&2
    exit 1
fi

if ((status != 0)); then
    exit "$status"
fi

printf 'key_construction_guard: %d covered sources passed, %d permitted normalizations, %d permitted entry-path spellings\n' \
    "$inspected_files" "$normalization_sightings" "$entry_path_sightings"

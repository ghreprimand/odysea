#!/usr/bin/env bash

# Proves the public-repository guard still rejects an address in a shell file
# after shell expansion syntax was carved out of the at-sign ban.
#
# A carve-out with no negative test is a hole with a comment on it: the guard
# would keep reporting success, and nothing would reveal that the exception had
# swallowed the rule. Each scenario below builds a throwaway repository, stages
# a file, and runs the real guard against it, so the check measures the guard's
# behaviour rather than the text of its patterns.
#
# The planted at-sign byte is composed at run time rather than written out.
# This file is scanned by the guard like any other tracked shell source, and an
# address written literally here would have to be excused by exactly the kind
# of file-level exclusion the carve-out exists to avoid. The legitimate
# expansion forms below are written literally, because the carve-out permits
# them: this file's own tracked form is part of what it demonstrates.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly guard="$tools_directory/check_public_repo.sh"
if [[ ! -f "$guard" ]]; then
    printf 'public_repository_guard_self_test: the gate is missing\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

readonly at="$(printf '\100')"
readonly owner_identity="1+owner${at}users.noreply.github.com"
readonly planted_address="admin${at}host.example"

readonly address_message='email-like or user-at-host text is tracked in a shell file'
readonly success_message='corpus paths'

status=0
checked=0

# Builds a throwaway repository holding one shell file with the given body,
# committed under an identity the guard accepts, so that a failure can only
# come from the corpus scan under test.
build_repository() {
    local name="$1"
    local body="$2"

    local root="$workspace/$name"
    mkdir -p "$root"
    git init -q "$root"
    git -C "$root" config user.name owner
    git -C "$root" config user.email "$owner_identity"
    git -C "$root" config commit.gpgsign false

    printf '%s\n' "$body" >"$root/script.sh"
    git -C "$root" add script.sh
    git -C "$root" -c core.hooksPath=/dev/null commit -q -m 'Add a script'

    printf '%s' "$root"
}

# The same throwaway repository holding one file under a caller-chosen name, so
# a scenario can put its fixture in the kind of file the case is about instead
# of always in a shell script.
build_repository_named() {
    local name="$1"
    local file_name="$2"
    local body="$3"

    local root="$workspace/$name"
    mkdir -p "$root"
    git init -q "$root"
    git -C "$root" config user.name owner
    git -C "$root" config user.email "$owner_identity"
    git -C "$root" config commit.gpgsign false

    printf '%s\n' "$body" >"$root/$file_name"
    git -C "$root" add "$file_name"
    git -C "$root" -c core.hooksPath=/dev/null commit -q -m 'Add a file'

    printf '%s' "$root"
}

# Runs the guard inside one throwaway repository and requires it to accept the
# corpus or to reject it for the address reason specifically. Requiring the
# reason keeps a scenario from passing because some unrelated check fired.
expect_outcome() {
    local scenario="$1"
    local expectation="$2"
    local root="$3"

    local output=""
    local exit_status=0
    output="$(cd "$root" && bash "$guard" 2>&1)" || exit_status=$?

    checked=$((checked + 1))
    case "$expectation" in
        accept)
            if ((exit_status != 0)) || [[ "$output" != *"$success_message"* ]]; then
                printf 'public_repository_guard_self_test: %s should be accepted, but the guard said: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            fi
            ;;
        reject)
            if ((exit_status == 0)); then
                printf 'public_repository_guard_self_test: %s should be rejected\n' \
                    "$scenario" >&2
                status=1
            elif [[ "$output" != *"$address_message"* ]]; then
                printf 'public_repository_guard_self_test: %s should be rejected for the address, but the guard said: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            fi
            ;;
    esac
}

# The permitted forms, one scenario each. These are the idioms the ban had
# driven contributors away from.
expect_outcome array_at_subscript accept \
    "$(build_repository array_at 'printf "%s\n" "${entries[@]}"')"

expect_outcome array_length accept \
    "$(build_repository array_length 'printf "%d\n" "${#entries[@]}"')"

expect_outcome quoted_positional accept \
    "$(build_repository quoted_positional 'printf "%s\n" "$@"')"

expect_outcome unquoted_positional accept \
    "$(build_repository unquoted_positional 'set -- a b; printf "%s\n" $@')"

expect_outcome braced_positional accept \
    "$(build_repository braced_positional 'printf "%s\n" "${@}"')"

expect_outcome braced_positional_slice accept \
    "$(build_repository braced_slice 'printf "%s\n" "${@:2}"')"

# The negative direction. An address in a shell file must still be rejected,
# and the carve-out must not have turned the ban into a formality.
expect_outcome planted_address reject \
    "$(build_repository planted "# contact ${planted_address}")"

# The case the carve-out could most plausibly have broken: an address sharing a
# line with a legitimate expansion. A scan that stopped at the first permitted
# form, or that excused the whole line or the whole file, would miss this.
expect_outcome address_beside_expansion reject \
    "$(build_repository beside "printf '%s\n' \"\${entries[@]}\" # ${planted_address}")"

# The same pairing across separate lines of one file: the permitted forms are
# accepted and the address is still reported, in a single run over a single
# corpus. A carve-out that pardoned a file once any permitted form appeared in
# it would pass this file while the address sat two lines below.
expect_outcome address_and_expansion_in_one_file reject \
    "$(build_repository one_file "printf '%s\n' \"\${entries[@]}\"
printf '%s\n' \"\$@\"
# contact ${planted_address}")"

# An address in a non-shell file is unaffected by the carve-out and stays
# banned by the blanket rule.
non_shell_root="$workspace/non_shell"
mkdir -p "$non_shell_root"
git init -q "$non_shell_root"
git -C "$non_shell_root" config user.name owner
git -C "$non_shell_root" config user.email "$owner_identity"
git -C "$non_shell_root" config commit.gpgsign false
printf 'contact %s\n' "$planted_address" >"$non_shell_root/NOTES.md"
git -C "$non_shell_root" add NOTES.md
git -C "$non_shell_root" -c core.hooksPath=/dev/null commit -q -m 'Add notes'

non_shell_output=""
non_shell_status=0
non_shell_output="$(cd "$non_shell_root" && bash "$guard" 2>&1)" || non_shell_status=$?
checked=$((checked + 1))
if ((non_shell_status == 0)) ||
    [[ "$non_shell_output" != *"email-like or user-at-host text is tracked"* ]]; then
    printf 'public_repository_guard_self_test: an address in a non-shell file should still be rejected\n' >&2
    status=1
fi

# --- Without repository metadata the corpus is still scanned ----------------
# A build made from a release archive has no repository, and this is the guard
# whose absence costs the most there. A copy is installed in a source tree with
# the metadata removed, and it has to find the same planted address.
metadata_free_root="$workspace/metadata-free"
mkdir -p "$metadata_free_root/tools"
printf 'contact %s\n' "$planted_address" >"$metadata_free_root/NOTES.md"
cp "$guard" "$metadata_free_root/tools/check_public_repo.sh"
cp "$tools_directory/guard_corpus.sh" "$metadata_free_root/tools/guard_corpus.sh"
printf 'project(metadata_free)\n' >"$metadata_free_root/CMakeLists.txt"

metadata_free_output=""
metadata_free_status=0
metadata_free_output="$(cd "$metadata_free_root" &&
    bash tools/check_public_repo.sh 2>&1)" || metadata_free_status=$?
checked=$((checked + 1))
if ((metadata_free_status == 0)) ||
    [[ "$metadata_free_output" != *"email-like or user-at-host text is tracked"* ]]; then
    printf 'public_repository_guard_self_test: an address should be rejected in a tree without repository metadata: %s\n' \
        "$metadata_free_output" >&2
    status=1
fi

# The same tree with nothing planted has to pass, and has to say that
# attribution went unchecked rather than leaving it to be assumed.
rm -f "$metadata_free_root/NOTES.md"
clean_metadata_free_output=""
clean_metadata_free_status=0
clean_metadata_free_output="$(cd "$metadata_free_root" &&
    bash tools/check_public_repo.sh 2>&1)" || clean_metadata_free_status=$?
checked=$((checked + 1))
if ((clean_metadata_free_status != 0)) ||
    [[ "$clean_metadata_free_output" != *"carries no history"* ]]; then
    printf 'public_repository_guard_self_test: a clean tree without metadata should pass and say attribution was unchecked: %s\n' \
        "$clean_metadata_free_output" >&2
    status=1
fi

# --- A repository with no commits examined none -----------------------------
# The attribution loops read history. Over an empty history they read nothing
# and report success, which is the shape of a check that has stopped running.
no_commits_root="$workspace/no-commits"
mkdir -p "$no_commits_root"
git init -q "$no_commits_root"
printf 'notes\n' >"$no_commits_root/NOTES.md"
git -C "$no_commits_root" add NOTES.md

no_commits_output=""
no_commits_status=0
no_commits_output="$(cd "$no_commits_root" && bash "$guard" 2>&1)" ||
    no_commits_status=$?
checked=$((checked + 1))
if ((no_commits_status == 0)) ||
    [[ "$no_commits_output" != *"no commit was examined"* ]]; then
    printf 'public_repository_guard_self_test: a repository with no commits should be refused: %s\n' \
        "$no_commits_output" >&2
    status=1
fi

# --- An empty corpus scans nothing -----------------------------------------
# Every pattern this guard looks for is absent from a corpus with no files in
# it, so the scan reaches its success line without reading a byte.
empty_corpus_root="$workspace/empty-corpus"
mkdir -p "$empty_corpus_root"
git init -q "$empty_corpus_root"

empty_corpus_output=""
empty_corpus_status=0
empty_corpus_output="$(cd "$empty_corpus_root" && bash "$guard" 2>&1)" ||
    empty_corpus_status=$?
checked=$((checked + 1))
if ((empty_corpus_status == 0)) ||
    [[ "$empty_corpus_output" != *"the corpus is empty"* ]]; then
    printf 'public_repository_guard_self_test: an empty corpus should be refused: %s\n' \
        "$empty_corpus_output" >&2
    status=1
fi

# --- Unresolved merge conflict markers --------------------------------------
# The record was once published with nine of these in it, and every gate that
# existed said the corpus was fine. These scenarios pin both directions of the
# check that now refuses them.
#
# Each marker is composed at run time from a repetition count rather than
# written out, for the same reason the planted address is: this file is scanned
# by the guard like any other tracked source, and a literal marker at the start
# of a line here would make the self-test fail the rule it is demonstrating.
readonly marker_ours="$(printf '<%.0s' 1 2 3 4 5 6 7)"
readonly marker_base="$(printf '|%.0s' 1 2 3 4 5 6 7)"
readonly marker_split="$(printf '=%.0s' 1 2 3 4 5 6 7)"
readonly marker_theirs="$(printf '>%.0s' 1 2 3 4 5 6 7)"
readonly marker_ours_nine="$(printf '<%.0s' 1 2 3 4 5 6 7 8 9)"
readonly marker_base_nine="$(printf '|%.0s' 1 2 3 4 5 6 7 8 9)"
readonly marker_split_nine="$(printf '=%.0s' 1 2 3 4 5 6 7 8 9)"
readonly marker_theirs_nine="$(printf '>%.0s' 1 2 3 4 5 6 7 8 9)"
readonly marker_message='an unresolved merge conflict marker is tracked'

# Runs the guard and requires the marker reason specifically, so a scenario
# cannot pass because some unrelated pattern happened to fire on the fixture.
expect_marker_outcome() {
    local scenario="$1"
    local expectation="$2"
    local root="$3"

    local output=""
    local exit_status=0
    output="$(cd "$root" && bash "$guard" 2>&1)" || exit_status=$?

    checked=$((checked + 1))
    case "$expectation" in
        accept)
            if ((exit_status != 0)) || [[ "$output" != *"$success_message"* ]]; then
                printf 'public_repository_guard_self_test: %s should be accepted, but the guard said: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            fi
            ;;
        reject)
            if ((exit_status == 0)); then
                printf 'public_repository_guard_self_test: %s should be rejected\n' \
                    "$scenario" >&2
                status=1
            elif [[ "$output" != *"$marker_message"* ]]; then
                printf 'public_repository_guard_self_test: %s should be rejected for the conflict marker, but the guard said: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            fi
            ;;
    esac
}

# One scenario per marker spelling. The base marker only appears under the
# diff3 and zdiff3 conflict styles, which is exactly why it is the spelling
# most likely to be left out of a hand-written pattern.
expect_marker_outcome conflict_marker_ours reject \
    "$(build_repository_named marker_ours NOTES.md "${marker_ours} HEAD")"

expect_marker_outcome conflict_marker_base reject \
    "$(build_repository_named marker_base NOTES.md "${marker_base} merged common ancestors")"

expect_marker_outcome conflict_marker_split reject \
    "$(build_repository_named marker_split NOTES.md "${marker_split}")"

expect_marker_outcome conflict_marker_theirs reject \
    "$(build_repository_named marker_theirs NOTES.md "${marker_theirs} topic")"

# The shape the record was actually published in: a full conflicted region
# inside otherwise ordinary prose.
expect_marker_outcome conflict_region_in_prose reject \
    "$(build_repository_named marker_region NOTES.md "# Notes

${marker_ours} HEAD
one
${marker_split}
two
${marker_theirs} topic")"

# Git's marker size is configurable. This complete nine-character region is
# the regression case: reducing the guard back to an exact seven-character
# match leaves every marker in it undetected.
expect_marker_outcome conflict_region_with_nine_character_markers reject \
    "$(build_repository_named marker_region_nine NOTES.md "# Notes

${marker_ours_nine} HEAD
one
${marker_base_nine} merged common ancestors
base
${marker_split_nine}
two
${marker_theirs_nine} topic")"

# A marker in a source file rather than a document. The check carries no
# exclusion list, so the file's kind must not matter.
expect_marker_outcome conflict_marker_in_source reject \
    "$(build_repository_named marker_source script.sh "${marker_split}")"

# --- The discriminating direction -------------------------------------------
# A check that fires on anything marker-shaped would be unusable in a project
# whose prose contains rules and comparisons. These cases must all be accepted,
# and each isolates one property of the pattern.

# Not at the start of a line. This is the property that lets the guard scan
# itself and its own fixtures, so if it regresses this file stops being
# committable and the rule loses the exclusion-free form it was written for.
expect_marker_outcome marker_not_at_line_start accept \
    "$(build_repository_named marker_indented NOTES.md "prose mentioning ${marker_split} in passing")"

# Shorter than seven, for the same reason from the other side.
expect_marker_outcome rule_shorter_than_marker accept \
    "$(build_repository_named marker_short NOTES.md "$(printf '=%.0s' 1 2 3 4 5 6)")"

# Seven characters but no terminator: the run continues into other text, so the
# line is prose rather than a marker.
expect_marker_outcome marker_length_run_without_terminator accept \
    "$(build_repository_named marker_runon NOTES.md "${marker_split}not-a-marker")"

# A clean document with no marker-shaped text at all, to keep the accepting
# direction from resting only on near-miss fixtures.
expect_marker_outcome clean_document accept \
    "$(build_repository_named marker_clean NOTES.md "# Notes

Ordinary prose with nothing marker-shaped in it.")"

if ((status != 0)); then
    exit "$status"
fi

printf 'public_repository_guard_self_test: %d scenarios are enforced\n' \
    "$checked"

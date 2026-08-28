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

# --- Third-party project references and derivative framing ------------------
# Tracked text describes this project's own behaviour. It must not identify
# another project of the same kind, and it must not present a decision as taken
# from or measured against one. Upstream dependencies are the opposite case and
# must keep passing, so both directions are pinned here.
#
# Every planted phrase is assembled from adjacent string literals rather than
# written out. This file is scanned by the guard like any other tracked source,
# and a literal violation here would have to be excused by a file-level
# exclusion - the same exclusion the at-sign carve-out above was written to
# avoid. The accepted fixtures are written literally, because the rules permit
# them: this file's own tracked form is part of what it demonstrates.
readonly forge_host="git""hub.com"
readonly other_forge_host="git""lab.com"
readonly third_party_slug="someone/some-other-thing"

readonly forge_message='a third-party hosted-source reference is tracked'
readonly framing_message='derivative or comparative framing is tracked'
readonly heading_message='a credits, prior-work, or positioning section heading is tracked'
readonly positioning_message='comparative positioning within a field is tracked'

# Runs the guard in one throwaway repository and requires either acceptance or
# rejection for a named reason. Requiring the reason is what stops a scenario
# passing because an unrelated rule fired on the fixture.
expect_reason_outcome() {
    local scenario="$1"
    local expectation="$2"
    local reason="$3"
    local root="$4"

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
            elif [[ "$output" != *"$reason"* ]]; then
                printf 'public_repository_guard_self_test: %s should be rejected for "%s", but the guard said: %s\n' \
                    "$scenario" "$reason" "$output" >&2
                status=1
            fi
            ;;
    esac
}

# A throwaway repository holding files at caller-chosen paths, so a scenario
# can put its fixture inside the vendored dependency tree and prove the scope
# is a path scope rather than a blanket pardon.
build_repository_at_paths() {
    local name="$1"
    shift

    local root="$workspace/$name"
    mkdir -p "$root"
    git init -q "$root"
    git -C "$root" config user.name owner
    git -C "$root" config user.email "$owner_identity"
    git -C "$root" config commit.gpgsign false

    while (($# > 1)); do
        local file_path="$1"
        local body="$2"
        shift 2
        mkdir -p "$root/$(dirname "$file_path")"
        printf '%s\n' "$body" >"$root/$file_path"
        git -C "$root" add "$file_path"
    done

    git -C "$root" -c core.hooksPath=/dev/null commit -q -m 'Add files'
    printf '%s' "$root"
}

# The hosted-source rule. Both reference spellings, and a second forge host, so
# the rule is not resting on one vendor's domain.
expect_reason_outcome forge_url_reference reject "$forge_message" \
    "$(build_repository_named forge_url NOTES.md \
        "Details live at https://${forge_host}/${third_party_slug}.")"

expect_reason_outcome forge_scp_reference reject "$forge_message" \
    "$(build_repository_named forge_scp NOTES.md \
        "Cloned from ${forge_host}:${third_party_slug}.git")"

expect_reason_outcome second_forge_host reject "$forge_message" \
    "$(build_repository_named forge_second NOTES.md \
        "Details live at https://${other_forge_host}/${third_party_slug}.")"

# This repository's own owner is the one owner a reference may carry, which is
# what keeps the installation instructions committable.
expect_reason_outcome own_owner_reference accept "$forge_message" \
    "$(build_repository_named forge_own NOTES.md \
        "Clone https://${forge_host}/ghreprimand/odysea.git and build it.")"

# Our own reference sharing a line with a third-party one. A filter that
# stopped at the first permitted match, or that pardoned the whole line once
# one appeared, would miss this.
expect_reason_outcome third_party_beside_own reject "$forge_message" \
    "$(build_repository_named forge_beside NOTES.md \
        "Clone https://${forge_host}/ghreprimand/odysea.git; see also https://${forge_host}/${third_party_slug}.")"

# The account-scoped no-reply domain is a bare host with no owner after it, and
# it appears throughout the attribution policy. Requiring the separator is what
# keeps that from reading as a repository reference.
expect_reason_outcome noreply_domain_is_not_a_reference accept "$forge_message" \
    "$(build_repository_named forge_noreply NOTES.md \
        "Attribution uses the account-scoped users.noreply.${forge_host} form.")"

# The vendored dependency tree carries the upstream's own provenance and
# license text, reproduced as its terms require, so a reference there passes.
expect_reason_outcome vendored_dependency_reference accept "$forge_message" \
    "$(build_repository_at_paths forge_vendored \
        app/third_party/typeface/LICENSE.txt \
        "Upstream project: https://${forge_host}/${third_party_slug}")"

# The same text one directory outside that tree is still refused, which is what
# makes the exclusion a path scope instead of a general exemption.
expect_reason_outcome reference_outside_vendored_tree reject "$forge_message" \
    "$(build_repository_at_paths forge_outside \
        app/third_party_notes.md \
        "Upstream project: https://${forge_host}/${third_party_slug}")"

# Derivative framing: a decision presented as taken from somewhere else.
expect_reason_outcome derivation_phrase reject "$framing_message" \
    "$(build_repository_named framing_derivation NOTES.md \
        "The outline treatment was inspir""ed by an earlier design.")"

expect_reason_outcome provenance_phrase reject "$framing_message" \
    "$(build_repository_named framing_provenance NOTES.md \
        "The undo journal was port""ed from an earlier implementation.")"

# Rivalry framing: a relationship of contest or descent rather than a property
# of this project.
expect_reason_outcome descent_phrase reject "$framing_message" \
    "$(build_repository_named framing_descent NOTES.md \
        "This shell began as a fork ""of an earlier browser.")"

expect_reason_outcome rivalry_noun reject "$framing_message" \
    "$(build_repository_named framing_rivalry NOTES.md \
        "No competi""tor ships this transfer behaviour.")"

expect_reason_outcome survey_phrase reject "$framing_message" \
    "$(build_repository_named framing_survey NOTES.md \
        "The prior ""art for bulk rename is uneven.")"

expect_reason_outcome parity_phrase reject "$framing_message" \
    "$(build_repository_named framing_parity NOTES.md \
        "The archive surface reaches feature pari""ty with what is already available.")"

# The comparative construction: the comparison word immediately governing a
# set this project is being placed against.
expect_reason_outcome comparative_construction reject "$framing_message" \
    "$(build_repository_named framing_comparative NOTES.md \
        "Directory loads are faster th""an most existing implementations.")"

expect_reason_outcome comparative_unlike reject "$framing_message" \
    "$(build_repository_named framing_unlike NOTES.md \
        "Unlike ""other file mana""gers, the trash path is transactional.")"

# The enumerated peer group. No comparative operator, no name: a qualifier in
# front of the project category is already a statement about a set of peers.
expect_reason_outcome peer_category_group reject "$framing_message" \
    "$(build_repository_named framing_peer_group NOTES.md \
        "Most desktop file mana""gers present directories first.")"

# The same peer group reached through a verb rather than a qualifier, and with
# a category noun the enumerated-group rule does not cover.
expect_reason_outcome peer_group_verb_construction reject "$framing_message" \
    "$(build_repository_named framing_peer_verb NOTES.md \
        "The palette filters rows as ""other desktop tools do.")"

# --- The discriminating direction -------------------------------------------
# These must all be accepted. A rule that reported them would be reporting
# correct prose, and a rule that reports correct prose is one people route
# around rather than obey.

# A weak comparative used about this project's own behaviour.
expect_reason_outcome weak_comparative_in_technical_prose accept "$framing_message" \
    "$(build_repository_named framing_weak NOTES.md \
        "The watch is incremental, unlike the polling approach it replaced.
Directory loads are faster than the linear scan they replaced.
Selection behaves compared to the anchor rather than the cursor.")"

# Interoperability prose. The thumbnail cache is shared, so what the rest of
# the system can read is a required subject and must keep passing. The peer
# category is what is refused, not the fact that other software exists.
expect_reason_outcome interoperability_statement accept "$framing_message" \
    "$(build_repository_named framing_interop NOTES.md \
        "The cache file name is the digest of these exact bytes, so a divergence
would produce a private cache that no other application can find.")"

# Dependency names are cited, not avoided.
expect_reason_outcome dependency_citation accept "$framing_message" \
    "$(build_repository_named framing_dependency NOTES.md \
        "Built with CMake and Ninja against Qt 6, compiled by Clang or GCC, and
bundling one typeface under its own open font license.")"

# The heading rule. The words below are unremarkable in running prose and only
# become a survey of other work when they head a section.
expect_reason_outcome credits_heading reject "$heading_message" \
    "$(build_repository_named heading_credits NOTES.md \
        "# Notes

## Credi""ts")"

expect_reason_outcome acknowledgements_heading reject "$heading_message" \
    "$(build_repository_named heading_acknowledgements NOTES.md \
        "# Notes

## Acknowledge""ments")"

expect_reason_outcome related_work_heading reject "$heading_message" \
    "$(build_repository_named heading_related NOTES.md \
        "# Notes

### Related ""work")"

# The same word as an ordinary verb in running prose. This exact use is in the
# tracked corpus, so a rule matching the bare word would refuse it.
expect_reason_outcome heading_word_in_running_prose accept "$heading_message" \
    "$(build_repository_named heading_prose NOTES.md \
        "The assertion must hold on a surface this test credits.")"

# Commit attribution is a policy this project documents under that name, so the
# heading is deliberately outside the rule.
expect_reason_outcome attribution_heading_is_permitted accept "$heading_message" \
    "$(build_repository_named heading_attribution NOTES.md \
        "# Notes

## Attribution

Commits carry the repository owner's account-scoped no-reply identity.")"

# --- Comparative positioning within a field ---------------------------------
# The argument that needs no peer at all: a survey of a field, a gap found in
# it, and this project placed in the gap. It carries no name and no derivation
# phrase, so every rule above passes it, and it is the same claim.

expect_reason_outcome field_survey_framing reject "$positioning_message" \
    "$(build_repository_named positioning_landscape NOTES.md \
        "The Linux file-mana""ger landscape spans a wide range of designs.")"

expect_reason_outcome placement_in_a_gap reject "$positioning_message" \
    "$(build_repository_named positioning_gap NOTES.md \
        "OdySea occupies the ""gap between the first two categories.")"

expect_reason_outcome two_poles_and_a_middle reject "$positioning_message" \
    "$(build_repository_named positioning_poles NOTES.md \
        "Desktop tools tend to sit ""at two ext""remes; OdySea aims for the middle grou""nd.")"

expect_reason_outcome as_good_as_any_construction reject "$positioning_message" \
    "$(build_repository_named positioning_asany NOTES.md \
        "A mouse-driven user should find it as natural as any main""stream desktop file manager.")"

expect_reason_outcome positioning_section_heading reject "$heading_message" \
    "$(build_repository_named positioning_heading NOTES.md \
        "# Notes

## Position""ing")"

# The discriminating direction. A scope statement about this project alone must
# pass, including one that says what it does not do.
expect_reason_outcome own_scope_statement accept "$positioning_message" \
    "$(build_repository_named positioning_scope NOTES.md \
        "OdySea browses what is mounted on this machine. It follows the
freedesktop specifications for trash, thumbnails, and MIME handling.

## Scope boundaries

- Not a cross-device virtual filesystem or cloud sync platform.
- Not modal by default. Modal keybindings are opt-in, never forced.")"

# A middle value in an engineering trade-off is not a market position. The
# words carry a real technical sense and must stay usable.
expect_reason_outcome engineering_trade_off_prose accept "$positioning_message" \
    "$(build_repository_named positioning_tradeoff NOTES.md \
        "The publication interval grows with the listing, so the cost sits
between a single batch and one signal per row.")"

# --- The tracked dependency citations, as they actually stand ---------------
# The strongest accepting case available: the real tracked files that cite
# upstream dependencies, copied verbatim into a throwaway repository and put to
# the guard. A paraphrase would only prove the rules accept a paraphrase.
dependency_corpus_root="$workspace/dependency-corpus"
mkdir -p "$dependency_corpus_root"
git init -q "$dependency_corpus_root"
git -C "$dependency_corpus_root" config user.name owner
git -C "$dependency_corpus_root" config user.email "$owner_identity"
git -C "$dependency_corpus_root" config commit.gpgsign false

readonly repository_root="$(cd "$tools_directory/.." && pwd -P)"
dependency_files_copied=0
for relative_path in \
    README.md \
    CONTRIBUTING.md \
    docs/STACK.md \
    app/third_party/victor-mono/OFL.txt \
    app/third_party/victor-mono/README.md; do
    [[ -f "$repository_root/$relative_path" ]] || continue
    mkdir -p "$dependency_corpus_root/$(dirname "$relative_path")"
    cp "$repository_root/$relative_path" "$dependency_corpus_root/$relative_path"
    git -C "$dependency_corpus_root" add "$relative_path"
    dependency_files_copied=$((dependency_files_copied + 1))
done
git -C "$dependency_corpus_root" -c core.hooksPath=/dev/null \
    commit -q -m 'Add the tracked dependency citations'

# A floor on the fixture itself. If those files are renamed the copy loop
# silently produces an empty corpus, and an empty corpus passes every rule
# above without reading a dependency citation at all.
readonly expected_dependency_files=5
checked=$((checked + 1))
if ((dependency_files_copied != expected_dependency_files)); then
    printf 'public_repository_guard_self_test: %d dependency-citation file(s) were copied, expected %d; the fixture no longer holds what it names\n' \
        "$dependency_files_copied" "$expected_dependency_files" >&2
    status=1
fi

expect_reason_outcome tracked_dependency_citations accept "$forge_message" \
    "$dependency_corpus_root"

if ((status != 0)); then
    exit "$status"
fi

# A floor on the suite's own reporting. A scenario that stops running leaves
# the remaining ones green, and the summary line below would still be printed.
readonly expected_scenarios=60
if ((checked != expected_scenarios)); then
    printf 'public_repository_guard_self_test: %d scenario(s) reported a result, expected %d; the suite did not run in full\n' \
        "$checked" "$expected_scenarios" >&2
    exit 1
fi

printf 'public_repository_guard_self_test: %d scenarios are enforced\n' \
    "$checked"

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

# --- A group survey with the framing removed --------------------------------
# Every rule above this point keys on framing. Delete the survey sentence, the
# placement verb and the section heading, keep the labelled groups and the
# names, and the same claim is made in fewer words. These scenarios pin the two
# rules that read the shape of the list instead of the words around it.
#
# The names below are invented. A real one written into the file whose visible
# purpose is to suppress it would publish that name in tracked text, which is
# the outcome the rules exist to prevent; the shape is what is under test and
# the shape does not need a real name to be reproduced.
readonly class_label_message='a list item is labelled as a class of file manager'
readonly enumeration_message='an enumeration of named items is qualified by a limitation'

# The label, on its own. No enumeration and no limitation, so only the label
# rule can fire.
expect_reason_outcome bold_class_label reject "$class_label_message" \
    "$(build_repository_named label_bold NOTES.md \
        "# Notes

- **Ambient file mana""gers** — a group with its own conventions.")"

expect_reason_outcome italic_class_label reject "$class_label_message" \
    "$(build_repository_named label_italic NOTES.md \
        "# Notes

- *Desktop explo""rers* — a group with its own conventions.")"

expect_reason_outcome plain_label_closed_by_a_colon reject "$class_label_message" \
    "$(build_repository_named label_colon NOTES.md \
        "# Notes

- Graphical file brow""sers: a group with its own conventions.")"

expect_reason_outcome bare_plural_class_label reject "$class_label_message" \
    "$(build_repository_named label_plural NOTES.md \
        "# Notes

- **File mana""gers** — a group with its own conventions.")"

# The discriminating direction for the label rule. A bullet about this program
# is singular and must keep passing, and so must a sentence that runs through
# the same words without closing a label.
expect_reason_outcome singular_self_reference_label accept "$class_label_message" \
    "$(build_repository_named label_singular NOTES.md \
        "# Notes

- **File manager** — the application this repository builds.")"

expect_reason_outcome sentence_running_through_the_category accept "$class_label_message" \
    "$(build_repository_named label_sentence NOTES.md \
        "# Notes

- The graphical file manager loads a directory off the main thread.")"

# The enumeration, on its own. No label, so only the enumeration rule can fire.
expect_reason_outcome three_named_items_and_a_limitation reject "$enumeration_message" \
    "$(build_repository_named enumeration_three NOTES.md \
        "# Notes

- Aure""lia, Bas""tion, Corv""ane. Fast and complete, but limited to one screen.")"

expect_reason_outcome four_items_two_of_them_named reject "$enumeration_message" \
    "$(build_repository_named enumeration_four NOTES.md \
        "# Notes

- Aure""lia, sprig, tanner, Corv""ane Deck. Quick, but constrained by the grid.")"

# The limitation usually sits further down the same bullet than the names do,
# which is why the scan is block-scoped. This fixture separates them by a line.
expect_reason_outcome limitation_on_a_later_line reject "$enumeration_message" \
    "$(build_repository_named enumeration_wrapped NOTES.md \
        "# Notes

- Aure""lia, Bas""tion, Corv""ane. Feature-complete and well integrated with
  their desktop environments, but tied to those environments' conventions.")"

# The discriminating directions for the enumeration rule. Each fixture below
# carries a list the rule does match, so that what separates it from a refusal
# is the one condition the scenario is named for. A fixture whose list the rule
# never matches at all would pass whatever the guard did to the rest of the
# condition, and four of these started life that way.
expect_reason_outcome enumeration_without_a_limitation accept "$enumeration_message" \
    "$(build_repository_named enumeration_plain NOTES.md \
        "# Notes

- Aure""lia, Bas""tion, Corv""ane. These are the three sample directories.")"

# A block ends at a blank line, at the next list item, at a heading, and at the
# end of a file. Each of the four fixtures below states its limitation just past
# one of those boundaries and nowhere else, so a boundary that stops being
# honoured attaches the limitation to the list and the scenario fails.
# The limitation below is plain prose rather than a second list item. Written
# as a bullet it sat past two boundaries at once - the blank line and the start
# of another item - so the list-item boundary alone held the fixture accepted
# and the blank-line clause could be deleted with the suite still green. One
# character was the difference between a scenario and a decoration, and the
# criterion that missed it - "does the fixture carry a list the rule matches" -
# is necessary but not sufficient. What a fixture has to prove is that the
# condition it is named for is the one deciding its outcome.
expect_reason_outcome limitation_after_a_blank_line accept "$enumeration_message" \
    "$(build_repository_named enumeration_split NOTES.md \
        "# Notes

- Aure""lia, Bas""tion, Corv""ane.

The scan is bounded, but the bound is configurable.")"

expect_reason_outcome limitation_in_the_next_list_item accept "$enumeration_message" \
    "$(build_repository_named enumeration_next_item NOTES.md \
        "# Notes

- Aure""lia, Bas""tion, Corv""ane.
- The scan is bounded, but the bound is configurable.")"

expect_reason_outcome limitation_below_a_heading accept "$enumeration_message" \
    "$(build_repository_named enumeration_heading NOTES.md \
        "# Notes

Aure""lia, Bas""tion, Corv""ane.
## Bounds
The scan is bounded, but the bound is configurable.")"

enumeration_across_files_root="$workspace/enumeration-across-files"
mkdir -p "$enumeration_across_files_root"
git init -q "$enumeration_across_files_root"
git -C "$enumeration_across_files_root" config user.name owner
git -C "$enumeration_across_files_root" config user.email "$owner_identity"
git -C "$enumeration_across_files_root" config commit.gpgsign false
printf 'Aure%s.\n' \
    "lia, Bas""tion, Corv""ane" >"$enumeration_across_files_root/A-NOTES.md"
printf 'The scan is bounded, but the bound is configurable.\n' \
    >"$enumeration_across_files_root/B-NOTES.md"
git -C "$enumeration_across_files_root" add A-NOTES.md B-NOTES.md
git -C "$enumeration_across_files_root" -c core.hooksPath=/dev/null \
    commit -q -m 'Add two files'

expect_reason_outcome limitation_in_the_next_file accept "$enumeration_message" \
    "$enumeration_across_files_root"

# The thresholds, from below. An ordinary comma list of this project's own
# operations closes its clause and sits beside a limitation, so only the count
# of capitalised items keeps it out of the report.
expect_reason_outcome ordinary_comma_list_of_operations accept "$enumeration_message" \
    "$(build_repository_named enumeration_operations NOTES.md \
        "# Notes

The reversible operations are copy, move, rename, and trash. The history is
bounded, but a reversal can still fail.")"

expect_reason_outcome three_items_two_of_them_capitalised accept "$enumeration_message" \
    "$(build_repository_named enumeration_columns NOTES.md \
        "# Notes

The columns are Name, Size, and date. Their order is configurable, but the
default is by name.")"

# The legitimate hit the closing condition was measured against. This is the
# real design document's shape: three capitalised input names that are the
# subject of a verb, in a paragraph that also carries a limitation word. The
# sentence runs on past the list, so the list is not the whole clause and the
# line is not reported.
expect_reason_outcome input_names_in_correct_prose accept "$enumeration_message" \
    "$(build_repository_named enumeration_inputs NOTES.md \
        "# Notes

Right-click, Menu, and Shift+F10 open the same shared context actions, but a
keyboard request anchors to the current delegate instead.")"

# The two historical shapes, side by side. The first carries the scaffolding
# and is refused by the rules that read framing; the second is the same claim
# with the scaffolding deleted, and is refused only by the two rules above.
# Both assertions on the second fixture name a different rule, so reverting
# either one alone fails its own scenario and leaves the other standing.
readonly framing_stripped_survey="# Notes

- **Ambient file mana""gers** — Aure""lia, Bas""tion, Corv""ane
  (Skiff""wright). Feature-complete and well integrated with their desktop
  environments, but tied to those environments' conventions.
- **Terminal file mana""gers** — Nym""bus, sprig, tanner, Tallow""mere. Extremely
  fast and keyboard-driven, but constrained by the terminal grid and text-only
  rendering."

expect_reason_outcome group_survey_with_scaffolding reject "$heading_message" \
    "$(build_repository_named survey_scaffolded NOTES.md \
        "# Notes

## Position""ing

$framing_stripped_survey")"

expect_reason_outcome framing_stripped_survey_label reject "$class_label_message" \
    "$(build_repository_named survey_stripped_label NOTES.md \
        "$framing_stripped_survey")"

expect_reason_outcome framing_stripped_survey_enumeration reject "$enumeration_message" \
    "$(build_repository_named survey_stripped_enumeration NOTES.md \
        "$framing_stripped_survey")"

# --- The serial comma, which is optional in English -------------------------
# Omitting the comma before the final conjunction merges the last two items
# into one, and a merged item beginning with a lowercase conjunction stops
# counting as a name. One character is the difference, and the omitted form is
# the more common of the two styles, so both spellings are pinned.
expect_reason_outcome enumeration_with_a_serial_comma reject "$enumeration_message" \
    "$(build_repository_named enumeration_serial NOTES.md \
        "# Notes

- Aure""lia, Bas""tion, Corv""ane, and Nem""ora. Capable, but bound to one grid.")"

expect_reason_outcome enumeration_without_a_serial_comma reject "$enumeration_message" \
    "$(build_repository_named enumeration_no_serial NOTES.md \
        "# Notes

- Aure""lia, Bas""tion, Corv""ane and Nem""ora. Capable, but bound to one grid.")"

# Three capitalised names satisfy the threshold on their own, so a merged final
# item is invisible in that shape: the count is what has to notice the missing
# comma. This fixture puts two lowercase names in the middle, where the four
# item total is the only thing that carries it over the threshold, and the
# fourth item exists only if the conjunction is counted as a boundary.
expect_reason_outcome four_items_without_a_serial_comma reject "$enumeration_message" \
    "$(build_repository_named enumeration_four_no_serial NOTES.md \
        "# Notes

- Aure""lia, sprig, tanner and Nem""ora. Quick, but constrained by the grid.")"

expect_reason_outcome enumeration_joined_by_or reject "$enumeration_message" \
    "$(build_repository_named enumeration_or NOTES.md \
        "# Notes

- Aure""lia, Bas""tion, Corv""ane or Nem""ora. Capable, but bound to one grid.")"

# The counting has to survive the serial comma without inflating the total. A
# comma immediately followed by the conjunction is two boundaries in a row, and
# an empty field between them would push a three-item list over the four-item
# threshold - which is correct prose being reported for its punctuation.
expect_reason_outcome three_items_with_a_serial_comma accept "$enumeration_message" \
    "$(build_repository_named enumeration_serial_three NOTES.md \
        "# Notes

The columns are Name, Size, and date. Their order is configurable, but the
default is by name.")"

# --- A table, which every prose rule reads straight past --------------------
# A survey laid out as a table defeated all five rules at once. The label rule
# needs a list marker a table row does not have, and the enumeration needs its
# run to close a clause, while a run inside a cell is closed by a cell wall.
# The rows below are the same claim as the bullets above them.
#
# Every fixture here commits a source file as well, so that none of these
# rejections can be coming from an empty vocabulary corpus. A repository with
# no sources in it has no vocabulary for anything to belong to, and a scenario
# that passes for the absence of a file proves nothing about the rule.
readonly unrelated_source='int main() { return 0; }'

expect_reason_outcome table_of_named_items reject "$enumeration_message" \
    "$(build_repository_at_paths table_named_items \
        docs/NOTES.md "## Field notes

| Program | Weakness |
| --- | --- |
| Aure""lia, Bas""tion, Corv""ane | Slow on large trees, but stable |
| Nym""bus, Spr""igg, Tallow""mere | Fast on small ones |" \
        app/src/shell.cpp "$unrelated_source")"

# One name a row, which is the more natural way to write the same table and
# the one a per-row item count would read straight past.
expect_reason_outcome table_with_one_name_a_row reject "$enumeration_message" \
    "$(build_repository_at_paths table_one_a_row \
        docs/NOTES.md "## Field notes

| Program | Weakness |
| --- | --- |
| Aure""lia | Fast, but limited |
| Bas""tion | Quick, but tied to the grid |
| Corv""ane | Complete, but heavy |" \
        app/src/shell.cpp "$unrelated_source")"

# The scope covers the two extensions tracked prose is written in, so moving
# the table out of Markdown does not move it out of the rule.
expect_reason_outcome table_in_a_text_file reject "$enumeration_message" \
    "$(build_repository_at_paths table_text_file \
        docs/NOTES.txt "Field notes

| Program | Weakness |
| --- | --- |
| Aure""lia | Fast, but limited |
| Bas""tion | Quick, but tied to the grid |
| Corv""ane | Complete, but heavy |" \
        app/src/shell.cpp "$unrelated_source")"

# The discriminating directions. Each fixture below carries a table the scan
# does read, so what decides its outcome is the one condition it is named for.
#
# A table with no limitation in it is a table, not an assessment of a field.
expect_reason_outcome table_without_a_limitation accept "$enumeration_message" \
    "$(build_repository_at_paths table_no_limitation \
        docs/NOTES.md "## Field notes

| Program | Behaviour |
| --- | --- |
| Aure""lia | Loads a directory off the main thread |
| Bas""tion | Renders a grid |
| Corv""ane | Watches for changes |" \
        app/src/shell.cpp "$unrelated_source")"

# The threshold from below. Two names and a limitation are a comparison of two
# things, which prose does legitimately; three are a survey.
expect_reason_outcome table_with_two_names accept "$enumeration_message" \
    "$(build_repository_at_paths table_two_names \
        docs/NOTES.md "## Field notes

| Program | Weakness |
| --- | --- |
| Aure""lia | Fast, but limited |
| Bas""tion | Quick, but tied to the grid |" \
        app/src/shell.cpp "$unrelated_source")"

# A header row names the columns. That is a property of the layout rather than
# of the writing, so it is not counted - and this table has name-shaped cells
# nowhere else, so counting the header would report it.
expect_reason_outcome table_header_is_not_counted accept "$enumeration_message" \
    "$(build_repository_at_paths table_header \
        docs/NOTES.md "## Field notes

| Aure""lia | Bas""tion | Corv""ane |
| --- | --- | --- |
| loads a directory | scans a tree, but stops at a mount | renders a grid |" \
        app/src/shell.cpp "$unrelated_source")"

# A stated limit rather than a discovery. The table scan reads prose files
# only, because two tracked shell lines begin with a pipe where a pipeline was
# continued onto the next line, and a rule that pretended to tell a pipeline
# from a table would be guessing. A survey laid out inside a source comment is
# outside this rule and is caught by review.
expect_reason_outcome table_shape_outside_prose_is_not_scanned accept "$enumeration_message" \
    "$(build_repository_at_paths table_in_source \
        app/src/notes.cpp "// | Aure""lia | Fast, but limited |
// | Bas""tion | Quick, but tied to the grid |
// | Corv""ane | Complete, but heavy |
$unrelated_source")"

# --- This project's own vocabulary, which is what the shape cannot tell -----
# The refused shape and the shape this repository writes constantly are the
# same shape: a run of capitalised names closed by a full stop, beside a
# sentence stating a limitation. What separates them is that the names in one
# of them are identifiers this program implements. A peer product's name
# appears in exactly one place, the sentence that surveys it.
readonly profile_line='- **Effect profiles** - Sl'"ate, Ha""lo, Dr""ift, Ve""il, and Be""am. Every profile is honoured, but a weak GPU falls back silently."
readonly profile_source='enum class Profile { Sl'"ate, Ha""lo, Dr""ift, Ve""il, Be""am };"
readonly partial_source='enum class Profile { Sl'"ate, Ha""lo, Dr""ift, Ve""il };"

expect_reason_outcome own_vocabulary_enumeration accept "$enumeration_message" \
    "$(build_repository_at_paths vocabulary_known \
        docs/DESIGN.md "## Effects

$profile_line" \
        app/src/effects.cpp "$profile_source")"

# All or nothing. One unrecognised name puts the whole run back in the report,
# so a survey cannot be smuggled in beside four words that happen to be
# settings.
expect_reason_outcome one_name_outside_the_vocabulary reject "$enumeration_message" \
    "$(build_repository_at_paths vocabulary_partial \
        docs/DESIGN.md "## Effects

$profile_line" \
        app/src/effects.cpp "$partial_source")"

# Documentation does not establish vocabulary. If it did, a survey would
# authorise itself: name six programs in a table and mention them once in a
# paragraph, and the paragraph would excuse the table.
expect_reason_outcome vocabulary_established_only_in_prose reject "$enumeration_message" \
    "$(build_repository_at_paths vocabulary_prose_only \
        docs/DESIGN.md "## Effects

$profile_line" \
        docs/GLOSSARY.md "$profile_source" \
        app/src/shell.cpp "$unrelated_source")"

# An upstream's identifiers are its vocabulary, not this project's, so the
# vendored tree is excluded from the lookup exactly as it is from the
# hosted-source rule.
expect_reason_outcome vocabulary_from_the_vendored_tree reject "$enumeration_message" \
    "$(build_repository_at_paths vocabulary_vendored \
        docs/DESIGN.md "## Effects

$profile_line" \
        app/third_party/vendor/vendor.cpp "$profile_source")"

# The same discriminator carries the table rule. A reference table of this
# program's own shortcuts is the shape the table rule would otherwise report.
expect_reason_outcome table_of_project_vocabulary accept "$enumeration_message" \
    "$(build_repository_at_paths table_vocabulary \
        docs/KEYS.md "## Shortcuts

| Shortcut | Action |
| --- | --- |
| Ctrl+C | Copies the selection, but not its paths |
| Ctrl+V | Pastes into the current directory |
| Ctrl+Z | Reverses the last operation, though not every kind |" \
        app/src/keys.cpp 'const char *keys[] = {"Ctrl+C", "Ctrl+V", "Ctrl+Z"};')"

# --- Text this project has no authority to rewrite --------------------------
# The two shape rules, and only these two, read a narrower corpus. They match
# on form rather than word choice, so they can land on the GPL, on an upstream
# license that requires verbatim distribution, and on archived record entries a
# separate gate refuses to let anyone modify. A rule that stands permanently
# red over text nobody may edit is a rule that gets deleted.
#
# The boundary is authority, not convenience: documentation this project writes
# and the live record are both still scanned, and the same text is reported
# there. Measured against the tracked corpus the three exclusions currently
# excuse nothing at all - they are preventive - so the scenarios below are the
# only thing holding them to their stated width.
readonly archived_exempt_line="establi""shed implementa""tion rather than from this one."

expect_reason_outcome survey_in_the_license accept "$class_label_message" \
    "$(build_repository_at_paths shape_license \
        LICENSE "$framing_stripped_survey")"

expect_reason_outcome survey_in_the_vendored_tree accept "$class_label_message" \
    "$(build_repository_at_paths shape_vendored \
        app/third_party/victor-mono/README.md "$framing_stripped_survey")"

# One directory outside the vendored tree the identical text is still reported,
# so the exclusion is a path scope rather than a pardon for the words.
expect_reason_outcome survey_outside_the_vendored_tree reject "$class_label_message" \
    "$(build_repository_at_paths shape_beside_vendored \
        app/third_party.md "$framing_stripped_survey")"

expect_reason_outcome survey_in_the_archived_record accept "$class_label_message" \
    "$(build_repository_at_paths shape_archive \
        docs/devlog/2026-01.md "$archived_exempt_line

$framing_stripped_survey")"

# The live record is not excluded, and that is where the seam sits: an entry is
# scanned by these rules while it can still be reworded, and stops being
# scanned only once it has been archived and made immutable.
expect_reason_outcome survey_in_the_live_record reject "$class_label_message" \
    "$(build_repository_at_paths shape_live_record \
        DEVLOG.md "$framing_stripped_survey")"

expect_reason_outcome survey_in_documentation reject "$class_label_message" \
    "$(build_repository_at_paths shape_documentation \
        docs/DESIGN.md "$framing_stripped_survey")"

# The enumeration half of the same boundary, named separately, so reverting
# either rule's exclusion fails its own scenario.
expect_reason_outcome enumeration_in_the_archived_record accept "$enumeration_message" \
    "$(build_repository_at_paths enumeration_archive \
        docs/devlog/2026-01.md "$archived_exempt_line

- Aure""lia, Bas""tion, Corv""ane. Capable, but bound to one grid." \
        app/src/shell.cpp "$unrelated_source")"

expect_reason_outcome enumeration_in_the_live_record reject "$enumeration_message" \
    "$(build_repository_at_paths enumeration_live_record \
        DEVLOG.md "- Aure""lia, Bas""tion, Corv""ane. Capable, but bound to one grid." \
        app/src/shell.cpp "$unrelated_source")"

# --- A scan whose status is never read --------------------------------------
# A search tool separates "found nothing" from "could not run". Collapsing both
# into a tolerated failure makes a pattern that will not compile look exactly
# like a clean corpus, and both scans below print nothing when they fail.
#
# A filter that will not compile cannot be produced from a fixture; it has to
# come from the guard itself. So a copy is damaged deliberately and its exit
# status and reason are asserted. The copy is compared against the original
# first: a patch that failed to apply would leave the working guard in place
# and the scenario would report a pass for a mutation that never landed.
readonly guard_library="$tools_directory/guard_corpus.sh"

expect_patched_guard_rejection() {
    local scenario="$1"
    local reason="$2"
    local patch="$3"
    local root="$4"

    checked=$((checked + 1))

    local tools_copy="$workspace/$scenario-tools"
    mkdir -p "$tools_copy"
    cp "$guard" "$tools_copy/check_public_repo.sh"
    cp "$guard_library" "$tools_copy/guard_corpus.sh"
    sed -i "$patch" "$tools_copy/check_public_repo.sh"

    if cmp -s "$guard" "$tools_copy/check_public_repo.sh"; then
        printf 'public_repository_guard_self_test: %s did not change the guard, so nothing was measured\n' \
            "$scenario" >&2
        status=1
        return
    fi

    local output=""
    local exit_status=0
    output="$(cd "$root" && bash "$tools_copy/check_public_repo.sh" 2>&1)" ||
        exit_status=$?

    if ((exit_status == 0)); then
        printf 'public_repository_guard_self_test: %s should be rejected\n' \
            "$scenario" >&2
        status=1
    elif [[ "$output" != *"$reason"* ]]; then
        printf 'public_repository_guard_self_test: %s should be rejected for "%s", but the guard said: %s\n' \
            "$scenario" "$reason" "$output" >&2
        status=1
    fi
}

clean_root="$(build_repository_named patched_guard_corpus NOTES.md \
    'Ordinary prose with nothing in it for any rule to find.')"

expect_patched_guard_rejection forge_scan_that_cannot_run \
    'the forge reference scan failed with status' \
    "s|^forge_reference_re=.*|forge_reference_re='[unterminated'|" \
    "$clean_root"

expect_patched_guard_rejection own_owner_filter_that_cannot_run \
    'the own-owner forge filter failed to run' \
    "s|^own_forge_reference_re=.*|own_forge_reference_re='[unterminated'|" \
    "$clean_root"

# --- A peer named by its standing rather than by its name -------------------
# The qualifier carries the claim: calling an implementation established or
# mainstream places it among peers. The narrow qualifier set is what keeps
# ordinary prose usable, so both directions are pinned.
readonly peer_standing_message='a decision is attributed to a peer implementation'
readonly exemption_floor_message='the archived line this rule exempts is no longer in the record'

expect_reason_outcome decision_taken_from_a_peer reject "$peer_standing_message" \
    "$(build_repository_named peer_standing_taken NOTES.md \
        "The escaping is pinned by an establi""shed implementa""tion.")"

expect_reason_outcome behaviour_matched_to_a_peer reject "$peer_standing_message" \
    "$(build_repository_named peer_standing_matched NOTES.md \
        "The palette filters rows as mainstr""eam applica""tions do.")"

# The wrapping case, which is the reason this rule exists. The derivation verb
# is on the line above and the qualifier and its noun sit together below, so a
# line-oriented rule anchored on the verb sees neither half.
expect_reason_outcome peer_standing_across_a_wrapped_line reject "$peer_standing_message" \
    "$(build_repository_named peer_standing_wrapped NOTES.md \
        "The escaping is therefore pinned by expectations taken from an
establi""shed implementa""tion rather than from this one.")"

# The discriminating direction. Interoperability prose says what other software
# can read, and it is a specification statement this repository has to be able
# to write. "Other" and "another" are deliberately outside the qualifier set.
expect_reason_outcome interoperability_prose_about_other_software accept "$peer_standing_message" \
    "$(build_repository_named peer_standing_interop NOTES.md \
        "A divergent escaping would produce a private cache that no other
application can find.")"

expect_reason_outcome policy_prose_naming_what_it_forbids accept "$peer_standing_message" \
    "$(build_repository_named peer_standing_policy NOTES.md \
        "Do not identify another project of the same kind, and do not present a
decision as measured against another application.")"

# The exemption floor. One archived line is excused by its exact text because
# published entries cannot be reworded; the floor fails when the record is
# present and that line is not, so the exemption cannot outlive its subject.
exemption_present_root="$workspace/exemption-present"
mkdir -p "$exemption_present_root/docs/devlog"
git init -q "$exemption_present_root"
git -C "$exemption_present_root" config user.name owner
git -C "$exemption_present_root" config user.email "$owner_identity"
git -C "$exemption_present_root" config commit.gpgsign false
printf 'The escaping is therefore pinned by expectations taken from an\n%s\n' \
    "establi""shed implementa""tion rather than from this one." \
    >"$exemption_present_root/docs/devlog/2026-07.md"
git -C "$exemption_present_root" add docs/devlog/2026-07.md
git -C "$exemption_present_root" -c core.hooksPath=/dev/null \
    commit -q -m 'Add an archived record'

expect_reason_outcome archived_line_is_exempted accept "$peer_standing_message" \
    "$exemption_present_root"

# The exemption excuses one exact line, not the file it sits in and not any
# line resembling it. This record holds the exempted line, a longer line that
# contains it, and an unrelated attribution - and the last two must both be
# reported while the first stays excused.
exemption_scope_root="$workspace/exemption-scope"
mkdir -p "$exemption_scope_root/docs/devlog"
git init -q "$exemption_scope_root"
git -C "$exemption_scope_root" config user.name owner
git -C "$exemption_scope_root" config user.email "$owner_identity"
git -C "$exemption_scope_root" config commit.gpgsign false
{
    printf 'The escaping is therefore pinned by expectations taken from an\n'
    printf '%s\n' "establi""shed implementa""tion rather than from this one."
    printf 'Note that the %s\n' \
        "establi""shed implementa""tion rather than from this one."
    printf 'The palette filters rows as %s do.\n' "mainstr""eam applica""tions"
} >"$exemption_scope_root/docs/devlog/2026-07.md"
git -C "$exemption_scope_root" add docs/devlog/2026-07.md
git -C "$exemption_scope_root" -c core.hooksPath=/dev/null \
    commit -q -m 'Add an archived record'

expect_reason_outcome exemption_covers_one_exact_line reject "$peer_standing_message" \
    "$exemption_scope_root"

# The longer line on its own. In the record above it is reported alongside an
# unrelated attribution, so that record cannot tell an exemption widened to a
# substring from one held to the exact line - something is reported either way.
# Here the exempted line and a line containing it are all there is, so the only
# way the guard can stay silent is by excusing text it was never given.
exemption_substring_root="$workspace/exemption-substring"
mkdir -p "$exemption_substring_root/docs/devlog"
git init -q "$exemption_substring_root"
git -C "$exemption_substring_root" config user.name owner
git -C "$exemption_substring_root" config user.email "$owner_identity"
git -C "$exemption_substring_root" config commit.gpgsign false
{
    printf '%s\n' "establi""shed implementa""tion rather than from this one."
    printf 'Note that the %s\n' \
        "establi""shed implementa""tion rather than from this one."
} >"$exemption_substring_root/docs/devlog/2026-07.md"
git -C "$exemption_substring_root" add docs/devlog/2026-07.md
git -C "$exemption_substring_root" -c core.hooksPath=/dev/null \
    commit -q -m 'Add an archived record'

expect_reason_outcome exemption_does_not_cover_a_longer_line reject "$peer_standing_message" \
    "$exemption_substring_root"

exemption_absent_root="$workspace/exemption-absent"
mkdir -p "$exemption_absent_root/docs/devlog"
git init -q "$exemption_absent_root"
git -C "$exemption_absent_root" config user.name owner
git -C "$exemption_absent_root" config user.email "$owner_identity"
git -C "$exemption_absent_root" config commit.gpgsign false
printf 'An archived entry with nothing in it for the exemption to excuse.\n' \
    >"$exemption_absent_root/docs/devlog/2026-07.md"
git -C "$exemption_absent_root" add docs/devlog/2026-07.md
git -C "$exemption_absent_root" -c core.hooksPath=/dev/null \
    commit -q -m 'Add an archived record'

expect_reason_outcome exemption_without_its_subject reject "$exemption_floor_message" \
    "$exemption_absent_root"

# The same absence without any archived record at all must stay acceptable, or
# every fixture in this file would fail for want of a file it never had.
expect_reason_outcome no_archived_record_at_all accept "$exemption_floor_message" \
    "$(build_repository_named exemption_no_record NOTES.md \
        "Ordinary prose with no archived record beside it.")"

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
readonly expected_scenarios=117
if ((checked != expected_scenarios)); then
    printf 'public_repository_guard_self_test: %d scenario(s) reported a result, expected %d; the suite did not run in full\n' \
        "$checked" "$expected_scenarios" >&2
    exit 1
fi

printf 'public_repository_guard_self_test: %d scenarios are enforced\n' \
    "$checked"

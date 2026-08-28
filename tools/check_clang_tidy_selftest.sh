#!/usr/bin/env bash

# Proves the static-analysis gate still fails on a fatal diagnostic, still
# holds the advisory set to its baseline in both directions, refuses a corpus
# it found no translation unit in, and rejects a swap that leaves the counts
# alone.
#
# This gate had no self-test. Its whole result is a comparison between two
# generated sets, and two empty sets compare equal: analysing nothing produces
# no diagnostics, which matches an emptied baseline exactly and prints as a
# clean gate. Nothing in the battery would have shown that.
#
# Each scenario builds a throwaway repository with the repository's own
# `.clang-tidy` policy, a hand-written compilation database, and a baseline of
# its own, then requires a specific exit status and a specific message.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly guard="$tools_directory/check_clang_tidy.sh"
readonly policy="$tools_directory/../.clang-tidy"

if [[ ! -f "$guard" || ! -f "$policy" ]]; then
    printf 'static_analysis_self_test: the gate or its policy file is missing\n' >&2
    exit 1
fi

# Resolved the way the gate resolves it, including the checkout-local copy, so
# the self-test analyses with the same binary the gate would use. No skip when
# it is absent: the gate fails in that case, and a self-test that skipped would
# report a healthier tree than the battery it belongs to.
resolve_clang_tidy() {
    local candidate="${ODYSEA_CLANG_TIDY:-}"
    if [[ -z "$candidate" ]]; then
        candidate="$(command -v clang-tidy || true)"
    fi
    if [[ -z "$candidate" ]]; then
        local shared_root common_git_directory
        if common_git_directory="$(git -C "$tools_directory" \
            rev-parse --path-format=absolute --git-common-dir 2>/dev/null)"; then
            shared_root="$(dirname "$common_git_directory")"
        else
            shared_root="$(cd "$tools_directory/.." && pwd -P)"
        fi
        local checkout_local="$shared_root/.archon/tools/clang-tidy-22/bin/clang-tidy"
        if [[ -x "$checkout_local" ]]; then
            candidate="$checkout_local"
        fi
    fi
    printf '%s' "$candidate"
}

tidy_binary="$(resolve_clang_tidy)"
readonly tidy_binary
if [[ -z "$tidy_binary" || ! -x "$tidy_binary" ]]; then
    printf 'static_analysis_self_test: clang-tidy is required, as it is by the gate\n' >&2
    exit 1
fi
export ODYSEA_CLANG_TIDY="$tidy_binary"

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

status=0
checked=0

# When non-empty, scenarios use this policy body instead of the repository's.
# Only the fatal-set floor needs it; every other scenario must analyse under the
# policy the project actually ships.
policy_override=""

# A translation unit clang-tidy has nothing to say about.
readonly clean_source='int addOne(int value) { return value + 1; }
'

# Manual memory management: fatal under the recorded policy, which promotes
# cppcoreguidelines-no-malloc and the analyzer's leak checks to errors.
readonly fatal_source='#include <cstdlib>

void allocate() {
    void* block = std::malloc(4);
    (void)block;
}
'

# A single advisory diagnostic, which the policy does not promote.
readonly advisory_source='int classify(int value) {
    if (value > 0)
        return 1;
    return 0;
}
'
readonly advisory_check='readability-braces-around-statements'
readonly advisory_message='statement should be inside braces'

# The same diagnostic, pushed down the file by two lines of comment. The gate's
# entry key is deliberately free of line and column so that editing above a
# recorded diagnostic does not restate the baseline, and the digest is taken
# over message text for the same reason. This source is what proves that
# property still holds: it must be accepted against a baseline recorded from
# the source above.
readonly advisory_moved_source='// Two lines of comment, so the diagnostic below moves.
//
int classify(int value) {
    if (value > 0)
        return 1;
    return 0;
}
'

# The balanced-swap pair. Both sources produce exactly two occurrences of one
# check in one file, so the recorded count is identical; between them one
# occurrence is fixed and another introduced, so the diagnostic text is not.
# This is the shape a contributor produces without trying, by tidying one
# expression while getting the precedence wrong in another, and it is the shape
# that a count-only ratchet reported as a clean tree.
readonly swap_before_source='int rounded(int total, int unit) { return total / unit + 1; }
int scaled(int rows, int stride) { return rows + stride * 4; }
int guard(int rows, int stride) { return rows + (stride * 8); }
'
readonly swap_after_source='int rounded(int total, int unit) { return (total / unit) + 1; }
int scaled(int rows, int stride) { return rows + stride * 4; }
int guard(int rows, int stride) { return rows + stride * 8; }
'
readonly swap_check='readability-math-missing-parentheses'

# One header location that two translation units word differently. The unit
# that sees only the destructor's declaration reports the class as defining "a
# destructor"; the unit that sees its definition reports "a non-default
# destructor". Same check, same line, same column, two messages.
#
# This is why the message is not part of the identity that collapses duplicate
# reports of a header. Counting the wordings separately would make this
# header's recorded count depend on how many unrelated sources include it,
# which is the drift the collapse exists to prevent.
readonly shared_header_source='#pragma once
class Shared {
public:
    Shared();
    ~Shared();

private:
    int value_ = 0;
};
'
readonly shared_header_user_source='#include "support.hpp"

void useShared(const Shared& shared) { (void)shared; }
'
readonly shared_header_owner_source='#include "support.hpp"

Shared::Shared() = default;

Shared::~Shared() { value_ = 1; }
'
readonly shared_header_check='cppcoreguidelines-special-member-functions'

# A check the policy promotes from advisory to error. It is fatal for the same
# reason the malloc source is, but through a check that was promoted rather than
# one that always was, which is what the gate's fatal-set derivation has to
# notice. Kept out of the advisory comparison entirely: it must fail the gate
# before any baseline is consulted.
readonly promoted_source='#include <mutex>

std::mutex gate;

void hold() {
    const std::lock_guard<std::mutex> guard(gate);
}
'
readonly promoted_check='modernize-use-scoped-lock'

report() {
    local outcome="$1" description="$2"
    if [[ "$outcome" == "pass" ]]; then
        printf '  ok      %s\n' "$description"
    else
        printf '  FAILED  %s\n' "$description" >&2
        status=1
    fi
}

# Lays out a throwaway repository at the given directory holding the named
# sources and the policy in force, and stages it so the gate's corpus
# enumeration sees the files. Sources are named "stem=clean", "stem=fatal",
# "stem=advisory", "stem=advisorymoved", "stem=promoted", "stem=swapbefore",
# "stem=swapafter", "stem=sharedheader", "stem=sharedheaderuser", or
# "stem=sharedheaderowner". A stem may name a subdirectory.
#
# The compilation database names its files by absolute path. A header is only
# analysed when its reported path matches the gate's header filter, which is
# anchored at the repository root, and a relative command line makes clang
# report the header relatively - so a header scenario would silently observe
# nothing.
build_sandbox() {
    local sandbox="$1"
    shift

    mkdir -p "$sandbox/tools"
    git -C "$sandbox" init -q
    git -C "$sandbox" config core.hooksPath /dev/null
    if [[ -n "$policy_override" ]]; then
        printf '%s' "$policy_override" >"$sandbox/.clang-tidy"
    else
        cp "$policy" "$sandbox/.clang-tidy"
    fi

    local database="["
    local separator=""
    local specification stem kind
    for specification in "$@"; do
        stem="${specification%%=*}"
        kind="${specification##*=}"
        mkdir -p "$(dirname "$sandbox/$stem")"

        # A header is written but never listed in the database: it reaches the
        # analysis only through the units that include it, which is the whole
        # point of the scenario that uses it.
        if [[ "$kind" == "sharedheader" ]]; then
            printf '%s' "$shared_header_source" >"$sandbox/$stem.hpp"
            continue
        fi

        case "$kind" in
            fatal) printf '%s' "$fatal_source" >"$sandbox/$stem.cpp" ;;
            promoted) printf '%s' "$promoted_source" >"$sandbox/$stem.cpp" ;;
            advisory) printf '%s' "$advisory_source" >"$sandbox/$stem.cpp" ;;
            advisorymoved) printf '%s' "$advisory_moved_source" >"$sandbox/$stem.cpp" ;;
            swapbefore) printf '%s' "$swap_before_source" >"$sandbox/$stem.cpp" ;;
            swapafter) printf '%s' "$swap_after_source" >"$sandbox/$stem.cpp" ;;
            sharedheaderuser) printf '%s' "$shared_header_user_source" >"$sandbox/$stem.cpp" ;;
            sharedheaderowner) printf '%s' "$shared_header_owner_source" >"$sandbox/$stem.cpp" ;;
            *) printf '%s' "$clean_source" >"$sandbox/$stem.cpp" ;;
        esac
        database+="${separator}{\"directory\": \"$sandbox\","
        database+=" \"command\": \"c++ -std=c++20 -c $sandbox/$stem.cpp\","
        database+=" \"file\": \"$sandbox/$stem.cpp\"}"
        separator=","
    done
    database+="]"
    printf '%s\n' "$database" >"$sandbox/compile_commands.json"

    git -C "$sandbox" add -A
}

# Records a baseline over the named sources by asking the gate for one, and
# prints it.
#
# Generated rather than written out by hand, because a hand-written digest
# would only ever restate whatever formula the gate happens to use. The
# scenarios that matter here compare one generated baseline against a different
# tree, so a digest that were constant, empty, or otherwise degenerate would
# make those scenarios fail rather than pass - which is the property a fixed
# expected value cannot give.
#
# The sandbox directory is allocated rather than numbered. This helper is
# called from a command substitution, so a counter kept here advances only
# inside that subshell and every call would name the same directory: the second
# generation would inherit the first one's sources and record a baseline
# describing both trees.
baseline_for() {
    local sandbox
    sandbox="$(mktemp -d "$workspace/baseline-XXXXXX")"
    build_sandbox "$sandbox" "$@"
    (cd "$sandbox" && bash "$guard" "$sandbox" --update-baseline) >/dev/null
    cat "$sandbox/tools/clang_tidy_baseline.txt"
}

# Builds a repository holding the named sources with the given baseline body,
# then requires the stated outcome and message.
#
# The baseline body is written verbatim; the literal string "absent" leaves the
# baseline file out entirely.
expect_verdict() {
    local description="$1"
    local expectation="$2"
    local fragment="$3"
    local baseline_body="$4"
    shift 4

    checked=$((checked + 1))

    local sandbox="$workspace/sandbox-$checked"
    build_sandbox "$sandbox" "$@"

    if [[ "$baseline_body" != "absent" ]]; then
        printf '%s' "$baseline_body" >"$sandbox/tools/clang_tidy_baseline.txt"
        git -C "$sandbox" add -A
    fi

    local output=""
    local exit_status=0
    output="$( (cd "$sandbox" && bash "$guard" "$sandbox") 2>&1 )" || exit_status=$?

    case "$expectation" in
        accept)
            if ((exit_status != 0)); then
                report fail "$description (exit ${exit_status}: ${output})"
                return
            fi
            ;;
        reject | reject_without)
            if ((exit_status == 0)); then
                report fail "$description (accepted: ${output})"
                return
            fi
            ;;
        *)
            report fail "$description (unknown expectation ${expectation})"
            return
            ;;
    esac

    # `reject_without` requires the rejection NOT to carry the fragment. A
    # wrong explanation cannot be caught by requiring the right one alone: the
    # gate prints a line per drifted entry, so a comparison that produced both
    # readings would satisfy a positive assertion while still telling a reader
    # the opposite of what happened.
    if [[ "$expectation" == "reject_without" ]]; then
        if [[ "$output" == *"$fragment"* ]]; then
            report fail "$description (message stated ${fragment}: ${output})"
            return
        fi
        report pass "$description"
        return
    fi

    if [[ "$output" != *"$fragment"* ]]; then
        report fail "$description (message did not state ${fragment}: ${output})"
        return
    fi

    report pass "$description"
}

readonly empty_baseline='# no advisory diagnostics recorded
'

# The digest formula restated independently of the gate, over the message text
# this source is known to produce. Every scenario below that hands the gate a
# hand-written baseline depends on this agreeing with what the gate computes,
# so the recorded format cannot change meaning without a scenario saying so.
digest_of() {
    printf '%s' "$1" | sha256sum | cut -c1-12
}

readonly advisory_digest="$(digest_of "$advisory_message"$'\n')"
readonly advisory_baseline="$(printf '1\tadvisory.cpp\t%s\t%s\n' \
    "$advisory_check" "$advisory_digest")"

# The pre-digest baseline format: three fields, no digest. Recorded baselines
# in this form predate the swap check and cannot answer it.
readonly legacy_baseline="$(printf '1\tadvisory.cpp\t%s\n' "$advisory_check")"

# A baseline whose digest field is present but not a digest. Told apart from
# the one above on purpose: a reader meeting "the entry has no digest" and a
# reader meeting a garbled one are in different situations, and a single
# scenario covering both would not notice if one of them stopped being caught.
readonly garbled_baseline="$(printf '1\tadvisory.cpp\t%s\tnot-a-digest\n' "$advisory_check")"

expect_verdict "a clean translation unit passes" \
    accept "1 translation units passed" \
    "$empty_baseline" "clean=clean"

expect_verdict "a fatal diagnostic is rejected" \
    reject "reported fatal diagnostics" \
    "$empty_baseline" "unsafe=fatal"

# The baseline holds one of the two units on purpose. A recorded set that is
# empty takes a different path through the gate's comparison, and that path is
# the subject of the three scenarios below rather than of this one.
expect_verdict "an unrecorded advisory diagnostic is rejected as new" \
    reject "extra.cpp: $advisory_check is new" \
    "$advisory_baseline" "advisory=advisory" "extra=advisory"

# The same diagnostic against an EMPTY recorded set: the case that read
# backwards. With nothing recorded, the comparison loaded the current set as
# the recorded one and announced a brand-new diagnostic as one that had stopped
# occurring. The gate failed either way, so the wording is the only thing that
# distinguishes the fixed comparison from the broken one - and the wording is
# what a reader acts on.
expect_verdict "a new diagnostic against an empty baseline is called new" \
    reject "advisory.cpp: $advisory_check is new" \
    "$empty_baseline" "advisory=advisory"

# Asserted separately, because the gate prints one line per drifted entry: a
# comparison emitting both readings would satisfy the scenario above while
# still telling a reader the opposite of what happened.
expect_verdict "a new diagnostic against an empty baseline is not called cleared" \
    reject_without "no longer occurs" \
    "$empty_baseline" "advisory=advisory"

# The end state the ratchet aims at: nothing recorded because nothing occurs.
# It passes, and the size of the recorded set is stated so that it can be told
# from a baseline holding a real set. Both otherwise pass identically, and an
# emptied baseline is exactly what the wrong reading above was born of.
expect_verdict "an exhausted baseline over a clean tree passes and states its size" \
    accept "across 0 recorded entries" \
    "$empty_baseline" "clean=clean"

expect_verdict "a recorded advisory diagnostic is accepted" \
    accept "1 advisory diagnostics across 1 recorded entries held at the baseline" \
    "$advisory_baseline" "advisory=advisory"

# The ratchet turns one way only: an entry that stops occurring has to be
# recorded as gone, or the baseline stops describing the tree.
expect_verdict "a recorded diagnostic that no longer occurs is rejected" \
    reject "no longer occurs" \
    "$advisory_baseline" "clean=clean"

# The fatal set is read from the policy, not repeated in the gate. A check the
# policy promotes must therefore be fatal here without the gate having been
# taught its name, which is the drift that made a promotion print a failing unit
# with nothing under it.
expect_verdict "a check promoted to an error in the policy is fatal" \
    reject "reported fatal diagnostics" \
    "$empty_baseline" "locked=promoted"

# And the diagnostic itself is printed, not merely the unit that produced it.
# This is the assertion that fails when the fatal set is hard-coded and a newly
# promoted check is missing from it: the gate still rejects, so only the
# presence of the reason tells the two apart.
expect_verdict "a promoted check's diagnostic is named in the failure" \
    reject "$promoted_check" \
    "$empty_baseline" "locked=promoted"

expect_verdict "a missing baseline is rejected" \
    reject "is missing" \
    absent "clean=clean"

# The balanced swap: the case a count-only ratchet passed. Both trees hold two
# occurrences of one check in one file, so the count, the entry set and the
# printed totals are identical; between them one occurrence was fixed and
# another introduced. Recording the baseline from the first tree and analysing
# the second is the whole exploit, reduced to two files.
swap_baseline="$(baseline_for "swap=swapbefore")"
readonly swap_baseline

expect_verdict "a balanced same-file same-check swap is rejected" \
    reject "swap.cpp: $swap_check" \
    "$swap_baseline" "swap=swapafter"

# And rejected for the right reason. The entry is matched and its text is what
# disagreed; a comparison that had re-keyed the entry would report it as new
# and one gone, which is a different claim about a different tree.
expect_verdict "a balanced swap is reported as changed text, not as a new entry" \
    reject_without "is new" \
    "$swap_baseline" "swap=swapafter"

expect_verdict "a balanced swap names the count it held and both digests" \
    reject "still occurs 2 time(s), but the analyser now says something else" \
    "$swap_baseline" "swap=swapafter"

# The property the digest was chosen to preserve, and the reason line and
# column are not in it. A diagnostic that moved down its file is the same
# diagnostic, and restating the baseline for it would train a reader to
# regenerate on sight - which is how a real swap gets waved through.
expect_verdict "a recorded diagnostic that moved down its file is accepted" \
    accept "1 advisory diagnostics across 1 recorded entries held at the baseline" \
    "$advisory_baseline" "advisory=advisorymoved"

# A baseline recorded before entries carried a digest cannot answer the swap,
# and there is no honest way to compare against a digest that is not there.
expect_verdict "a baseline entry with no digest is refused" \
    reject "carries no diagnostic-text digest" \
    "$legacy_baseline" "advisory=advisory"

expect_verdict "a baseline entry with a malformed digest is refused" \
    reject "carries no diagnostic-text digest" \
    "$garbled_baseline" "advisory=advisory"

# One header location described two ways by two translation units. It is one
# occurrence, and both wordings belong to it. Putting the message into the key
# that collapses duplicate reports would count it twice and make the recorded
# number a property of how many sources include the header - which is what the
# collapse exists to prevent, and what showed up in this repository as the
# recorded total moving by one when the message was first carried too far.
readonly two_wording_specification=(
    "app/support=sharedheader"
    "app/user=sharedheaderuser"
    "app/owner=sharedheaderowner"
)
two_wording_baseline="$(baseline_for "${two_wording_specification[@]}")"
readonly two_wording_baseline

expect_verdict "one location worded two ways by two translation units counts once" \
    accept "1 advisory diagnostics across 1 recorded entries held at the baseline" \
    "$two_wording_baseline" "${two_wording_specification[@]}"

# And the recorded entry says so: one occurrence of that check in that header.
expect_verdict "the two wordings are recorded as a single occurrence of one check" \
    accept "1 advisory diagnostics" \
    "$(printf '1\tapp/support.hpp\t%s\t%s\n' "$shared_header_check" \
        "$(printf '%s' "$two_wording_baseline" | grep "$shared_header_check" | cut -f4)")" \
    "${two_wording_specification[@]}"

# The floor. With nothing analysed there are no diagnostics to compare, and an
# empty comparison is the same shape as a passing one.
expect_verdict "a corpus holding no translation unit is refused rather than passed" \
    reject "no translation unit" \
    "$empty_baseline"

# The floor under the derived fatal set. With no check promoted, the pattern the
# gate builds is empty, and an empty alternation matches every diagnostic: the
# gate would reprint an advisory line as a fatal one and explain a failure with
# the wrong list. Refusing to run is the only honest answer, because the gate
# cannot know what it is meant to enforce.
policy_override='Checks: >
  readability-*
'
expect_verdict "a policy that promotes no check to an error is refused" \
    reject "no check is promoted to an error" \
    "$empty_baseline" "advisory=advisory"
policy_override=""

# The scenario count is checked against the number this file is written to
# contain. A scenario that stopped running would otherwise leave a smaller suite
# printing the same success sentence.
readonly expected_scenarios=21
if ((checked != expected_scenarios)); then
    printf 'static_analysis_self_test: ran %d scenario(s), expected %d\n' \
        "$checked" "$expected_scenarios" >&2
    status=1
fi

if ((status != 0)); then
    printf 'static_analysis_self_test: failed\n' >&2
    exit 1
fi

printf 'static_analysis_self_test: %d scenarios passed\n' "$checked"

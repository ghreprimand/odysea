#!/usr/bin/env bash

# Proves the static-analysis gate still fails on a fatal diagnostic, still
# holds the advisory set to its baseline in both directions, and refuses a
# corpus it found no translation unit in.
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

report() {
    local outcome="$1" description="$2"
    if [[ "$outcome" == "pass" ]]; then
        printf '  ok      %s\n' "$description"
    else
        printf '  FAILED  %s\n' "$description" >&2
        status=1
    fi
}

# Builds a repository holding the named sources with the given baseline body,
# then requires the stated outcome and message.
#
# The baseline body is written verbatim; the literal string "absent" leaves the
# baseline file out entirely. Sources are named "stem=clean", "stem=fatal", or
# "stem=advisory".
expect_verdict() {
    local description="$1"
    local expectation="$2"
    local fragment="$3"
    local baseline_body="$4"
    shift 4

    checked=$((checked + 1))

    local sandbox="$workspace/sandbox-$checked"
    mkdir -p "$sandbox/tools"
    git -C "$sandbox" init -q
    git -C "$sandbox" config core.hooksPath /dev/null
    cp "$policy" "$sandbox/.clang-tidy"

    if [[ "$baseline_body" != "absent" ]]; then
        printf '%s' "$baseline_body" >"$sandbox/tools/clang_tidy_baseline.txt"
    fi

    local database="["
    local separator=""
    local specification stem kind
    for specification in "$@"; do
        stem="${specification%%=*}"
        kind="${specification##*=}"
        case "$kind" in
            fatal) printf '%s' "$fatal_source" >"$sandbox/$stem.cpp" ;;
            advisory) printf '%s' "$advisory_source" >"$sandbox/$stem.cpp" ;;
            *) printf '%s' "$clean_source" >"$sandbox/$stem.cpp" ;;
        esac
        database+="${separator}{\"directory\": \"$sandbox\","
        database+=" \"command\": \"c++ -std=c++20 -c $stem.cpp\","
        database+=" \"file\": \"$sandbox/$stem.cpp\"}"
        separator=","
    done
    database+="]"
    printf '%s\n' "$database" >"$sandbox/compile_commands.json"

    git -C "$sandbox" add -A

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
        reject)
            if ((exit_status == 0)); then
                report fail "$description (accepted: ${output})"
                return
            fi
            ;;
    esac

    if [[ "$output" != *"$fragment"* ]]; then
        report fail "$description (message did not state ${fragment}: ${output})"
        return
    fi

    report pass "$description"
}

readonly empty_baseline='# no advisory diagnostics recorded
'
readonly advisory_baseline="$(printf '1\tadvisory.cpp\t%s\n' "$advisory_check")"

expect_verdict "a clean translation unit passes" \
    accept "1 tracked translation units passed" \
    "$empty_baseline" "clean=clean"

expect_verdict "a fatal diagnostic is rejected" \
    reject "reported fatal diagnostics" \
    "$empty_baseline" "unsafe=fatal"

# The baseline holds one of the two units on purpose. A recorded set that is
# empty takes a different path through the gate's comparison, and that path is
# not the subject of this scenario.
expect_verdict "an unrecorded advisory diagnostic is rejected as new" \
    reject "extra.cpp: $advisory_check is new" \
    "$advisory_baseline" "advisory=advisory" "extra=advisory"

expect_verdict "a recorded advisory diagnostic is accepted" \
    accept "advisory diagnostics held at the baseline" \
    "$advisory_baseline" "advisory=advisory"

# The ratchet turns one way only: an entry that stops occurring has to be
# recorded as gone, or the baseline stops describing the tree.
expect_verdict "a recorded diagnostic that no longer occurs is rejected" \
    reject "no longer occurs" \
    "$advisory_baseline" "clean=clean"

expect_verdict "a missing baseline is rejected" \
    reject "is missing" \
    absent "clean=clean"

# The floor. With nothing analysed there are no diagnostics to compare, and an
# empty comparison is the same shape as a passing one.
expect_verdict "a corpus holding no translation unit is refused rather than passed" \
    reject "no tracked translation unit" \
    "$empty_baseline"

if ((status != 0)); then
    printf 'static_analysis_self_test: failed\n' >&2
    exit 1
fi

printf 'static_analysis_self_test: %d scenarios passed\n' "$checked"

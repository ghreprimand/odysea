#!/usr/bin/env bash
set -euo pipefail

# Proves the coverage reconciler fails on every way an entry can drop out of a
# battery, and passes only when the results account for the registered roster.
#
# Each scenario asserts the SPECIFIC result. The fixtures are hand-written
# JUnit documents shaped like ctest's own output — a run entry, an entry that
# declared a refusal, an entry that skipped with a cause but no declaration, a
# silent skip with nothing on either stream — so the reconciler's decision is
# exercised without running a battery.
#
# The scenarios around the declaration are the load-bearing ones: tolerating a
# shortfall because it printed SOMETHING would reopen the failure class the
# reconciler was built to close, so an explained capability skip, a mention of
# the token in prose, and a declaration with no reason after it are each
# required to fail, separately, by name.

# The reconciler parses JUnit with python3, so without python3 there is nothing
# for this self-test to exercise. Decline by name rather than reporting a false
# pass or a failure that only means the interpreter is absent; CTest maps this
# exit to a skip through the entry's SKIP_RETURN_CODE.
if ! command -v python3 >/dev/null 2>&1; then
    printf 'battery_coverage_self_test: SKIP -- python3 is required to exercise the reconciler on the JUnit fixtures\n' >&2
    exit 77
fi

readonly reconciler="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/check_battery_coverage.sh"

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

failures=0

report() {
    printf 'battery_coverage_self_test: %s\n' "$1" >&2
    failures=$((failures + 1))
}

expect() {
    local scenario="$1"
    local junit="$2"
    local registered="$3"
    local expected_code="$4"
    local expected_text="$5"
    local output
    local code
    output="$(bash "$reconciler" "$junit" "$registered" 2>&1)" && code=0 || code=$?

    if [[ "$code" != "$expected_code" ]]; then
        report "$scenario: expected exit $expected_code, got $code; output: $output"
        return
    fi
    if [[ -n "$expected_text" && "$output" != *"$expected_text"* ]]; then
        report "$scenario: expected the result to mention '$expected_text'; output: $output"
    fi
}

# A JUnit document with one testcase per line-description. Arguments are triples
# of name/status/output; an empty output means nothing was captured.
write_junit() {
    local path="$1"
    shift
    {
        printf '<?xml version="1.0" encoding="UTF-8"?>\n'
        printf '<testsuite name="battery">\n'
        while [[ "$#" -ge 3 ]]; do
            local name="$1" status="$2" out="$3"
            shift 3
            printf '  <testcase name="%s" classname="%s" status="%s">\n' "$name" "$name" "$status"
            if [[ "$status" == "notrun" ]]; then
                printf '    <skipped message="SKIP_RETURN_CODE=77"/>\n'
            fi
            printf '    <system-out>%s</system-out>\n' "$out"
            printf '  </testcase>\n'
        done
        printf '</testsuite>\n'
    } >"$path"
}

write_registered() {
    local path="$1"
    shift
    printf '%s\n' "$@" >"$path"
}

# Scenario 1: every registered entry ran. The clean pass.
junit="$workspace/all_ran.xml"
registered="$workspace/all_ran.txt"
write_junit "$junit" a run "a ok" b run "b ok" c run "c ok"
write_registered "$registered" a b c
expect "all entries ran" "$junit" "$registered" 0 "3/3 registered entries accounted for"

# Scenario 2: a DECLARED REFUSAL is the one tolerated shortfall. It passes and
# is listed with its reason, so the cost of the refusal stays visible.
junit="$workspace/declined.xml"
registered="$workspace/declined.txt"
write_junit "$junit" a run "a ok" \
    b notrun "compositor-gate: DECL -- declined: no compositor was declared for this run" \
    c run "c ok"
write_registered "$registered" a b c
expect "declared refusal passes and is listed" "$junit" "$registered" 0 "declared refusal"

# Scenario 2a: THE TIGHT HALF. A skip that explained itself but did not declare
# a refusal is a capability hole, and a capability hole fails. This is the
# scenario that keeps the classification from degenerating into "printed
# something, therefore allowed" — which is the whole failure class this gate
# exists to close.
junit="$workspace/undeclared.xml"
registered="$workspace/undeclared.txt"
write_junit "$junit" a run "a ok" \
    b notrun "rhi-gate: SKIP -- could not run: no display server reachable" \
    c run "c ok"
write_registered "$registered" a b c
expect "explained capability skip still fails" "$junit" "$registered" 1 \
    "without a declared refusal"

# Scenario 2b: the form is anchored, not merely mentioned. A skip whose output
# talks ABOUT declining — the token present but not as the line's own
# declaration — is not a declaration.
junit="$workspace/mentions_decl.xml"
registered="$workspace/mentions_decl.txt"
write_junit "$junit" a run "a ok" \
    b notrun "note: this gate would print compositor-gate: DECL -- declined: if it refused" \
    c run "c ok"
write_registered "$registered" a b c
expect "a mention of the token is not a declaration" "$junit" "$registered" 1 \
    "without a declared refusal"

# Scenario 2c: a declaration with no reason after it declares nothing, and is
# refused. Otherwise the cheapest way past the gate would be the token alone.
junit="$workspace/empty_reason.xml"
registered="$workspace/empty_reason.txt"
write_junit "$junit" a run "a ok" b notrun "compositor-gate: DECL -- declined: " c run "c ok"
write_registered "$registered" a b c
expect "a declaration with no reason fails" "$junit" "$registered" 1 \
    "without a declared refusal"

# Scenario 2b: a test disabled in the build configuration — status "disabled",
# as ctest records static_analysis under the sanitizer preset — is an intended
# opt-out. It passes and is listed, and is not counted as having run.
junit="$workspace/disabled.xml"
registered="$workspace/disabled.txt"
write_junit "$junit" a run "a ok" b disabled "Disabled" c run "c ok"
write_registered "$registered" a b c
expect "disabled entry passes and is listed" "$junit" "$registered" 0 "disabled"

# Scenario 3: a silent skip — notrun with nothing captured — is the defect and
# must fail.
junit="$workspace/silent_skip.xml"
registered="$workspace/silent_skip.txt"
write_junit "$junit" a run "a ok" b notrun "" c run "c ok"
write_registered "$registered" a b c
expect "silent skip fails" "$junit" "$registered" 1 "silent skip"

# Scenario 4: a registered entry absent from the results fails as missing.
junit="$workspace/missing.xml"
registered="$workspace/missing.txt"
write_junit "$junit" a run "a ok" b run "b ok"
write_registered "$registered" a b c
expect "missing entry fails" "$junit" "$registered" 1 "missing"

# Scenario 5: a result not on the roster fails as unexpected — a stale list is
# not a roster the reconciler can trust.
junit="$workspace/unexpected.xml"
registered="$workspace/unexpected.txt"
write_junit "$junit" a run "a ok" b run "b ok" z run "z ok"
write_registered "$registered" a b
expect "unexpected entry fails" "$junit" "$registered" 1 "unexpected"

# Scenario 6: an empty registered list is a broken enumeration, not an empty
# suite, and fails rather than vacuously passing.
junit="$workspace/empty_reg.xml"
registered="$workspace/empty_reg.txt"
write_junit "$junit" a run "a ok"
: >"$registered"
expect "empty registered list fails" "$junit" "$registered" 1 "registered list is empty"

# Scenario 7: results where nothing ran — every entry declined, each of them
# properly — is the floor, not coverage, and fails. Declaring a refusal buys
# tolerance for one entry, never for a whole battery.
junit="$workspace/nothing_ran.xml"
registered="$workspace/nothing_ran.txt"
write_junit "$junit" a notrun "a-gate: DECL -- declined: nothing declared" \
    b notrun "b-gate: DECL -- declined: nothing declared"
write_registered "$registered" a b
expect "nothing ran fails even when every entry declined" "$junit" "$registered" 1 "nothing ran"

# Scenario 8: a missing results file fails rather than being read as a pass.
registered="$workspace/no_junit.txt"
write_registered "$registered" a b
expect "missing junit file fails" "$workspace/does_not_exist.xml" "$registered" 1 "results not found"

# Scenario 9: an unparseable results file fails by name.
junit="$workspace/malformed.xml"
registered="$workspace/malformed.txt"
printf '<testsuite><testcase name="a"\n' >"$junit"
write_registered "$registered" a
expect "malformed junit fails" "$junit" "$registered" 1 "not parseable"

if ((failures != 0)); then
    printf 'battery_coverage_self_test: %d scenario(s) failed\n' "$failures" >&2
    exit 1
fi

echo "battery_coverage_self_test: all scenarios passed"

#!/usr/bin/env bash
set -euo pipefail

# Proves the coverage reconciler fails on every way an entry can drop out of a
# battery, and passes only when the results account for the registered roster
# and the declared skip capabilities match the configuration they came from.
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
#
# The registry scenarios are load-bearing for a different reason. They are the
# only place where a skip is judged before it happens: an entry that CAN skip
# but never did still has to be declared, and the scenario that proves it is
# the one where every result is clean and the reconciliation fails anyway.

# The reconciler parses JUnit with python3, so without python3 there is nothing
# for this self-test to exercise. Decline by name rather than reporting a false
# pass or a failure that only means the interpreter is absent; CTest maps this
# exit to a skip through the entry's SKIP_RETURN_CODE, which is declared in
# tools/skip_declarations.txt.
if ! command -v python3 >/dev/null 2>&1; then
    printf 'battery_coverage_self_test: SKIP -- python3 is required to exercise the reconciler on the JUnit fixtures\n' >&2
    exit 77
fi

readonly reconciler="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/check_battery_coverage.sh"

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

# Every scenario is counted as it reports, and the total is checked against the
# number this file is written to contain. A scenario that stopped running — an
# edit that dropped it, a helper that returned early — would otherwise leave a
# smaller suite reporting the same success sentence.
readonly expected_scenarios=26
scenarios=0
failures=0

report() {
    printf 'battery_coverage_self_test: %s\n' "$1" >&2
    failures=$((failures + 1))
}

expect() {
    local scenario="$1"
    local junit="$2"
    local registered="$3"
    local declarations="$4"
    local expected_code="$5"
    local expected_text="$6"
    local output
    local code
    scenarios=$((scenarios + 1))
    output="$(bash "$reconciler" "$junit" "$registered" "$declarations" 2>&1)" && code=0 || code=$?

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

# Roster entries. A bare argument is an entry that cannot skip; `name:code`
# gives it a skip return code, written as the tab-separated second field the
# battery runner produces from ctest's JSON listing.
write_registered() {
    local path="$1"
    shift
    : >"$path"
    local entry
    for entry in "$@"; do
        if [[ "$entry" == *:* ]]; then
            printf '%s\t%s\n' "${entry%%:*}" "${entry#*:}" >>"$path"
        else
            printf '%s\n' "$entry" >>"$path"
        fi
    done
}

# Declaration lines, written as `name:tolerance:precondition`. The file always
# opens with a comment so the comment-skipping path is exercised by every
# scenario rather than by one of them.
write_declarations() {
    local path="$1"
    shift
    printf '# declared skip capabilities\n\n' >"$path"
    local entry rest
    for entry in "$@"; do
        rest="${entry#*:}"
        printf '%s\t%s\t%s\n' "${entry%%:*}" "${rest%%:*}" "${rest#*:}" >>"$path"
    done
}

# The declarations most scenarios use: entry b may refuse, and nothing else can
# skip. Scenarios that are about the registry write their own.
readonly refusal_declarations="$workspace/declare_b_refusal.txt"
write_declarations "$refusal_declarations" "b:refusal:a compositor started for this run"

readonly no_declarations="$workspace/declare_none.txt"
write_declarations "$no_declarations"

# --- coverage accounting ----------------------------------------------------

# Scenario 1: every registered entry ran. The clean pass.
junit="$workspace/all_ran.xml"
registered="$workspace/all_ran.txt"
write_junit "$junit" a run "a ok" b run "b ok" c run "c ok"
write_registered "$registered" a b c
expect "all entries ran" "$junit" "$registered" "$no_declarations" 0 \
    "3/3 registered entries accounted for"

# Scenario 2: a DECLARED REFUSAL is the one tolerated shortfall. It passes and
# is listed with its reason, so the cost of the refusal stays visible.
junit="$workspace/declined.xml"
registered="$workspace/declined.txt"
write_junit "$junit" a run "a ok" \
    b notrun "compositor-gate: DECL -- declined: no compositor was declared for this run" \
    c run "c ok"
write_registered "$registered" a "b:77" c
expect "declared refusal passes and is listed" "$junit" "$registered" "$refusal_declarations" 0 \
    "declared refusal"

# Scenario 3: THE TIGHT HALF. A skip that explained itself but did not declare
# a refusal is a capability hole, and a capability hole fails. This is the
# scenario that keeps the classification from degenerating into "printed
# something, therefore allowed" — which is the whole failure class this gate
# exists to close.
junit="$workspace/undeclared.xml"
registered="$workspace/undeclared.txt"
write_junit "$junit" a run "a ok" \
    b notrun "rhi-gate: SKIP -- could not run: no display server reachable" \
    c run "c ok"
write_registered "$registered" a "b:77" c
expect "explained capability skip still fails" "$junit" "$registered" "$refusal_declarations" 1 \
    "without a declared refusal"

# Scenario 4: the form is anchored, not merely mentioned. A skip whose output
# talks ABOUT declining — the token present but not as the line's own
# declaration — is not a declaration.
junit="$workspace/mentions_decl.xml"
registered="$workspace/mentions_decl.txt"
write_junit "$junit" a run "a ok" \
    b notrun "note: this gate would print compositor-gate: DECL -- declined: if it refused" \
    c run "c ok"
write_registered "$registered" a "b:77" c
expect "a mention of the token is not a declaration" "$junit" "$registered" \
    "$refusal_declarations" 1 "without a declared refusal"

# Scenario 5: a declaration with no reason after it declares nothing, and is
# refused. Otherwise the cheapest way past the gate would be the token alone.
junit="$workspace/empty_reason.xml"
registered="$workspace/empty_reason.txt"
write_junit "$junit" a run "a ok" b notrun "compositor-gate: DECL -- declined: " c run "c ok"
write_registered "$registered" a "b:77" c
expect "a declaration with no reason fails" "$junit" "$registered" "$refusal_declarations" 1 \
    "without a declared refusal"

# Scenario 6: a test disabled in the build configuration — status "disabled",
# as ctest records static_analysis under the sanitizer preset — is an intended
# opt-out. It passes and is listed, and is not counted as having run.
junit="$workspace/disabled.xml"
registered="$workspace/disabled.txt"
write_junit "$junit" a run "a ok" b disabled "Disabled" c run "c ok"
write_registered "$registered" a b c
expect "disabled entry passes and is listed" "$junit" "$registered" "$no_declarations" 0 "disabled"

# Scenario 7: a silent skip — notrun with nothing captured — is the defect and
# must fail.
junit="$workspace/silent_skip.xml"
registered="$workspace/silent_skip.txt"
write_junit "$junit" a run "a ok" b notrun "" c run "c ok"
write_registered "$registered" a "b:77" c
expect "silent skip fails" "$junit" "$registered" "$refusal_declarations" 1 "silent skip"

# Scenario 8: a registered entry absent from the results fails as missing.
junit="$workspace/missing.xml"
registered="$workspace/missing.txt"
write_junit "$junit" a run "a ok" b run "b ok"
write_registered "$registered" a b c
expect "missing entry fails" "$junit" "$registered" "$no_declarations" 1 "missing"

# Scenario 9: a result not on the roster fails as unexpected — a stale list is
# not a roster the reconciler can trust.
junit="$workspace/unexpected.xml"
registered="$workspace/unexpected.txt"
write_junit "$junit" a run "a ok" b run "b ok" z run "z ok"
write_registered "$registered" a b
expect "unexpected entry fails" "$junit" "$registered" "$no_declarations" 1 "unexpected"

# Scenario 10: an empty roster is a broken enumeration, not an empty suite, and
# fails rather than vacuously passing.
junit="$workspace/empty_reg.xml"
registered="$workspace/empty_reg.txt"
write_junit "$junit" a run "a ok"
: >"$registered"
expect "empty roster fails" "$junit" "$registered" "$no_declarations" 1 "roster is empty"

# Scenario 11: results where nothing ran — every entry declined, each of them
# properly — is the floor, not coverage, and fails. Declaring a refusal buys
# tolerance for one entry, never for a whole battery.
junit="$workspace/nothing_ran.xml"
registered="$workspace/nothing_ran.txt"
declarations="$workspace/nothing_ran_decl.txt"
write_junit "$junit" a notrun "a-gate: DECL -- declined: nothing declared" \
    b notrun "b-gate: DECL -- declined: nothing declared"
write_registered "$registered" "a:77" "b:77"
write_declarations "$declarations" "a:refusal:a compositor started for this run" \
    "b:refusal:a compositor started for this run"
expect "nothing ran fails even when every entry declined" "$junit" "$registered" \
    "$declarations" 1 "nothing ran"

# Scenario 12: a missing results file fails rather than being read as a pass.
registered="$workspace/no_junit.txt"
write_registered "$registered" a b
expect "missing junit file fails" "$workspace/does_not_exist.xml" "$registered" \
    "$no_declarations" 1 "results not found"

# Scenario 13: an unparseable results file fails by name.
junit="$workspace/malformed.xml"
registered="$workspace/malformed.txt"
printf '<testsuite><testcase name="a"\n' >"$junit"
write_registered "$registered" a
expect "malformed junit fails" "$junit" "$registered" "$no_declarations" 1 "not parseable"

# --- the declaration registry -----------------------------------------------

# Scenario 14: THE ONE THAT MATTERS. Every result is clean — nothing skipped,
# nothing is missing, nothing is unexpected — and the reconciliation fails
# anyway, because an entry was given the ability to skip and nobody wrote it
# down. Without this the registry would only ever be consulted on a run where
# something already skipped, which is exactly the too-late position the whole
# file exists to leave.
junit="$workspace/undeclared_capability.xml"
registered="$workspace/undeclared_capability.txt"
write_junit "$junit" a run "a ok" b run "b ok" c run "c ok"
write_registered "$registered" a "b:77" c
expect "an undeclared skip capability fails on an otherwise clean run" "$junit" "$registered" \
    "$no_declarations" 1 "can skip without being declared"

# Scenario 15: and it names the entry, so the reader is not left to diff the
# roster against the file by hand.
expect "the undeclared entry is named" "$junit" "$registered" "$no_declarations" 1 \
    "b: carries a skip return code with no line in the declarations"

# Scenario 16: a declaration naming an entry that is not registered at all is
# stale — a test was renamed or deleted and the file was not.
junit="$workspace/stale.xml"
registered="$workspace/stale.txt"
declarations="$workspace/stale_decl.txt"
write_junit "$junit" a run "a ok" c run "c ok"
write_registered "$registered" a c
write_declarations "$declarations" "b:refusal:a compositor started for this run"
expect "a declaration naming no registered entry fails" "$junit" "$registered" "$declarations" 1 \
    "declared, but no entry of that name is registered"

# Scenario 17: a declaration naming a registered entry that carries no skip
# return code fails too. A declaration that outlives its mechanism is the
# reverse drift, and it is the one that would otherwise let the file accumulate
# permissions nothing exercises.
junit="$workspace/inoperative.xml"
registered="$workspace/inoperative.txt"
write_junit "$junit" a run "a ok" b run "b ok" c run "c ok"
write_registered "$registered" a b c
expect "a declaration whose entry cannot skip fails" "$junit" "$registered" \
    "$refusal_declarations" 1 "the declaration outlived the mechanism it describes"

# Scenario 18: a skip return code other than the project's own fails. A second
# skip convention beside the declared one is a second mechanism to keep honest.
junit="$workspace/foreign_code.xml"
registered="$workspace/foreign_code.txt"
write_junit "$junit" a run "a ok" b run "b ok" c run "c ok"
write_registered "$registered" a "b:125" c
expect "a foreign skip return code fails" "$junit" "$registered" "$refusal_declarations" 1 \
    "skip return code 125, expected 77"

# Scenario 19: an entry that did not run and declares no skip capability at all
# fails by that name. This is the class no output-shaped rule can see: ctest
# records a missing executable or an unsatisfied dependency as not-run, and
# such an entry never had a skip return code to declare.
junit="$workspace/undeclared_notrun.xml"
registered="$workspace/undeclared_notrun.txt"
write_junit "$junit" a run "a ok" b notrun "Unable to find executable" c run "c ok"
write_registered "$registered" a b c
expect "a not-run entry with no declared capability fails" "$junit" "$registered" \
    "$no_declarations" 1 "declares no skip capability"

# Scenario 20: an entry declared as a capability precondition that skipped is a
# coverage hole and fails — the declaration records what the entry needs, it
# does not forgive its absence.
junit="$workspace/unmet.xml"
registered="$workspace/unmet.txt"
declarations="$workspace/unmet_decl.txt"
write_junit "$junit" a run "a ok" \
    b notrun "rhi-gate: SKIP -- could not run: no display server reachable" c run "c ok"
write_registered "$registered" a "b:77" c
write_declarations "$declarations" "b:capability:an offscreen OpenGL context"
expect "an unmet declared precondition fails" "$junit" "$registered" "$declarations" 1 \
    "unmet declared precondition"

# Scenario 21: and the failure prints the recorded precondition, so the reader
# is told what to provide rather than having to rediscover it.
expect "the unmet precondition is printed" "$junit" "$registered" "$declarations" 1 \
    "declared to require an offscreen OpenGL context"

# Scenario 22: a capability declaration is not a standing red. The same entry,
# on a machine that met the precondition, passes — which is what keeps the
# classification from being a way to mark an entry permanently broken.
junit="$workspace/met.xml"
write_junit "$junit" a run "a ok" b run "b ok" c run "c ok"
expect "a met precondition passes" "$junit" "$registered" "$declarations" 0 \
    "3/3 registered entries accounted for"

# Scenario 23: a missing declarations file fails by name rather than being
# treated as "nothing is declared", which would pass every roster with no skip
# capability in it and quietly disable the check on the ones that have some.
junit="$workspace/no_decl.xml"
registered="$workspace/no_decl.txt"
write_junit "$junit" a run "a ok"
write_registered "$registered" a
expect "a missing declarations file fails" "$junit" "$registered" \
    "$workspace/does_not_exist.txt" 1 "skip declarations not found"

# Scenario 24: a malformed declaration line fails by name and line number,
# rather than being skipped over as if it had not been written.
junit="$workspace/malformed_decl.xml"
registered="$workspace/malformed_decl.txt"
declarations="$workspace/malformed_decl_file.txt"
write_junit "$junit" a run "a ok"
write_registered "$registered" a
printf '# declared skip capabilities\nb\trefusal\n' >"$declarations"
expect "a malformed declaration line fails" "$junit" "$registered" "$declarations" 1 \
    "expected name<TAB>tolerance<TAB>precondition"

# Scenario 25: a tolerance outside the two the reconciler implements fails.
# Silently treating an unknown word as one of them would decide the entry's
# fate by whichever branch happened to be the fallback.
declarations="$workspace/unknown_tolerance.txt"
printf '# declared skip capabilities\nb\tallowed\tbecause\n' >"$declarations"
expect "an unknown tolerance fails" "$junit" "$registered" "$declarations" 1 \
    "is not one of refusal, capability"

# Scenario 26: the same entry declared twice fails. Two lines for one name
# means two answers to the same question, and picking either one silently is
# how a tolerance nobody wrote deliberately ends up in force.
declarations="$workspace/duplicate.txt"
printf '# declared skip capabilities\nb\trefusal\tfirst\nb\tcapability\tsecond\n' \
    >"$declarations"
expect "a duplicated declaration fails" "$junit" "$registered" "$declarations" 1 \
    "is declared more than once"

if ((scenarios != expected_scenarios)); then
    printf 'battery_coverage_self_test: reported %d scenario(s), expected %d\n' \
        "$scenarios" "$expected_scenarios" >&2
    failures=$((failures + 1))
fi

if ((failures != 0)); then
    printf 'battery_coverage_self_test: %d scenario(s) failed\n' "$failures" >&2
    exit 1
fi

printf 'battery_coverage_self_test: all %d scenarios passed\n' "$scenarios"

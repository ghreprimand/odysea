#!/usr/bin/env bash

# Verification battery runner with coverage reconciliation.
#
# `ctest` answers "did anything I ran fail". It does not answer "did everything
# that should run, run" — a skipped entry keeps the headline at "100% passed",
# and the executed count can fall by a third without changing a number. This
# runner adds the missing question: it captures the registered roster live from
# `ctest -N`, runs the battery capturing per-entry results as JUnit, and
# reconciles the two so a drop turns the run red. The only shortfall the
# reconciliation tolerates is an entry that printed a declared refusal —
# `<gate>: DECL -- declined: <reason>` — which is a policy this project
# enforces rather than a capability the machine lacks. Every other skip,
# whether it explained itself or not, fails.
#
# It is a wrapper rather than one more `ctest` entry on purpose. A reconciler
# registered inside the battery would run in the middle of it under `ctest -j`
# and could not see the entries scheduled after itself; the accounting is only
# sound once the whole run has finished. So the reconciliation happens here,
# after ctest returns, over results that are complete.
#
# The application smoke and both coverage self-tests ARE ordinary battery
# entries, so they run inside ctest and are themselves reconciled.
#
# Usage:
#   run_verification_battery.sh <build-directory> [extra ctest args...]
#
# Extra arguments are passed through to the battery run (for example -j8, or
# -R to scope it — though a scoped run will reconcile only what it selected).

set -euo pipefail

if [[ "$#" -lt 1 ]]; then
    printf 'usage: %s <build-directory> [extra ctest args...]\n' "$0" >&2
    exit 2
fi

build_directory="$1"
shift

if [[ ! -f "$build_directory/CTestTestfile.cmake" ]]; then
    printf 'verification_battery: FAIL -- no CTest configuration at %s\n' "$build_directory" >&2
    printf '  Configure and build first (cmake --preset <name> && cmake --build ...).\n' >&2
    exit 1
fi

tool_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

registered_list="$workspace/registered.txt"
junit_results="$workspace/results.junit.xml"

# The roster, captured live so it can never lag the real suite. `ctest -N`
# lists without running; the first sed pulls the test name from each
# "Test #N: name", the second strips the " (Disabled)" annotation ctest appends
# to a test disabled in the build configuration, so the roster name matches the
# bare name the JUnit records.
ctest --test-dir "$build_directory" -N \
    | sed -n 's/^[[:space:]]*Test[[:space:]]*#[0-9]*:[[:space:]]*//p' \
    | sed 's/[[:space:]]*(Disabled)[[:space:]]*$//' \
    >"$registered_list"
registered_count="$(grep -c . "$registered_list" || true)"

printf 'verification_battery: %s registered entries at %s\n' \
    "$registered_count" "$build_directory"

# Run the battery. A test failure must not stop the reconciliation: a run where
# entries both failed AND silently dropped needs both reported, so the ctest
# status is captured rather than allowed to abort the script.
set +e
ctest --test-dir "$build_directory" --output-junit "$junit_results" "$@"
ctest_status=$?
set -e

reconcile_status=0
bash "$tool_directory/check_battery_coverage.sh" "$junit_results" "$registered_list" \
    || reconcile_status=$?

printf '\nverification_battery: summary\n'
printf '  ctest exit        : %s\n' "$ctest_status"
printf '  reconciler exit   : %s\n' "$reconcile_status"

if [[ "$ctest_status" -ne 0 || "$reconcile_status" -ne 0 ]]; then
    printf 'verification_battery: FAIL -- '
    if [[ "$ctest_status" -ne 0 ]]; then
        printf 'a battery entry failed'
    fi
    if [[ "$ctest_status" -ne 0 && "$reconcile_status" -ne 0 ]]; then
        printf '; '
    fi
    if [[ "$reconcile_status" -ne 0 ]]; then
        printf 'coverage did not reconcile'
    fi
    printf '\n'
    exit 1
fi

printf 'verification_battery: PASS -- every registered entry ran or declared a refusal\n'

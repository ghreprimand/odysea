#!/usr/bin/env bash

# Verification battery runner with coverage reconciliation.
#
# `ctest` answers "did anything I ran fail". It does not answer "did everything
# that should run, run" — a skipped entry keeps the headline at "100% passed",
# and the executed count can fall by a third without changing a number. This
# runner adds the missing question: it captures the registered roster live from
# ctest, runs the battery capturing per-entry results as JUnit, and reconciles
# the two so a drop turns the run red. The only shortfall the reconciliation
# tolerates is an entry that both declares a refusal in
# tools/skip_declarations.txt and prints the declaration plus its matching
# isolated-compositor interlock proof. A bare `<gate>: DECL -- declined: ...`
# line is not enough. Every other skip,
# whether it explained itself or not, fails.
#
# The roster carries each entry's skip return code alongside its name, so the
# reconciler can also check the declarations against the configuration before
# it reads a single result: an entry able to skip without a declaration fails,
# and so does a declaration that no longer names one.
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

# The roster is parsed from ctest's JSON listing and the results from JUnit,
# both with python3. Saying so here turns its absence into one sentence instead
# of an interpreter error from the middle of a pipeline.
if ! command -v python3 >/dev/null 2>&1; then
    printf 'verification_battery: FAIL -- python3 is required to read the roster and the results\n' >&2
    exit 1
fi

tool_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

registered_list="$workspace/registered.txt"
junit_results="$workspace/results.junit.xml"

# The roster, captured live so it can never lag the real suite, and read from
# the machine-readable listing rather than the human one. `ctest -N` prints
# names; `--show-only=json-v1` prints names AND per-entry properties, which is
# where SKIP_RETURN_CODE lives. Taking the roster from there is what lets the
# reconciler know which entries are able to skip at all, instead of learning it
# on the first run where one did. It also reports the bare name of a test
# disabled in the build configuration, where the human listing appends a
# "(Disabled)" annotation that had to be stripped back off.
#
# Each line is the name, plus a tab and the skip return code for an entry that
# declares one.
ctest --test-dir "$build_directory" --show-only=json-v1 \
    | python3 -c '
import json
import sys

listing = json.load(sys.stdin)
for test in listing.get("tests", []):
    name = test.get("name", "")
    if not name:
        continue
    code = ""
    for entry in test.get("properties", []):
        if entry.get("name") == "SKIP_RETURN_CODE":
            code = str(entry.get("value", "")).strip()
    sys.stdout.write(name + ("\t" + code if code else "") + "\n")
' >"$registered_list"
registered_count="$(grep -c . "$registered_list" || true)"
skip_capable_count="$(grep -c "$(printf '\t')" "$registered_list" || true)"

printf 'verification_battery: %s registered entries at %s, %s able to skip\n' \
    "$registered_count" "$build_directory" "$skip_capable_count"

# Run the battery. A test failure must not stop the reconciliation: a run where
# entries both failed AND silently dropped needs both reported, so the ctest
# status is captured rather than allowed to abort the script.
set +e
ctest --test-dir "$build_directory" --output-junit "$junit_results" "$@"
ctest_status=$?
set -e

reconcile_status=0
bash "$tool_directory/check_battery_coverage.sh" "$junit_results" "$registered_list" \
    "$tool_directory/skip_declarations.txt" \
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

printf 'verification_battery: PASS -- every registered entry ran or proved an interlock refusal\n'

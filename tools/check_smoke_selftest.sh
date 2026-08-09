#!/usr/bin/env bash
set -euo pipefail

# Proves the smoke gate rejects every outcome the old zero-bytes criterion
# accepted, and accepts only a launch that is genuinely alive and clean.
#
# Each scenario asserts the SPECIFIC result, not merely pass or fail: a gate
# that fails a core dump for the wrong reason is as blind as one that passes
# it. The planted binaries are tiny stubs standing in for the application, so
# the gate's decision logic is exercised without a Qt process.

readonly smoke_gate="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/check_smoke.sh"

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

failures=0

report() {
    printf 'application_smoke_self_test: %s\n' "$1" >&2
    failures=$((failures + 1))
}

# Writes an executable stub with the given body and returns its path.
make_stub() {
    local name="$1"
    local body="$2"
    local path="$workspace/$name"
    printf '#!/bin/sh\n%s\n' "$body" >"$path"
    chmod +x "$path"
    printf '%s\n' "$path"
}

# Runs the gate against a stub and asserts its exit code and reason. The alive
# stubs are held for one second so the timeout, not the stub, decides the run.
expect_smoke() {
    local scenario="$1"
    local stub="$2"
    local expected_code="$3"
    local expected_text="$4"
    local output
    local code
    output="$(bash "$smoke_gate" "$stub" 1 2>&1)" && code=0 || code=$?

    if [[ "$code" != "$expected_code" ]]; then
        report "$scenario: expected exit $expected_code, got $code; output: $output"
        return
    fi
    if [[ -n "$expected_text" && "$output" != *"$expected_text"* ]]; then
        report "$scenario: expected the result to mention '$expected_text'; output: $output"
    fi
}

# Scenario 1: a launch that stays alive and says nothing is the one pass.
stub="$(make_stub alive_clean 'exec sleep 999')"
expect_smoke "alive and clean" "$stub" 0 "alive at timeout"

# Scenario 2: alive but announcing a platform fault fails, even though the
# process is still up when the timeout fires. This is the silently
# non-functional case the exit code alone cannot see.
stub="$(make_stub alive_dirty 'printf "qt.qpa.xcb: could not connect to display\n" >&2; exec sleep 999')"
expect_smoke "alive but faulting" "$stub" 1 "printed a fault line"

# Scenario 3: a core dump is the case the old criterion accepted. It must fail
# by name.
stub="$(make_stub aborts 'printf "Failed to create RHI\n" >&2; kill -ABRT $$')"
expect_smoke "aborts before timeout" "$stub" 1 "aborted (SIGABRT)"

# Scenario 4: a segmentation fault fails by name too.
stub="$(make_stub segfaults 'kill -SEGV $$')"
expect_smoke "segfaults before timeout" "$stub" 1 "segmentation fault"

# Scenario 5: an application that exits cleanly before the timeout did not stay
# alive, so it is not a healthy launch.
stub="$(make_stub exits_zero 'exit 0')"
expect_smoke "exits cleanly and early" "$stub" 1 "did not stay alive"

# Scenario 6: any other early exit fails, naming the status.
stub="$(make_stub exits_three 'exit 3')"
expect_smoke "exits non-zero and early" "$stub" 1 "status 3"

# Scenario 7: a path that is not an executable is rejected rather than treated
# as a silent pass.
expect_smoke "missing binary" "$workspace/does_not_exist" 1 "not an executable file"

if ((failures != 0)); then
    printf 'application_smoke_self_test: %d scenario(s) failed\n' "$failures" >&2
    exit 1
fi

echo "application_smoke_self_test: all scenarios passed"

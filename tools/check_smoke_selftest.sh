#!/usr/bin/env bash

# Discriminates the application smoke gate with planted executable mutations.
# The passing program verifies the pinned environment and stays alive. The
# failing programs abort or exit before the observation window, including an
# early status 124 that a timeout-status-only gate would accept.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly gate="$tools_directory/check_smoke.sh"

if [[ ! -x "$gate" ]]; then
    printf 'application_smoke_self_test: gate is missing or not executable\n' >&2
    exit 1
fi

readonly workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

failures=0
checked=0

report_failure() {
    printf 'application_smoke_self_test: %s\n' "$1" >&2
    failures=$((failures + 1))
}

make_stub() {
    local name="$1"
    local body="$2"
    local path="$workspace/$name"
    printf '#!/usr/bin/env bash\nset -euo pipefail\n%s\n' "$body" >"$path"
    chmod +x "$path"
    printf '%s\n' "$path"
}

expect_result() {
    local scenario="$1"
    local stub="$2"
    local expected_status="$3"
    local expected_text="$4"
    local output
    local status

    output="$(DISPLAY=:91 WAYLAND_DISPLAY=ambient-socket \
        XDG_CONFIG_HOME=/ambient/config XDG_CACHE_HOME=/ambient/cache \
        XDG_DATA_HOME=/ambient/data \
        bash "$gate" "$stub" 1 2>&1)" && status=0 || status=$?
    checked=$((checked + 1))

    if [[ "$status" != "$expected_status" ]]; then
        report_failure "$scenario: expected status $expected_status, got $status; output: $output"
        return
    fi
    if [[ "$output" != *"$expected_text"* ]]; then
        report_failure "$scenario: expected '$expected_text'; output: $output"
    fi
}

alive_stub="$(make_stub alive '
    [[ "${QT_QPA_PLATFORM:-}" == "offscreen" ]]
    [[ "${QT_FORCE_STDERR_LOGGING:-}" == "1" ]]
    [[ "${QT_QUICK_BACKEND:-}" == "software" ]]
    [[ -z "${DISPLAY:-}" ]]
    [[ -z "${WAYLAND_DISPLAY:-}" ]]
    [[ "${XDG_CONFIG_HOME:-}" != "/ambient/config" ]]
    [[ "${XDG_CACHE_HOME:-}" != "/ambient/cache" ]]
    [[ "${XDG_DATA_HOME:-}" != "/ambient/data" ]]
    [[ -d "$1" ]]
    exec sleep 30
')"
expect_result "alive with pinned environment" "$alive_stub" 0 \
    "harness closed it with SIGTERM"

abort_stub="$(make_stub aborts 'kill -ABRT $$')"
expect_result "abort mutation" "$abort_stub" 1 "died from SIGABRT (status 134)"

segfault_stub="$(make_stub segfaults 'kill -SEGV $$')"
expect_result "segmentation-fault mutation" "$segfault_stub" 1 \
    "died from SIGSEGV (status 139)"

clean_exit_stub="$(make_stub clean_exit 'exit 0')"
expect_result "early clean exit" "$clean_exit_stub" 1 \
    "exited with status 0 before 1s"

status_124_stub="$(make_stub status_124 'exit 124')"
expect_result "early status 124" "$status_124_stub" 1 \
    "exited with status 124 before 1s"

fatal_stub="$(make_stub fatal_while_alive '
    printf "qt.qpa.xcb: could not connect to display\n" >&2
    exec sleep 30
')"
expect_result "alive with fatal diagnostics" "$fatal_stub" 1 \
    "remained alive but reported a fatal diagnostic"

ignore_term_stub="$(make_stub ignores_term '
    trap "" TERM
    exec sleep 30
')"
expect_result "alive and ignoring SIGTERM" "$ignore_term_stub" 0 \
    "harness closed it with SIGKILL"

expect_result "missing executable" "$workspace/missing" 1 \
    "not an executable file"

if ((failures != 0)); then
    printf 'application_smoke_self_test: %d of %d scenarios failed\n' \
        "$failures" "$checked" >&2
    exit 1
fi

printf 'application_smoke_self_test: %d scenarios passed\n' "$checked"

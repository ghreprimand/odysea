#!/usr/bin/env bash

# Launches the application on the deterministic offscreen software path and
# proves that it remains alive for the observation window. A quiet stream is
# not evidence of life: Qt may route a fatal diagnostic to the journal when
# standard error is redirected, and an early exit can be quiet too.
#
# Usage:
#   check_smoke.sh <application-binary> [duration-seconds]

set -euo pipefail

if [[ "$#" -lt 1 || "$#" -gt 2 ]]; then
    printf 'usage: %s <application-binary> [duration-seconds]\n' "$0" >&2
    exit 2
fi

readonly application="$1"
readonly duration_seconds="${2:-5}"

if [[ ! -x "$application" ]]; then
    printf 'application_smoke: FAIL -- not an executable file: %s\n' \
        "$application" >&2
    exit 1
fi

if [[ ! "$duration_seconds" =~ ^[1-9][0-9]*$ ]]; then
    printf 'application_smoke: FAIL -- duration must be a positive integer: %s\n' \
        "$duration_seconds" >&2
    exit 1
fi

if ! command -v setsid >/dev/null 2>&1; then
    printf 'application_smoke: FAIL -- setsid is required for bounded cleanup\n' >&2
    exit 1
fi

readonly workspace="$(mktemp -d)"
readonly stdout_capture="$workspace/stdout"
readonly stderr_capture="$workspace/stderr"
readonly termination_marker="$workspace/harness-termination"
readonly smoke_directory="$workspace/directory"

mkdir -p "$workspace/config" "$workspace/cache" "$workspace/data" \
    "$smoke_directory"

application_pid=""
watchdog_pid=""

cleanup() {
    if [[ -n "$watchdog_pid" ]]; then
        kill "$watchdog_pid" 2>/dev/null || true
        wait "$watchdog_pid" 2>/dev/null || true
    fi

    if [[ -n "$application_pid" ]] && kill -0 "$application_pid" 2>/dev/null; then
        kill -TERM -- "-$application_pid" 2>/dev/null || true
        sleep 0.1
        kill -KILL -- "-$application_pid" 2>/dev/null || true
        wait "$application_pid" 2>/dev/null || true
    fi

    rm -rf -- "$workspace"
}

trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Do not leave a core file behind when the program under test aborts. The
# process is a session leader so cleanup can terminate the whole launched
# process group without reaching any unrelated process.
ulimit -c 0
setsid env \
    -u DISPLAY \
    -u WAYLAND_DISPLAY \
    QT_FORCE_STDERR_LOGGING=1 \
    QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    XDG_CONFIG_HOME="$workspace/config" \
    XDG_CACHE_HOME="$workspace/cache" \
    XDG_DATA_HOME="$workspace/data" \
    "$application" "$smoke_directory" >"$stdout_capture" 2>"$stderr_capture" &
application_pid=$!

# This subprocess owns the observation deadline. It records a marker only
# after the application is still addressable and the harness successfully
# sends the terminating signal. An application that exits early with status
# 124 therefore cannot impersonate a timeout.
bash -c '
    duration="$1"
    application_pid="$2"
    marker="$3"

    sleep "$duration"
    if kill -0 "$application_pid" 2>/dev/null; then
        printf "TERM\n" >"$marker"
        if ! kill -TERM -- "-$application_pid" 2>/dev/null; then
            rm -f -- "$marker"
            exit 0
        fi
    else
        exit 0
    fi

    sleep 1
    if kill -0 "$application_pid" 2>/dev/null; then
        printf "KILL\n" >"$marker"
        if ! kill -KILL -- "-$application_pid" 2>/dev/null; then
            printf "TERM\n" >"$marker"
        fi
    fi
' -- "$duration_seconds" "$application_pid" "$termination_marker" &
watchdog_pid=$!

set +e
wait "$application_pid"
application_status=$?
set -e
application_pid=""

if [[ -n "$watchdog_pid" ]]; then
    if [[ ! -f "$termination_marker" ]]; then
        kill "$watchdog_pid" 2>/dev/null || true
    fi
    wait "$watchdog_pid" 2>/dev/null || true
    watchdog_pid=""
fi

if [[ -f "$termination_marker" ]]; then
    termination_signal="$(<"$termination_marker")"
else
    termination_signal=""
fi

readonly fault_pattern='qt[.]qpa.*(could not|failed|fatal)|could not load the qt platform plugin|no qt platform plugin could be initialized|failed to create|scene.?graph.*(failed|not functional)|addresssanitizer|undefinedbehaviorsanitizer|runtime error:|summary: .*sanitizer|(^|[^[:alpha:]])fatal([^[:alpha:]]|$)'
fault_lines="$(LC_ALL=C grep -aEi "$fault_pattern" "$stderr_capture" \
    | head -n 20 || true)"
readonly stdout_bytes="$(wc -c <"$stdout_capture" | tr -d ' ')"
readonly stderr_bytes="$(wc -c <"$stderr_capture" | tr -d ' ')"

print_context() {
    printf '  platform=offscreen backend=software duration=%ss status=%s stdout=%sB stderr=%sB\n' \
        "$duration_seconds" "$application_status" "$stdout_bytes" "$stderr_bytes" >&2
    if [[ -n "$fault_lines" ]]; then
        printf '  fatal diagnostics:\n' >&2
        printf '%s\n' "$fault_lines" | sed 's/^/    /' >&2
    elif [[ "$stderr_bytes" != "0" ]]; then
        printf '  standard error (first 8192 bytes):\n' >&2
        head -c 8192 "$stderr_capture" | sed 's/^/    /' >&2
        printf '\n' >&2
    fi
}

if [[ -n "$termination_signal" ]]; then
    if [[ -n "$fault_lines" ]]; then
        printf 'application_smoke: FAIL -- remained alive but reported a fatal diagnostic\n' >&2
        print_context
        exit 1
    fi

    printf 'application_smoke: PASS -- alive for %ss; harness closed it with SIG%s' \
        "$duration_seconds" "$termination_signal"
    printf ' (status %s)\n' "$application_status"
    exit 0
fi

if ((application_status >= 129 && application_status <= 192)); then
    signal_number=$((application_status - 128))
    signal_name="$(kill -l "$signal_number" 2>/dev/null || printf 'UNKNOWN')"
    printf 'application_smoke: FAIL -- died from SIG%s (status %s) before %ss\n' \
        "$signal_name" "$application_status" "$duration_seconds" >&2
else
    printf 'application_smoke: FAIL -- exited with status %s before %ss\n' \
        "$application_status" "$duration_seconds" >&2
fi
print_context
exit 1

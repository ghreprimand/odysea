#!/usr/bin/env bash

# Application smoke gate.
#
# A smoke launch answers one question: does the application come up and stay
# up without a fatal fault. The criterion this gate replaces — "zero bytes on
# both streams" — could not answer it, because three different outcomes are
# byte-identical under redirection:
#   - a healthy launch that Qt routes to the journal (stderr is not a terminal),
#   - a launch that stays alive but is silently non-functional,
#   - a core dump, whose only bytes are `timeout`'s own "dumped core" line.
# A criterion a core dump satisfies is not a criterion.
#
# This gate makes the three distinguishable and demands the healthy one:
#   1. QT_FORCE_STDERR_LOGGING=1 so Qt's diagnostics reach stderr instead of
#      the journal, where a fault names itself.
#   2. The launch must be ALIVE when the timeout fires — exit status exactly
#      124, the timeout signal. An early exit (the window closed itself, a
#      clean 0, any non-timeout code) and an abort (134 = SIGABRT, 139 =
#      SIGSEGV, 137 = SIGKILL) are each reported by name and fail.
#   3. stderr must carry no platform-plugin, RHI, scene-graph, or sanitizer
#      failure line. Zero bytes is no longer sufficient evidence on its own;
#      the evidence is the timeout status together with clean diagnostics.
#
# The launch is forced onto the offscreen platform with the SOFTWARE scene
# graph — QT_QUICK_BACKEND=software, which is the real software key. (QSG_RHI_
# BACKEND=software is not a valid key; Qt answers "Unknown key" and falls back
# to the default, so a smoke spelling it that way runs the default twice and
# proves nothing.) Offscreen software renders at device pixel ratio 1 by
# construction and runs on every machine, display or not, which is what makes
# this gate deterministic. A launch on a real GPU path is a different and
# stronger check, and is covered by the real-compositor presentation gate, not
# by a second offscreen smoke that would be this one over again.
#
# Usage:
#   check_smoke.sh <application-binary> [timeout-seconds]

set -euo pipefail

if [[ "$#" -lt 1 || "$#" -gt 2 ]]; then
    printf 'usage: %s <application-binary> [timeout-seconds]\n' "$0" >&2
    exit 2
fi

application="$1"
timeout_seconds="${2:-5}"

if [[ ! -x "$application" ]]; then
    printf 'application_smoke: FAIL -- not an executable file: %s\n' "$application" >&2
    exit 1
fi

stderr_capture="$(mktemp)"
stdout_capture="$(mktemp)"
trap 'rm -f -- "$stderr_capture" "$stdout_capture"' EXIT

# A fault names itself only when Qt is told to write to stderr rather than the
# journal. The offscreen software backend is the deterministic, display-free
# path; it is the real software key, not the invalid RHI spelling.
set +e
env \
    QT_FORCE_STDERR_LOGGING=1 \
    QT_QPA_PLATFORM=offscreen \
    QT_QUICK_BACKEND=software \
    timeout "${timeout_seconds}" "$application" \
    >"$stdout_capture" 2>"$stderr_capture"
status=$?
set -e

# Lines that mean the launch is not healthy even if the process is alive. Each
# is a concrete failure Qt or a sanitizer prints; none appears in a clean
# offscreen software launch.
readonly fault_pattern='qt\.qpa|Failed to create|scenegraph is not functional|no Qt platform plugin|Could not load the Qt platform plugin|AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:|SUMMARY: .*Sanitizer'

fault_lines="$(grep -E "$fault_pattern" "$stderr_capture" || true)"
stderr_bytes="$(wc -c <"$stderr_capture" | tr -d ' ')"

report_and_fail() {
    printf 'application_smoke: FAIL -- %s\n' "$1" >&2
    printf '  platform=offscreen backend=software timeout=%ss exit=%s stderr=%sB\n' \
        "$timeout_seconds" "$status" "$stderr_bytes" >&2
    if [[ -n "$fault_lines" ]]; then
        printf '  fault lines on stderr:\n' >&2
        printf '%s\n' "$fault_lines" | sed 's/^/    /' >&2
    fi
    exit 1
}

case "$status" in
    124)
        # Alive when the timeout fired. This is the one healthy outcome; it
        # still fails if the process was alive but announcing a fault.
        if [[ -n "$fault_lines" ]]; then
            report_and_fail "stayed alive but printed a fault line on stderr"
        fi
        printf 'application_smoke: PASS -- alive at timeout, clean stderr\n'
        printf '  platform=offscreen backend=software (device pixel ratio 1 by construction)'
        printf ' timeout=%ss exit=124 stderr=%sB\n' "$timeout_seconds" "$stderr_bytes"
        exit 0
        ;;
    134) report_and_fail "aborted (SIGABRT) before the timeout" ;;
    139) report_and_fail "segmentation fault (SIGSEGV) before the timeout" ;;
    137) report_and_fail "killed (SIGKILL) before the timeout" ;;
    0) report_and_fail "exited cleanly before the timeout — it did not stay alive" ;;
    *) report_and_fail "exited with status ${status} before the timeout" ;;
esac

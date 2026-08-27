#!/usr/bin/env bash

# The real-compositor launcher may render only on the isolated Wayland socket
# its harness declared. Ambient display variables are inputs an interactive
# session leaves behind, not authority to choose a platform. Exercise those
# variables with deliberately unreachable endpoints and require a policy
# refusal before Qt can create an application or connect to either display.

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    printf 'usage: %s <compositor-launcher>\n' "$0" >&2
    exit 2
fi

launcher="$1"
if [[ ! -x "$launcher" ]]; then
    printf 'compositor-platform-contract: FAIL -- launcher is not executable: %s\n' \
        "$launcher" >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT
chmod 700 "$workspace"

failures=0
run_refusal_case() {
    local name="$1"
    shift
    local output="$workspace/$name.txt"
    local status=0

    set +e
    env \
        -u DISPLAY \
        -u WAYLAND_DISPLAY \
        -u QT_QPA_PLATFORM \
        -u QT_QUICK_BACKEND \
        -u QSG_RHI_BACKEND \
        -u ODYSEA_ISOLATED_COMPOSITOR \
        -u ODYSEA_ISOLATED_COMPOSITOR_NONCE \
        -u ODYSEA_ISOLATED_COMPOSITOR_RUNDIR \
        -u ODYSEA_REQUIRE_COMPOSITOR \
        XDG_RUNTIME_DIR="$workspace" \
        "$@" \
        "$launcher" >"$output" 2>&1
    status=$?
    set -e

    if [[ "$status" -ne 77 ]]; then
        printf 'compositor-platform-contract: FAIL %s -- exit %s, expected 77\n' \
            "$name" "$status" >&2
        head -n 20 "$output" >&2
        failures=$((failures + 1))
        return
    fi
    if ! grep -Fq \
        'compositor-gate: DECL -- declined: ODYSEA_ISOLATED_COMPOSITOR is not set' \
        "$output"; then
        printf 'compositor-platform-contract: FAIL %s -- ambient display was not declined\n' \
            "$name" >&2
        head -n 20 "$output" >&2
        failures=$((failures + 1))
        return
    fi
    if grep -Eq 'compositor-gate: (RUN|SKIP|FAIL)' "$output"; then
        printf 'compositor-platform-contract: FAIL %s -- launcher selected a platform\n' \
            "$name" >&2
        head -n 20 "$output" >&2
        failures=$((failures + 1))
        return
    fi

    printf 'compositor-platform-contract: PASS %s\n' "$name"
}

# No endpoint is live. A launcher that regresses to selecting from the ambient
# environment will take its probe path and report SKIP/FAIL instead of DECL,
# which makes the corresponding named case red without touching a real session.
run_refusal_case ambient_x11 \
    DISPLAY=:65535 QT_QPA_PLATFORM=xcb
run_refusal_case ambient_wayland \
    WAYLAND_DISPLAY=wayland-ambient-unreachable QT_QPA_PLATFORM=wayland
run_refusal_case ambient_mixed \
    DISPLAY=:65535 WAYLAND_DISPLAY=wayland-ambient-unreachable QT_QPA_PLATFORM=xcb

if [[ "$failures" -ne 0 ]]; then
    printf 'compositor-platform-contract: FAIL -- %s case(s) failed\n' "$failures" >&2
    exit 1
fi

printf 'compositor-platform-contract: PASS -- ambient variables never select a platform\n'

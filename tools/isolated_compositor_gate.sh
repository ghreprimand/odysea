#!/usr/bin/env bash
# Runs a compositor-dependent GPU gate against a compositor this harness starts
# and tears down, never against a session already in use.
#
# WHY THIS EXISTS. The real-compositor validation gate renders an activating
# window and asks to be brought to the front. A window like that, placed into a
# session someone is using, can take input focus onto a surface nobody is
# looking at; if the run ends abnormally it leaves that state behind, and the
# visible surfaces stop receiving keyboard and pointer input until the session
# is restarted. The gate therefore refuses to run unless
# ODYSEA_ISOLATED_COMPOSITOR names the Wayland socket of the compositor started
# for this run and disposed of afterwards (see
# app/tests/tst_presentation_compositor_launcher.cpp). This harness is the
# thing that legitimately makes that declaration: it starts a compositor in a
# private runtime directory, points the gate at it, and disposes of it on every
# exit path a trap can reach.
#
# CONTRACT
#   isolated_compositor_gate.sh <gate-command> [args...]
# Acquires a cross-run lock, starts a compositor in a private XDG_RUNTIME_DIR,
# waits for its socket, exports WAYLAND_DISPLAY + XDG_RUNTIME_DIR +
# ODYSEA_ISOLATED_COMPOSITOR into the gate's environment, runs the gate, and
# tears the compositor down. The harness — not the compositor command — chooses
# WAYLAND_DISPLAY, so nothing the command does can point the gate at a session
# that was already in the environment.
#
# EXIT STATUS
#   0..N   the gate command's own status, passed through unchanged
#   77     skipped: another run holds the cross-run lock (never proceeds)
#   1      the harness could not start a compositor for the run
#
# ENVIRONMENT KNOBS (defaults are the production settings)
#   ODYSEA_GATE_COMPOSITOR_CMD  How to start the compositor, run under a private
#                               XDG_RUNTIME_DIR. Default is Hyprland headless.
#                               The self-test overrides this with a stub so the
#                               lock, trap, teardown, and reaper are provable
#                               with no real compositor running.
#   ODYSEA_GATE_LOCK_WAIT       Seconds to wait for the cross-run lock before
#                               skipping by name. Default 0 (try once).
#   ODYSEA_GATE_READY_TIMEOUT   Seconds to wait for the compositor socket to
#                               appear. Default 20.
#   ODYSEA_GATE_STATE_DIR       Directory holding the fixed lock and the private
#                               run directories. Default is a fixed per-user
#                               path outside every worktree. The self-test
#                               points it at a sandbox; changing it in a real
#                               run defeats cross-worktree exclusion, the same
#                               way setting an isolated-compositor declaration
#                               by hand
#                               defeats the interlock.
set -euo pipefail

readonly skip_status=77
readonly harness_failure_status=1

# The ambient runtime directory, captured before anything is overridden. The
# fixed lock lives next to it and the private run directory is checked against
# it so the gate is never pointed back at the session it inherited.
readonly ambient_runtime="${XDG_RUNTIME_DIR:-/tmp}"

readonly state_dir="${ODYSEA_GATE_STATE_DIR:-$ambient_runtime/odysea-gate}"
readonly lock_path="$state_dir/lock"
readonly runs_parent="$state_dir/runs"
readonly lock_wait="${ODYSEA_GATE_LOCK_WAIT:-0}"
readonly ready_timeout="${ODYSEA_GATE_READY_TIMEOUT:-20}"
# The default is a headless wlroots compositor. Starting it and configuring an
# output that presents a genuine 2x surface is the part that stays unproven
# until an isolated-compositor run is available; the self-test overrides this
# with a stub, and the lock, trap, teardown, and reaper are proven without it.
readonly compositor_cmd="${ODYSEA_GATE_COMPOSITOR_CMD:-env WLR_BACKENDS=headless Hyprland}"

log() { printf 'isolated-compositor-gate: %s\n' "$*" >&2; }

usage() {
    log "usage: isolated_compositor_gate.sh <gate-command> [args...]"
    exit "$harness_failure_status"
}

# The starttime field (proc(5) field 22) of a live process, used to tell a pid
# apart from a later reuse of the same number. Empty when the pid is gone.
starttime_of() {
    local pid="$1" stat rest
    stat="$(cat "/proc/$pid/stat" 2>/dev/null)" || return 0
    # comm (field 2) is wrapped in parentheses and may itself contain spaces and
    # parentheses, so everything up to the last ") " is dropped before the
    # remaining fields are split. starttime is field 22 overall, which is field
    # 20 of what remains once pid and comm are removed.
    rest="${stat##*') '}"
    # shellcheck disable=SC2086
    set -- $rest
    printf '%s' "${20:-}"
}

# Signals a process group by its leader pid, but only when that pid still names
# the process recorded with it. The starttime comparison closes the window where
# the number has been reused by an unrelated process since it was written down.
kill_group_if_matches() {
    local pid="$1" recorded_starttime="$2" signal="$3"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 0
    ((pid > 1)) || return 0
    local current
    current="$(starttime_of "$pid")"
    [[ -n "$current" && "$current" == "$recorded_starttime" ]] || return 0
    # Negative pid targets the whole process group; the compositor is a session
    # leader, so its group id equals its pid.
    kill "-$signal" "-$pid" 2>/dev/null || kill "-$signal" "$pid" 2>/dev/null || true
}

# Removes one abandoned run directory: stops each process group recorded in it,
# if that exact process is still alive, then deletes the directory. Both the
# compositor and the gate are recorded, so an untrappable kill of a harness
# leaves neither its compositor nor the gate's own subtree behind past the next
# run. Only ever called for a directory whose liveness lock this process already
# holds, so no live harness owns it.
reap_run_dir() {
    local dir="$1" pidfile pid recorded_starttime
    for pidfile in "$dir/compositor.pid" "$dir/gate.pid"; do
        [[ -f "$pidfile" ]] || continue
        read -r pid recorded_starttime <"$pidfile" || true
        [[ -n "${pid:-}" ]] || continue
        kill_group_if_matches "$pid" "${recorded_starttime:-}" TERM
    done
    rm -rf -- "$dir"
    log "reaped an abandoned run directory: $dir"
}

# Sweeps for run directories no live harness holds and disposes of them. A
# harness holds its run directory's liveness lock for its whole lifetime; the
# kernel drops that lock when the process ends by any means, SIGKILL included.
# So a directory whose lock this sweep can take is one whose harness is gone,
# and reaping it here bounds the residue an untrappable kill leaves behind to
# the interval until the next run.
reap_stale_runs() {
    [[ -d "$runs_parent" ]] || return 0
    local dir probe_fd
    for dir in "$runs_parent"/run.*; do
        [[ -d "$dir" ]] || continue
        [[ -f "$dir/harness.lock" ]] || continue
        exec {probe_fd}>"$dir/harness.lock" || continue
        if flock -n "$probe_fd"; then
            reap_run_dir "$dir"
        fi
        exec {probe_fd}>&-
    done
}

# State the teardown needs. Set as the run progresses; cleanup tolerates each
# being unset, so it is correct at every point a signal can arrive.
compositor_pid=""
compositor_starttime=""
private_runtime=""
gate_pid=""
gate_group_pid=""
cleanup_done=0

stop_group() {
    local pid="$1" starttime="$2"
    [[ -n "$pid" ]] || return 0
    kill_group_if_matches "$pid" "$starttime" TERM
    local waited=0
    while ((waited < 20)); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.1
        ((waited += 1))
    done
    kill_group_if_matches "$pid" "$starttime" KILL
}

cleanup() {
    ((cleanup_done)) && return 0
    cleanup_done=1
    if [[ -n "$gate_group_pid" ]]; then
        # The gate ran in its own session; stop that whole group so a launcher's
        # own children go with it.
        kill -TERM "-$gate_group_pid" 2>/dev/null || true
    fi
    if [[ -n "$compositor_pid" ]]; then
        stop_group "$compositor_pid" "$compositor_starttime"
    fi
    [[ -n "$private_runtime" ]] && rm -rf -- "$private_runtime"
}

# --- Argument and precondition checks ---------------------------------------
(($# >= 1)) || usage
command -v flock >/dev/null 2>&1 || {
    log "flock (util-linux) is required for cross-run exclusion and is not installed"
    exit "$harness_failure_status"
}

mkdir -p "$runs_parent"

# --- Cross-run lock (c) ------------------------------------------------------
# Held through an open descriptor for the whole run, so the kernel releases it
# when this process ends by any means. The lock file at the fixed path is never
# removed: it is the rendezvous every worktree opens, and deleting it would let
# two runs open different inodes and both proceed.
exec {lock_fd}>"$lock_path"
if ((lock_wait > 0)); then
    lock_ok=0
    flock -w "$lock_wait" -x "$lock_fd" && lock_ok=1 || lock_ok=0
else
    lock_ok=0
    flock -n -x "$lock_fd" && lock_ok=1 || lock_ok=0
fi
if ((!lock_ok)); then
    log "SKIP -- another compositor gate run holds $lock_path; not proceeding."
    log "Cross-run exclusion is by design: one compositor renders at a time across all worktrees."
    exit "$skip_status"
fi

# The lock is held: from here on cleanup must run on every path out.
trap 'cleanup' EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM

# Dispose of anything an earlier untrappable kill abandoned before starting new.
reap_stale_runs

# --- Start the compositor in a private runtime directory --------------------
private_runtime="$(mktemp -d "$runs_parent/run.XXXXXX")"
if [[ "$private_runtime" == "$ambient_runtime" ]]; then
    log "refusing: the private runtime directory resolved to the ambient one ($ambient_runtime)"
    exit "$harness_failure_status"
fi

# A liveness lock held for this run's lifetime. reap_stale_runs in another run
# uses it to tell a live run from an abandoned one.
exec {liveness_fd}>"$private_runtime/harness.lock"
flock -n -x "$liveness_fd" || {
    log "refusing: could not take the liveness lock on a directory we just created"
    exit "$harness_failure_status"
}

# The compositor creates its socket in its own runtime directory and does not
# read WAYLAND_DISPLAY, which is cleared so it can never bind the ambient name.
# setsid puts it in its own session, so its group can be stopped as a unit and
# it does not share the harness's controlling terminal. The lock descriptors are
# closed for it ({lock_fd}>&- {liveness_fd}>&-): a child that inherited them
# would hold the flock open, so a later SIGKILL of the harness would leave the
# lock held by the surviving orphan instead of released by the kernel, and the
# reaper — which depends on the liveness lock being free once the harness is
# gone — could never run.
WAYLAND_DISPLAY="" XDG_RUNTIME_DIR="$private_runtime" \
    setsid bash -c "$compositor_cmd" {lock_fd}>&- {liveness_fd}>&- &
compositor_pid="$!"
compositor_starttime="$(starttime_of "$compositor_pid")"
printf '%s %s\n' "$compositor_pid" "$compositor_starttime" >"$private_runtime/compositor.pid"

# --- Wait for the socket ----------------------------------------------------
wayland_display=""
ready_deadline=$((SECONDS + ready_timeout))
while ((SECONDS < ready_deadline)); do
    for candidate in "$private_runtime"/wayland-*; do
        [[ -S "$candidate" ]] || continue
        case "$candidate" in
            *.lock) continue ;;
        esac
        wayland_display="$(basename "$candidate")"
        break
    done
    [[ -n "$wayland_display" ]] && break
    if ! kill -0 "$compositor_pid" 2>/dev/null; then
        log "the compositor exited before advertising a socket"
        exit "$harness_failure_status"
    fi
    sleep 0.1
done
if [[ -z "$wayland_display" ]]; then
    log "timed out after ${ready_timeout}s waiting for the compositor socket"
    exit "$harness_failure_status"
fi

# The socket lives inside the private runtime directory, which was already
# refused if it equalled the ambient one, so the gate's WAYLAND_DISPLAY and
# XDG_RUNTIME_DIR together cannot resolve to the inherited session. The
# declaration carries that exact socket name too: a launcher accepts it only
# when it equals WAYLAND_DISPLAY, so a failed or stale export cannot authorise
# a different session.
log "RUN -- compositor ready at $private_runtime/$wayland_display; running the gate"

# --- Run the gate against the isolated compositor ---------------------------
# The gate runs in its own session so a signal to the harness is handled by the
# trap here rather than being swallowed while a foreground child blocks, and so
# the gate's whole process group can be stopped on teardown.
XDG_RUNTIME_DIR="$private_runtime" \
    WAYLAND_DISPLAY="$wayland_display" \
    ODYSEA_ISOLATED_COMPOSITOR="$wayland_display" \
    setsid "$@" {lock_fd}>&- {liveness_fd}>&- &
gate_pid="$!"
gate_group_pid="$gate_pid"
printf '%s %s\n' "$gate_pid" "$(starttime_of "$gate_pid")" >"$private_runtime/gate.pid"

set +e
wait "$gate_pid"
gate_status=$?
set -e
gate_group_pid=""

exit "$gate_status"

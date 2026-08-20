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
# Acquires a cross-run lock, proves the compositor command is headless on the
# compositor actually installed, starts it in a private XDG_RUNTIME_DIR with a
# per-run token, waits for its socket, exports WAYLAND_DISPLAY +
# XDG_RUNTIME_DIR + ODYSEA_ISOLATED_COMPOSITOR +
# ODYSEA_ISOLATED_COMPOSITOR_NONCE into the gate's environment, runs the gate,
# and tears the compositor down.
#
# WHAT ACTUALLY ISOLATES THE RUN. Not the socket name: the harness clears
# WAYLAND_DISPLAY and then DISCOVERS whatever name the compositor bound inside
# the private directory, which on a compositor that numbers from zero is
# routinely the same name the live session uses. The isolation is the private
# runtime directory, and the proof that a gate is looking at this run's
# compositor rather than at one that was already listening is the token written
# into that directory. An earlier version of this comment claimed the harness
# chose the name; it did not, and a false safety claim in the file auditors
# read is how three holes survived review here.
#
# EXIT STATUS
#   0..N   the gate command's own status, passed through unchanged
#   77     skipped: another run holds the cross-run lock (never proceeds)
#   1      the harness could not start a compositor for the run
#
# ENVIRONMENT KNOBS (defaults are the production settings)
#   ODYSEA_GATE_COMPOSITOR_CMD  How to start the compositor, run under a private
#                               XDG_RUNTIME_DIR. The default names a headless
#                               wlroots backend. The self-test overrides this
#                               with a stub so the lock, trap, teardown, and
#                               reaper are provable with no real compositor
#                               running.
#   ODYSEA_GATE_HEADLESS_ENV    The environment variable in the compositor
#                               command that selects a headless backend, and
#                               which the harness verifies the installed
#                               compositor actually reads. Default WLR_BACKENDS.
#                               Set alongside ODYSEA_GATE_COMPOSITOR_CMD when
#                               pointing the harness at a different compositor.
#   ODYSEA_GATE_STUB_COMPOSITOR Set by the self-test to declare that the
#                               compositor command is a stub that opens a
#                               socket and nothing else. It bypasses the
#                               headless proof — which has no meaning for a
#                               stub — and nothing else. Production must never
#                               set it, and the refusal it bypasses is the one
#                               control that keeps this harness off the seat.
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
readonly headless_env="${ODYSEA_GATE_HEADLESS_ENV:-WLR_BACKENDS}"
# The file name the per-run token is written into, inside the private runtime
# directory. app/tests/isolated_compositor_declaration.hpp reads the same name.
readonly nonce_file_name="odysea-isolated-compositor.nonce"

log() { printf 'isolated-compositor-gate: %s\n' "$*" >&2; }

usage() {
    log "usage: isolated_compositor_gate.sh <gate-command> [args...]"
    exit "$harness_failure_status"
}

# True when the named program, or a library it links, contains the given
# environment-variable name as a whole string — the observable trace of code
# that reads it. Absence is treated as proof the variable is not read, which is
# the conservative direction: it produces a refusal, never a run.
compositor_reads_variable() {
    local binary="$1" variable="$2" resolved lib
    resolved="$(command -v "$binary" 2>/dev/null)" || return 1
    [[ -n "$resolved" && -f "$resolved" ]] || return 1
    if strings -a -- "$resolved" 2>/dev/null | grep -qx -- "$variable"; then
        return 0
    fi
    while read -r lib; do
        [[ -f "$lib" ]] || continue
        if strings -a -- "$lib" 2>/dev/null | grep -qx -- "$variable"; then
            return 0
        fi
    done < <(ldd "$resolved" 2>/dev/null | grep -oE '/[^ ]+\.so[^ ]*')
    return 1
}

# Refuses to start a compositor whose headless selection cannot be shown to
# have any effect on the compositor that is actually installed.
#
# WHY THIS IS THE FIRST CHECK. The default command carried WLR_BACKENDS=headless
# and was described as "Hyprland headless". That variable belongs to wlroots.
# The installed Hyprland links aquamarine instead, whose backend selection is a
# different set of names entirely, and the string WLR_BACKENDS appears in
# neither the binary nor that library. So the assignment was inert: the
# compositor would have started with no backend constraint at all, and with
# WAYLAND_DISPLAY deliberately cleared — which is the selector for a nested
# Wayland backend — the remaining plausible choice is the DRM backend, which
# takes a seat, a VT, and DRM master on the machine's own GPU. A harness written
# to protect an interactive session would have taken it over instead.
#
# What follows from that is a rule, not a patch: a declaration of headlessness
# is worth nothing unless it is checked against the thing being run. The harness
# reads the selector out of its own command, confirms the installed compositor
# contains that variable, and refuses otherwise. It never falls through to
# "start it and see", because seeing costs the session.
require_provably_headless_compositor() {
    if [[ -n "${ODYSEA_GATE_STUB_COMPOSITOR:-}" ]]; then
        return 0
    fi
    local words=() word binary="" selector_value="" selector_present=0
    read -r -a words <<<"$compositor_cmd"
    for word in "${words[@]}"; do
        case "$word" in
            env) continue ;;
            *=*)
                if [[ "${word%%=*}" == "$headless_env" ]]; then
                    selector_present=1
                    selector_value="${word#*=}"
                fi
                ;;
            *)
                binary="$word"
                break
                ;;
        esac
    done
    if ((!selector_present)) || [[ -z "$selector_value" ]]; then
        log "refusing: the compositor command does not set $headless_env to a backend, so"
        log "nothing in it selects a headless backend: $compositor_cmd"
        return 1
    fi
    if [[ -z "$binary" ]]; then
        log "refusing: the compositor command names no program: $compositor_cmd"
        return 1
    fi
    if ! compositor_reads_variable "$binary" "$headless_env"; then
        log "refusing: $binary does not read $headless_env, so setting it selects nothing and"
        log "the compositor would start with whatever backend it picks by itself -- on this"
        log "class of machine that is the DRM backend, which takes the seat, the VT, and DRM"
        log "master on the display the session is using."
        log "Install a compositor whose headless backend this harness can verify, or set"
        log "ODYSEA_GATE_COMPOSITOR_CMD and ODYSEA_GATE_HEADLESS_ENV together to one that"
        log "reads its selector. The real-compositor gate stays unmeasured until then, which"
        log "is a smaller cost than the alternative."
        return 1
    fi
    return 0
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

# With XDG_RUNTIME_DIR unset the state directory would fall back to a path under
# /tmp that any local user can create first, including as a symlink pointing the
# harness's own rm -rf somewhere else. There is no run worth taking that risk
# for: a machine with no runtime directory is not the machine this gate is meant
# to run on.
if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
    log "refusing: XDG_RUNTIME_DIR is not set, so the run state would live under a"
    log "world-writable path this harness deletes recursively"
    exit "$harness_failure_status"
fi

# Before the lock, before any process is started: a compositor command whose
# headless selection cannot be shown to apply is refused outright.
require_provably_headless_compositor || exit "$harness_failure_status"

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
# An invariant assertion, and it is worth saying that it is only that: a fresh
# mktemp directory under the run parent can never equal the ambient one, so this
# branch cannot fire and deleting it changes no test result. It used to read as
# the control that stops a gate being pointed back at the inherited session.
# That control is the per-run token below and the checks the gates apply to it;
# a line that cannot fail is not a defence, and describing one as a defence is
# how the interlock kept passing review with a session-shaped hole in it.
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

# The per-run token. A gate accepts a declaration only when the directory
# holding the socket contains this exact value, which is what separates a
# compositor this run created from one that was already listening — and the
# machine's own session is always listening. It is read from the kernel's random
# source per run and written nowhere the declaration itself can reach.
nonce="$(od -An -tx1 -N 32 /dev/urandom | tr -d ' \n')"
if ((${#nonce} < 32)); then
    log "refusing: could not read a per-run token from /dev/urandom"
    exit "$harness_failure_status"
fi
readonly nonce
(
    umask 077
    printf '%s' "$nonce" >"$private_runtime/$nonce_file_name"
)

# A private HOME and configuration tree. Without them the compositor reads the
# real user's configuration, whose startup entries would launch a second copy of
# that session's daemons; with the session bus also removed below, those would
# have competed for the live session's bus names.
private_home="$private_runtime/home"
mkdir -p "$private_home/.config" "$private_home/.cache"

# The compositor creates its socket in its own runtime directory and does not
# read WAYLAND_DISPLAY, which is cleared so it can never bind the ambient name.
# setsid puts it in its own session, so its group can be stopped as a unit and
# it does not share the harness's controlling terminal. The lock descriptors are
# closed for it ({lock_fd}>&- {liveness_fd}>&-): a child that inherited them
# would hold the flock open, so a later SIGKILL of the harness would leave the
# lock held by the surviving orphan instead of released by the kernel, and the
# reaper — which depends on the liveness lock being free once the harness is
# gone — could never run.
#
# The session bus is removed as well. It is an absolute unix path, so a private
# runtime directory does not isolate it, and this compositor's own startup
# routine calls dbus-update-activation-environment — which would have rewritten
# the LIVE session's activation environment to point at a throwaway compositor
# and unset those variables again on exit, leaving residue no teardown here
# could undo. DISPLAY and XAUTHORITY go for the same reason they go inside the
# gates: an inherited X display is a session this harness did not create.
#
# Streams are redirected into the run directory. A survivor that inherited the
# harness's stdout holds that pipe open, and a reader that waits for end-of-file
# — CTest does — then blocks on a process the harness has already stopped
# reporting about.
env -u DBUS_SESSION_BUS_ADDRESS -u DBUS_SESSION_BUS_PID -u DISPLAY -u XAUTHORITY \
    WAYLAND_DISPLAY="" XDG_RUNTIME_DIR="$private_runtime" \
    HOME="$private_home" XDG_CONFIG_HOME="$private_home/.config" \
    XDG_CACHE_HOME="$private_home/.cache" XDG_STATE_HOME="$private_home/.local/state" \
    setsid bash -c "$compositor_cmd" \
    >"$private_runtime/compositor.out" 2>"$private_runtime/compositor.err" \
    {lock_fd}>&- {liveness_fd}>&- &
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
#
# DISPLAY and XAUTHORITY are removed here too. Both current gates unset DISPLAY
# themselves once they accept a declaration, but this harness accepts any
# command, and a Qt fallback onto an inherited X display is one of the holes
# this interlock has already had. The defence belongs at the boundary as well as
# inside the things that happen to pass through it today.
env -u DISPLAY -u XAUTHORITY \
    XDG_RUNTIME_DIR="$private_runtime" \
    WAYLAND_DISPLAY="$wayland_display" \
    ODYSEA_ISOLATED_COMPOSITOR="$wayland_display" \
    ODYSEA_ISOLATED_COMPOSITOR_NONCE="$nonce" \
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

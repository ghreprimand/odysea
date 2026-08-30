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
# and tears the run down.
#
# TEARDOWN IS AN OBLIGATION, NOT A GESTURE. Everything this harness starts is a
# process group it must account for before it exits. The gate is stopped first
# and waited for, because it is the process using the compositor and reading the
# runtime directory that teardown then takes away; the compositor follows; each
# is escalated from SIGTERM to SIGKILL; and a run directory is deleted only once
# every process recorded in it is confirmed gone. What cannot be confirmed is
# kept and named, because the record is the only thing that keeps an orphan
# reachable by a later run, and a deleted record with a live process behind it
# is an orphan holding the GPU and the seat that nothing can find again.
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
#   1      the harness could not start a compositor for the run; or an
#          abandoned earlier run could not be disposed of, so this one does not
#          start beside it; or the gate passed but its own teardown failed,
#          which is not a clean run and is not reported as one. A gate's own
#          non-zero status is never overwritten: it is the more specific answer.
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
#   ODYSEA_GATE_PROC_ROOT       Where process records are read from. Default
#                               /proc. The self-test points it at an empty
#                               directory to measure the refusal that follows
#                               when a start time cannot be read. Any other
#                               value in a real run makes every record
#                               unreadable, which is refused rather than run.
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
# Where process records are read from. Overridable so the self-test can present
# a /proc that answers nothing and measure what the harness does when a start
# time cannot be read. Pointing it anywhere else in a real run makes every
# record unreadable, and this harness treats that as a hard failure rather than
# running with its teardown quietly disabled -- so the knob fails closed.
readonly proc_root="${ODYSEA_GATE_PROC_ROOT:-/proc}"
# How long a process group is given to stop, in tenths of a second: 2.0s for
# SIGTERM, then 3.0s for SIGKILL before it is reported as having survived one.
readonly term_wait_ticks=20
readonly kill_wait_ticks=30
# How long a child whose start time cannot be read is given to answer whether it
# is alive or gone, in tenths of a second. See settle_starttime: this bounds a
# settle, not a comparison, and only a child that is both alive and unreadable
# spends it.
readonly record_settle_ticks=20

log() { printf 'isolated-compositor-gate: %s\n' "$*" >&2; }

usage() {
    log "usage: isolated_compositor_gate.sh <gate-command> [args...]"
    exit "$harness_failure_status"
}

# True when the named program, or a library it links, CONTAINS the given
# environment-variable name as a whole string.
#
# Be exact about what that is worth, because the earlier comment here was not.
# Containing the string is not reading the variable. A mention in a help text,
# a log message, or a comment in an interpreted script satisfies it, and the
# self-test's own positive control is a shell script whose only occurrence sits
# in an unexecuted here-document. What this measures is the presence of the
# name, nothing more.
#
# The direction that carries the weight is the failing one, and it is exact:
# a program that never mentions the name cannot be reading it, so the variable
# selects nothing and the command that sets it is inert. That is the case the
# shipped default was in. Absence is therefore treated as proof the variable is
# unread — conservative, because it produces a refusal and never a run. Presence
# is treated as nothing more than "not provably inert", which is why the
# selector's VALUE is allow-listed separately: the name decides whether the
# variable is read at all, and only the value decides where the compositor
# renders.
#
# Library resolution uses objdump on the ELF headers rather than ldd. ldd is a
# shell script that execs the dynamic loader against the binary, which is more
# than an inspection and more than this check needs; objdump reads the file.
compositor_mentions_variable() {
    local binary="$1" variable="$2" resolved lib soname
    resolved="$(command -v "$binary" 2>/dev/null)" || return 1
    [[ -n "$resolved" && -f "$resolved" ]] || return 1
    if strings -a -- "$resolved" 2>/dev/null | grep -qx -- "$variable"; then
        return 0
    fi
    while read -r soname; do
        [[ -n "$soname" ]] || continue
        for lib in /usr/lib/"$soname" /lib/"$soname" /usr/lib64/"$soname"; do
            [[ -f "$lib" ]] || continue
            if strings -a -- "$lib" 2>/dev/null | grep -qx -- "$variable"; then
                return 0
            fi
        done
    done < <(objdump -p -- "$resolved" 2>/dev/null | awk '$1 == "NEEDED" { print $2 }')
    return 1
}

# The backend values a given selector is permitted to carry. The selector's name
# decides whether the variable is read; only its VALUE decides where the
# compositor renders, and nothing checked the value at all until a measurement
# showed WLR_BACKENDS=drm starting a run and logging RUN. On a genuine wlroots
# compositor drm is precisely the backend that takes the seat, the VT and DRM
# master — the outcome this whole refusal exists to prevent — and it was
# reachable through the documented pair of knobs rather than by sabotage.
#
# So the value is allow-listed per selector. An unknown selector has no known
# safe value and is refused rather than trusted, because a selector this harness
# does not understand is one whose headless spelling it cannot confirm.
headless_value_allowed() {
    local selector="$1" value="$2"
    case "$selector" in
        WLR_BACKENDS)
            # wlroots accepts a comma-separated list; every element must be
            # headless, or a single non-headless entry reintroduces the seat.
            [[ "$value" == "headless" ]]
            ;;
        *)
            return 1
            ;;
    esac
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
    # The stub declaration is deliberately hard to set by accident or in
    # passing. It disables the one control that keeps this harness off the
    # seat, so it is honoured only when the run is also pointed at a state
    # directory that is not the production one — which a real run never is. One
    # exported variable from anywhere else does nothing.
    if [[ -n "${ODYSEA_GATE_STUB_COMPOSITOR:-}" ]]; then
        if [[ -z "${ODYSEA_GATE_STATE_DIR:-}" ]]; then
            log "refusing: ODYSEA_GATE_STUB_COMPOSITOR bypasses the headless proof and is"
            log "honoured only together with a non-default ODYSEA_GATE_STATE_DIR. A run"
            log "against the production state directory is a real run."
            return 1
        fi
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
    if ! headless_value_allowed "$headless_env" "$selector_value"; then
        log "refusing: $headless_env=$selector_value does not select a headless backend."
        log "The variable's name decides only whether the compositor reads it; the VALUE"
        log "decides where it renders, and this one does not name a backend known to render"
        log "nowhere. A value such as drm takes the seat, the VT, and DRM master on the"
        log "display the session is using, which is the outcome this refusal exists for."
        log "A selector this harness does not recognise is refused for the same reason: it"
        log "has no headless spelling that can be confirmed here."
        return 1
    fi
    if ! compositor_mentions_variable "$binary" "$headless_env"; then
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
# apart from a later reuse of the same number. Empty when the pid is gone, and
# equally empty when the record could not be read at all: the two are
# indistinguishable from here, which is why an empty value is never recorded and
# never compared. See require_starttime.
starttime_of() {
    local pid="$1" stat rest
    stat="$(cat "$proc_root/$pid/stat" 2>/dev/null)" || return 0
    # comm (field 2) is wrapped in parentheses and may itself contain spaces and
    # parentheses, so everything up to the last ") " is dropped before the
    # remaining fields are split. starttime is field 22 overall, which is field
    # 20 of what remains once pid and comm are removed.
    rest="${stat##*') '}"
    # shellcheck disable=SC2086
    set -- $rest
    printf '%s' "${20:-}"
}

# What a record says about the process it names, as three answers rather than
# two:
#   0  the recorded process is alive
#   1  it is gone, or the number now names something else
#   2  it cannot be told apart -- the pid is alive but its start time is
#      unreadable, or no start time was recorded with it
#
# The third answer is the one that matters and the one this file used to fold
# into "nothing to do". An unreadable record is not an absent process: acting on
# it means signalling a whole process group on no evidence, and ignoring it
# means walking away from something still running. Neither is acceptable
# silently, so callers get told which case they are in.
#
# Liveness is asked of the kernel (kill -0), not of the record, so a /proc that
# has stopped answering cannot make a live process look gone. The known limit is
# EPERM: a pid this user may not signal reads as gone. Every pid recorded here is
# this harness's own child, so that case does not arise in the runs it covers.
recorded_process_state() {
    local pid="$1" recorded="$2" current
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    ((pid > 1)) || return 1
    [[ -n "$recorded" ]] || return 2
    kill -0 "$pid" 2>/dev/null || return 1
    current="$(starttime_of "$pid")"
    [[ -n "$current" ]] || return 2
    [[ "$current" == "$recorded" ]] || return 1
    return 0
}

# True only when the recorded process is alive and identified. Anything else,
# including a record that cannot be checked, is false -- so nothing is ever
# signalled on the strength of a record this harness cannot verify.
process_matches() {
    local state=0
    recorded_process_state "$1" "$2" || state=$?
    ((state == 0))
}

# Refuses to record a pid whose start time could not be read. A pid on its own
# cannot be told from a later reuse of the same number, so nothing is allowed to
# signal it -- which means teardown, the escalation and every later reap of this
# run all become silent no-ops, and the run directory is deleted anyway, leaving
# nothing that names the process at all. One transient read failure would
# produce a permanent orphan holding the GPU and the seat, with no record of it.
# Refusing to start the run is the smaller cost, and it is loud.
require_starttime() {
    local starttime="$1" what="$2" pid="$3"
    if [[ -z "$starttime" ]]; then
        log "refusing: could not read the start time of the $what (pid $pid) under $proc_root."
        log "Without it this run could not be torn down or reaped afterwards, so it does not run."
        return 1
    fi
    return 0
}

# Settles what an unreadable start time means for a child that was started a
# moment ago, as three answers rather than the two require_starttime gives:
#
#   0  the start time was read; it is printed on stdout and the child is
#      identified for teardown as usual
#   1  the child is alive and cannot be identified, which is the case
#      require_starttime exists for: nothing may be recorded for it and nothing
#      may signal it, so the run does not proceed
#   2  the child has already terminated, so there is no process to tear down
#      and no record is needed for one
#
# The third answer is the one the earlier code did not have, and folding it into
# the first cost real runs. A child that exits in a few milliseconds can be
# reaped by this shell before the read happens -- the reap is asynchronous, and
# a command substitution is one of the places it lands -- and once a child is
# reaped its /proc entry is gone. The run had already succeeded at that point:
# the compositor came up, the gate ran, and its status is still recoverable,
# because a shell keeps the status of a background child it has reaped until it
# is waited for. Refusing there reports a failure for a run that worked, and it
# does so more often the busier the machine is, which makes the outcome depend
# on what else happened to be running.
#
# Liveness is asked of the kernel, not of proc_root, so a /proc that has stopped
# answering cannot make a live child look terminated -- the answer that would
# turn this into a way to walk away from a process still running.
#
# The wait bounds a settle and never a comparison: a child on its way out passes
# through a window where the kernel still answers kill -0 while its /proc entry
# has already stopped being readable, and answering on the first reading would
# put that window back into the result. The loop ends as soon as either question
# is answered, so a child that is already gone costs one reading and a child
# that is alive and identifiable costs none.
settle_starttime() {
    local pid="$1" waited=0 current=""
    while :; do
        current="$(starttime_of "$pid")"
        if [[ -n "$current" ]]; then
            printf '%s' "$current"
            return 0
        fi
        kill -0 "$pid" 2>/dev/null || return 2
        ((waited < record_settle_ticks)) || return 1
        sleep 0.1
        ((waited += 1))
    done
}

# Signals a process group by its leader pid, but only when that pid still names
# the process recorded with it. The start-time comparison closes the window
# where the number has been reused since it was written down, and because the
# signal goes to a whole process group, what it closes is this harness sending
# SIGKILL to an unrelated group in the session it exists to protect.
kill_group_if_matches() {
    local pid="$1" recorded_starttime="$2" signal="$3"
    process_matches "$pid" "$recorded_starttime" || return 0
    # Negative pid targets the whole process group; the compositor is a session
    # leader, so its group id equals its pid.
    kill "-$signal" "-$pid" 2>/dev/null || kill "-$signal" "$pid" 2>/dev/null || true
}

# Waits for a recorded process to disappear, for a bounded number of tenths of a
# second:
#   0  it is gone
#   1  it is still there when the window closes
#   2  it is still there and no longer identifiable when the window closes
#
# The unidentifiable answer is deliberately not raised the moment it is seen. A
# process that is exiting normally passes through a brief window where the
# kernel still answers kill -0 for it while its /proc entry has already stopped
# being readable, so treating the first such reading as a failure fails every
# healthy teardown -- which is exactly what the first version of this did. What
# distinguishes a transient window from a record that has genuinely stopped
# being checkable is that the latter is still the answer when the wait runs out.
wait_for_stop() {
    local pid="$1" starttime="$2" ticks="$3" waited=0 state=0
    while ((waited < ticks)); do
        state=0
        recorded_process_state "$pid" "$starttime" || state=$?
        if ((state == 1)); then
            return 0
        fi
        sleep 0.1
        ((waited += 1))
    done
    if ((state == 2)); then
        return 2
    fi
    return 1
}

# Says what a process that outlasted its own record means. It is not signalled:
# the signal goes to a whole process group, and a group this harness cannot
# identify is one it has no business signalling. It is not forgotten either.
report_unaccountable() {
    local what="$1" pid="$2"
    log "cannot account for the $what (pid $pid): it is still alive and its start time is no"
    log "longer readable under $proc_root, so it can be neither confirmed stopped nor safely"
    log "signalled. It is left running and its record is kept."
}

# Stops a recorded process group and does not return until it is gone or has
# survived SIGKILL. Politeness, then force, then the truth about which happened:
#   0  the process is gone, or the record names nothing live
#   1  the record is unusable, or the process is still alive after SIGKILL
# A caller that is about to delete a record must treat 1 as "do not delete": the
# record is the only thing that keeps the process reachable by a later run.
stop_group() {
    local pid="$1" starttime="$2" what="${3:-process group}"
    [[ -n "$pid" ]] || return 0
    if [[ -z "$starttime" ]]; then
        log "cannot stop the $what: pid $pid was recorded with no start time, so the process"
        log "it names cannot be identified and must not be signalled."
        return 1
    fi
    kill_group_if_matches "$pid" "$starttime" TERM
    if wait_for_stop "$pid" "$starttime" "$term_wait_ticks"; then
        return 0
    fi
    local state=$?
    if ((state == 2)); then
        report_unaccountable "$what" "$pid"
        return 1
    fi
    log "the $what (pid $pid) did not stop for SIGTERM; escalating to SIGKILL"
    kill_group_if_matches "$pid" "$starttime" KILL
    if wait_for_stop "$pid" "$starttime" "$kill_wait_ticks"; then
        return 0
    fi
    state=$?
    if ((state == 2)); then
        report_unaccountable "$what" "$pid"
        return 1
    fi
    log "the $what (pid $pid) is still alive after SIGKILL"
    return 1
}

# Disposes of one abandoned run directory: stops every process group recorded in
# it, and removes the directory only once each one is confirmed gone. Both the
# compositor and the gate are recorded, so an untrappable kill of a harness
# leaves neither behind past the next run.
#
# The record is deleted on no other terms. Removing it while a process it names
# is still alive turns a recoverable orphan -- one holding the GPU, a seat and a
# runtime directory -- into one nothing can find again, and reporting that as a
# reap writes the opposite of what happened into the log someone reads next.
# Only ever called for a directory whose liveness lock this process already
# holds, so no live harness owns it.
reap_run_dir() {
    local dir="$1" pidfile pid recorded_starttime survivors=0
    for pidfile in "$dir/compositor.pid" "$dir/gate.pid"; do
        [[ -f "$pidfile" ]] || continue
        pid=""
        recorded_starttime=""
        read -r pid recorded_starttime <"$pidfile" || true
        [[ -n "${pid:-}" ]] || continue
        if ! stop_group "$pid" "${recorded_starttime:-}" "abandoned $(basename "$pidfile" .pid)"; then
            survivors=$((survivors + 1))
        fi
    done
    if ((survivors > 0)); then
        log "NOT reaping $dir: $survivors recorded process group(s) could not be confirmed"
        log "stopped. The directory is kept, so they stay reachable and a later run can try"
        log "again."
        return 1
    fi
    rm -rf -- "$dir"
    log "reaped an abandoned run directory: $dir"
    return 0
}

# Sweeps for run directories no live harness holds and disposes of them. A
# harness holds its run directory's liveness lock for its whole lifetime; the
# kernel drops that lock when the process ends by any means, SIGKILL included.
# So a directory whose lock this sweep can take is one whose harness is gone,
# and reaping it here bounds the residue an untrappable kill leaves behind to
# the interval until the next run.
#
# Returns non-zero when any abandoned run could not be disposed of, which the
# caller treats as a reason not to start: whatever survived is holding the
# resources this run is about to ask for.
reap_stale_runs() {
    [[ -d "$runs_parent" ]] || return 0
    local dir probe_fd failures=0
    for dir in "$runs_parent"/run.*; do
        [[ -d "$dir" ]] || continue
        [[ -f "$dir/harness.lock" ]] || continue
        exec {probe_fd}>"$dir/harness.lock" || continue
        if flock -n "$probe_fd"; then
            reap_run_dir "$dir" || failures=$((failures + 1))
        fi
        exec {probe_fd}>&-
    done
    ((failures == 0))
}

# State the teardown needs. Set as the run progresses; cleanup tolerates each
# being unset, so it is correct at every point a signal can arrive.
compositor_pid=""
compositor_starttime=""
private_runtime=""
gate_pid=""
gate_starttime=""
cleanup_done=0
teardown_failed=0

cleanup() {
    ((cleanup_done)) && return 0
    cleanup_done=1
    # The gate is stopped first, and teardown waits for it. It is the process
    # using the compositor and reading the runtime directory that the next two
    # steps take away; signalling it and walking away -- which is what this did
    # -- leaves it rendering against a display being torn down, inside a
    # directory being deleted, with nothing said about it. It ran in its own
    # session, so stopping the group takes a launcher's own children with it.
    if [[ -n "$gate_pid" ]]; then
        if ! stop_group "$gate_pid" "$gate_starttime" "gate"; then
            teardown_failed=1
        fi
    fi
    if [[ -n "$compositor_pid" ]]; then
        if ! stop_group "$compositor_pid" "$compositor_starttime" "compositor"; then
            teardown_failed=1
        fi
    fi
    if [[ -n "$private_runtime" ]]; then
        if ((teardown_failed)); then
            log "keeping $private_runtime: it records the process groups this run could not"
            log "stop, and deleting it would leave them running with nothing naming them."
        else
            rm -rf -- "$private_runtime"
        fi
    fi
    return 0
}

# A gate that passed while its own run could not be torn down is not a clean
# run: something this harness started is still alive on the machine. That is a
# harness failure and is reported as one, rather than being hidden behind the
# gate's result. A gate failure is never overwritten -- it is the more specific
# answer of the two.
on_exit() {
    local status=$?
    cleanup
    if ((teardown_failed)) && ((status == 0)); then
        log "the gate exited 0, but its run could not be torn down; reporting a harness failure"
        exit "$harness_failure_status"
    fi
    exit "$status"
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

# Before the lock and before any compositor is started: a compositor command
# whose headless selection cannot be shown to apply is refused outright.
#
# Say "no compositor" rather than "nothing". The check itself runs command -v,
# strings and objdump against the named program, so it does read files and fork
# tools; what it never does is start the compositor or anything that could open
# a display. The earlier wording claimed more than that and would have been read
# as a guarantee it does not make.
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
trap 'on_exit' EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM

# Dispose of anything an earlier untrappable kill abandoned before starting new.
# A run that cannot be disposed of stops this one: whatever survived it is
# holding the compositor resources this run is about to ask for, and starting
# another compositor beside it is how a machine ends up with several.
if ! reap_stale_runs; then
    log "refusing: an abandoned run could not be disposed of (named above). Stop the process"
    log "groups its record lists and remove that directory before running this gate again."
    exit "$harness_failure_status"
fi

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
# uses it to tell a live run from an abandoned one, and the gates use it as the
# one part of a declaration that cannot be produced by writing files: the kernel
# releases it when this process ends by any means, so a gate that finds it held
# knows a harness is alive right now.
exec {liveness_fd}>"$private_runtime/harness.lock"
flock -n -x "$liveness_fd" || {
    log "refusing: could not take the liveness lock on a directory we just created"
    exit "$harness_failure_status"
}

# The per-run token. One of four conditions a gate applies, not a proof on its
# own: the comparison is "the file beside the socket equals the exported value",
# and anyone able to write both halves can satisfy that with any value at all.
# Its job is to make an accidental or hand-typed declaration fail, not to be
# unguessable. What binds a declaration to a live process is the liveness lock
# above; what stops one resolving onto the login session is the directory
# identity check the gates apply. All four are needed and none is sufficient.
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
if ! require_starttime "$compositor_starttime" "compositor" "$compositor_pid"; then
    # The record is unusable, so the ordinary teardown path is not allowed to
    # act on this pid. It was started moments ago by this shell and its number
    # has not been released, so signalling it directly here is sound in a way it
    # never is later; and leaving it is not an option, since it holds a socket
    # inside the directory this exit is about to remove.
    kill -KILL "-$compositor_pid" 2>/dev/null || kill -KILL "$compositor_pid" 2>/dev/null || true
    compositor_pid=""
    exit "$harness_failure_status"
fi
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
#
# The run directory is exported alongside the socket name. A gate compares the
# directory holding the socket against it by device and inode, so no spelling of
# any other directory — and in particular no spelling of the login session's —
# can stand in for it.
env -u DISPLAY -u XAUTHORITY \
    XDG_RUNTIME_DIR="$private_runtime" \
    WAYLAND_DISPLAY="$wayland_display" \
    ODYSEA_ISOLATED_COMPOSITOR="$wayland_display" \
    ODYSEA_ISOLATED_COMPOSITOR_NONCE="$nonce" \
    ODYSEA_ISOLATED_COMPOSITOR_RUNDIR="$private_runtime" \
    setsid "$@" {lock_fd}>&- {liveness_fd}>&- &
gate_wait_pid="$!"
# Recorded for teardown from the moment it exists, so a signal arriving during
# the settle below finds a run that names its own child rather than one that
# does not.
gate_pid="$gate_wait_pid"
gate_record_state=0
gate_starttime="$(settle_starttime "$gate_wait_pid")" || gate_record_state=$?
if ((gate_record_state == 1)); then
    require_starttime "" "gate" "$gate_wait_pid" || true
    # Same reasoning as the compositor above: signalled directly because the pid
    # is still this shell's own child and cannot yet have been reused, and
    # stopped rather than left, because it was about to be handed a compositor.
    kill -KILL "-$gate_wait_pid" 2>/dev/null || kill -KILL "$gate_wait_pid" 2>/dev/null || true
    gate_pid=""
    exit "$harness_failure_status"
fi
if ((gate_record_state == 2)); then
    # The gate finished before its start time could be read. Nothing is recorded
    # for it because there is nothing left to stop, and the status it exited
    # with is still the answer this harness reports: it is asked for below.
    log "the gate (pid $gate_wait_pid) finished before it could be recorded; there is no"
    log "process to tear down and its exit status is still this run's result"
    gate_pid=""
    gate_starttime=""
else
    printf '%s %s\n' "$gate_pid" "$gate_starttime" >"$private_runtime/gate.pid"
fi

set +e
wait "$gate_wait_pid"
gate_status=$?
set -e

exit "$gate_status"

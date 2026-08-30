#!/usr/bin/env bash
# Proves tools/isolated_compositor_gate.sh by mutation, with no real compositor
# running. A stub stands in for the compositor: it binds a genuine AF_UNIX
# socket where a wlroots compositor would, so the harness's own readiness check
# is exercised, and then holds it. Every property the harness exists for is
# checked by observing a specific consequence, not merely an exit status, so a
# harness that quietly dropped the property fails here:
#
#   - the declaration the gate requires (ODYSEA_ISOLATED_COMPOSITOR) names the
#     same socket as WAYLAND_DISPLAY, and the gate is pointed at the private
#     runtime directory, never the ambient;
#   - the gate's own exit status is passed through unchanged;
#   - the cross-run lock makes a second run skip by name rather than proceed,
#     and releases so a later run is not blocked forever;
#   - the trap tears the compositor down on success, on assertion failure, on
#     SIGINT, and on SIGTERM (the signal a CTest timeout sends);
#   - an untrappable SIGKILL leaves the lock released by the kernel, and the
#     residue it does leave is disposed of by the next run's startup reaper.
#
# Each scenario runs in its own state directory. That isolation is what makes
# the scenarios discriminate: a mutation that breaks teardown leaves exactly one
# scenario's residue behind for that scenario's own assertion to catch, instead
# of leaking directories into a shared tree where a later scenario cannot tell
# its own run from an abandoned one.
#
# It starts no compositor of its own and touches nothing outside its sandbox.
#
# THE RESULT DOES NOT DEPEND ON HOW MANY TESTS RUN BESIDE IT. That is a property
# worth stating, because it did not hold: one scenario reported a failure only
# when the suite was scheduled inside a parallel battery, which teaches everyone
# reading the summary to re-run rather than to look, and that is how a real
# failure in the same file gets waved through. What was scheduling-sensitive was
# the harness reading a child's start time, not any budget here; it is settled
# against the child's liveness now rather than against a clock, and scenario 17
# checks both of its answers directly.
#
# The one wall-clock budget left in a passing run is the harness's wait for the
# compositor socket, and it is not close: the stub advertises in 0.10s against a
# 10s budget, measured both serially and inside a four-way parallel battery, so
# the wait would have to take a hundred times longer before the result changed.
# Every other wait here ends on an observable event -- a marker file, a process
# ending, a lock being released -- and reports by name if it does not happen.
set -euo pipefail

readonly script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly harness="$script_directory/isolated_compositor_gate.sh"

if [[ ! -f "$harness" ]]; then
    echo "compositor_gate_harness_self_test: harness script is missing at $harness" >&2
    exit 1
fi

# Stated as preconditions rather than skipped: a self-test that declined to run
# would report nothing while reading as a pass in the summary. The harness needs
# flock and setsid; the stub needs python3 to bind a real socket.
for tool in flock setsid python3 env pgrep; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "compositor_gate_harness_self_test: $tool is required and is not installed" >&2
        exit 1
    fi
done
if ! env --default-signal=TERM true 2>/dev/null; then
    echo "compositor_gate_harness_self_test: env --default-signal is required to deliver a signal to a backgrounded harness and is unavailable" >&2
    exit 1
fi

readonly sandbox="$(mktemp -d)"

# Everything the harness starts is a session leader (own process group), so the
# backstop stops each group by the pid recorded in every run directory, then the
# sandbox is removed last. This runs even if a scenario aborts.
cleanup_sandbox() {
    local pidfile pid rest
    for pidfile in "$sandbox"/state.*/runs/run.*/compositor.pid "$sandbox"/state.*/runs/run.*/gate.pid; do
        [[ -f "$pidfile" ]] || continue
        read -r pid rest <"$pidfile" || true
        [[ "${pid:-}" =~ ^[0-9]+$ ]] && ((pid > 1)) && kill -KILL "-$pid" 2>/dev/null || true
    done
    for pid in ${harness_pids[*]:-}; do
        kill -KILL "$pid" 2>/dev/null || true
    done
    rm -rf -- "$sandbox"
}
harness_pids=()
trap cleanup_sandbox EXIT

# How many scenarios must report a result. A suite that runs a subset of itself
# prints nothing but passes and reads as a clean run -- which is exactly what one
# truncated run of this file did, reporting the first nine scenarios and a
# summary saying all of them passed. The count below is the floor that makes
# that impossible to mistake for a result.
readonly expected_scenario_count=21

failures=0
reported=0
report() {
    local outcome="$1" scenario="$2"
    printf '%-5s %s\n' "$outcome" "$scenario"
    reported=$((reported + 1))
    [[ "$outcome" == PASS ]] || failures=$((failures + 1))
}

# The state directory the current scenario works in. Set by new_state at the top
# of each scenario so no scenario can see another's runs or lock.
active_state=""
new_state() {
    active_state="$sandbox/state.$1"
    mkdir -p "$active_state/runs"
}
lock_path() { printf '%s' "$active_state/lock"; }

# --- Stub compositor and stub gate commands ---------------------------------
# Every scenario below runs a stub that opens a socket and does nothing else, so
# the harness's headless proof — which asks whether the installed compositor
# reads the backend selector in its command — has nothing to decide about. The
# declaration below says so, and it is the only thing it does. The proof itself
# is exercised in both directions by its own scenario, which does not set this.
export ODYSEA_GATE_STUB_COMPOSITOR=1

readonly stub_compositor="$sandbox/stub_compositor.sh"
cat >"$stub_compositor" <<'STUB'
#!/usr/bin/env bash
# Binds a real AF_UNIX socket where a wlroots compositor advertises one, in the
# private runtime directory the harness chose, then holds it. No rendering, no
# window, no ambient session: only the socket the harness waits for.
set -eu
exec python3 -c 'import socket, sys, time
sock = sys.argv[1]
s = socket.socket(socket.AF_UNIX)
s.bind(sock)
s.listen(1)
time.sleep(10 ** 9)' "$XDG_RUNTIME_DIR/wayland-1"
STUB
chmod +x "$stub_compositor"

# Deliberately advertises no socket. The harness must refuse before invoking its
# gate, so a failed compositor start cannot hand a declaration to a command
# pointed at an ambient or stale display.
readonly no_socket_compositor="$sandbox/no_socket_compositor.sh"
cat >"$no_socket_compositor" <<'STUB'
#!/usr/bin/env bash
exec sleep 100000
STUB
chmod +x "$no_socket_compositor"

# Records the environment the harness handed the gate, then exits with a chosen
# status so exit-code pass-through is observable.
readonly gate_record="$sandbox/gate_record.sh"
cat >"$gate_record" <<'GATE'
#!/usr/bin/env bash
out="$1"; code="$2"
{
    printf 'ODYSEA_ISOLATED_COMPOSITOR=%s\n' "${ODYSEA_ISOLATED_COMPOSITOR:-<unset>}"
    printf 'WAYLAND_DISPLAY=%s\n' "${WAYLAND_DISPLAY:-<unset>}"
    printf 'XDG_RUNTIME_DIR=%s\n' "${XDG_RUNTIME_DIR:-<unset>}"
    printf 'ODYSEA_ISOLATED_COMPOSITOR_NONCE=%s\n' "${ODYSEA_ISOLATED_COMPOSITOR_NONCE:-<unset>}"
    # Whether the exported token matches the one in the directory holding the
    # socket is the whole question the gates ask, so the stub gate answers it
    # from where a gate stands rather than from where the harness stands.
    recorded="<absent>"
    if [[ -f "${XDG_RUNTIME_DIR:-}/odysea-isolated-compositor.nonce" ]]; then
        recorded="$(cat "$XDG_RUNTIME_DIR/odysea-isolated-compositor.nonce")"
    fi
    printf 'NONCE_FILE=%s\n' "$recorded"
    printf 'ODYSEA_ISOLATED_COMPOSITOR_RUNDIR=%s\n' "${ODYSEA_ISOLATED_COMPOSITOR_RUNDIR:-<unset>}"
    # The liveness lock is what binds the declaration to a running harness. From
    # the gate's side the accepting answer is that the lock CANNOT be taken.
    lockstate="<absent>"
    if [[ -f "${ODYSEA_ISOLATED_COMPOSITOR_RUNDIR:-}/harness.lock" ]]; then
        if flock -n -x "$ODYSEA_ISOLATED_COMPOSITOR_RUNDIR/harness.lock" true 2>/dev/null; then
            lockstate="free"
        else
            lockstate="held"
        fi
    fi
    printf 'HARNESS_LOCK=%s\n' "$lockstate"
    printf 'DISPLAY=%s\n' "${DISPLAY:-<unset>}"
} >"$out"
exit "$code"
GATE
chmod +x "$gate_record"

# Signals readiness by creating a marker, then blocks so a signal or a competing
# run can be delivered while the gate is "running".
readonly gate_block="$sandbox/gate_block.sh"
cat >"$gate_block" <<'GATE'
#!/usr/bin/env bash
: >"$1"
exec sleep 100000
GATE
chmod +x "$gate_block"

# Survives SIGTERM. Stands in for any gate that installs its own handler, blocks
# in a library call that does not return promptly, or simply takes longer to die
# than the harness waits: the harness must escalate rather than walk away from
# it.
readonly gate_ignores_term="$sandbox/gate_ignores_term.sh"
cat >"$gate_ignores_term" <<'GATE'
#!/usr/bin/env bash
trap '' TERM
: >"$1"
while true; do sleep 0.2; done
GATE
chmod +x "$gate_ignores_term"

# Signals readiness, then exits 0 as soon as a release file appears. Lets a
# scenario change the conditions the harness will tear down under, and then have
# the gate finish normally, so the gate's own result is a clean pass.
readonly gate_until_released="$sandbox/gate_until_released.sh"
cat >"$gate_until_released" <<'GATE'
#!/usr/bin/env bash
: >"$1"
while [[ ! -e "$2" ]]; do sleep 0.1; done
exit 0
GATE
chmod +x "$gate_until_released"

# Answers the ordering question from the one place it can be answered. On
# SIGTERM the gate waits, then tries to CONNECT to the compositor socket it was
# given, and records whether the connection was accepted or refused. The socket
# file outlives the compositor process, so its presence proves nothing; a
# completed connection proves the compositor was still running when the gate was
# stopped, which is the order teardown must use.
readonly gate_probes_compositor="$sandbox/gate_probes_compositor.sh"
cat >"$gate_probes_compositor" <<'GATE'
#!/usr/bin/env bash
result="$1"
marker="$2"
on_term() {
    sleep 1
    python3 -c 'import socket, sys
try:
    connection = socket.socket(socket.AF_UNIX)
    connection.connect(sys.argv[1])
    print("ok")
except OSError:
    print("refused")' "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" >"$result"
    exit 0
}
trap on_term TERM
: >"$marker"
while true; do
    sleep 0.2 &
    wait $! || true
done
GATE
chmod +x "$gate_probes_compositor"

# A compositor stub that ignores SIGTERM, used to abandon a run whose orphan
# cannot be stopped politely. What disposes of it is the reaper's escalation.
readonly stub_compositor_ignores_term="$sandbox/stub_compositor_ignores_term.sh"
cat >"$stub_compositor_ignores_term" <<'STUB'
#!/usr/bin/env bash
set -eu
exec python3 -c 'import signal, socket, sys, time
signal.signal(signal.SIGTERM, signal.SIG_IGN)
sock = sys.argv[1]
s = socket.socket(socket.AF_UNIX)
s.bind(sock)
s.listen(1)
time.sleep(10 ** 9)' "$XDG_RUNTIME_DIR/wayland-1"
STUB
chmod +x "$stub_compositor_ignores_term"

# Waits for a release file inside its own runtime directory before binding its
# socket. That makes the order of two otherwise independent events fixed: the
# harness has recorded the compositor by the time the socket appears, so a
# scenario can change what the harness is able to read in between and know
# exactly which read it changed.
readonly stub_compositor_waits_to_bind="$sandbox/stub_compositor_waits_to_bind.sh"
cat >"$stub_compositor_waits_to_bind" <<'STUB'
#!/usr/bin/env bash
set -eu
while [[ ! -e "$XDG_RUNTIME_DIR/go" ]]; do sleep 0.02; done
exec python3 -c 'import socket, sys, time
sock = sys.argv[1]
s = socket.socket(socket.AF_UNIX)
s.bind(sock)
s.listen(1)
time.sleep(10 ** 9)' "$XDG_RUNTIME_DIR/wayland-1"
STUB
chmod +x "$stub_compositor_waits_to_bind"

# Records its own pid and then blocks. exec keeps the pid, so the number in the
# file is the one the harness started and a scenario can ask what happened to
# it.
#
# It ends by itself rather than running until something stops it. A harness that
# mistook this gate for one that had already finished would wait for it, and a
# gate that never ends turns that mistake into a suite that hangs until a
# timeout kills it -- which says far less than a scenario failing by name. The
# wait is bounded so the wrong answer is reported rather than merely survived;
# the right answer stops it long before, so the bound costs a passing run
# nothing.
readonly gate_records_pid="$sandbox/gate_records_pid.sh"
cat >"$gate_records_pid" <<'GATE'
#!/usr/bin/env bash
printf '%s' "$$" >"$1"
exec sleep 30
GATE
chmod +x "$gate_records_pid"

# Creates a REGULAR FILE where the socket belongs, and nothing else. A readiness
# check that looks for a name rather than for a socket accepts this and hands a
# declaration to a gate naming something no compositor is listening on.
readonly stub_compositor_regular_file="$sandbox/stub_compositor_regular_file.sh"
cat >"$stub_compositor_regular_file" <<'STUB'
#!/usr/bin/env bash
set -eu
: >"$XDG_RUNTIME_DIR/wayland-1"
exec sleep 100000
STUB
chmod +x "$stub_compositor_regular_file"

# Records the WAYLAND_DISPLAY it inherited, and binds its socket only when that
# variable is empty. A compositor started with the ambient name still set is the
# nested-backend case: it would attach to the session this harness exists to
# stay away from.
readonly stub_compositor_records_display="$sandbox/stub_compositor_records_display.sh"
cat >"$stub_compositor_records_display" <<'STUB'
#!/usr/bin/env bash
set -eu
printf '%s' "${WAYLAND_DISPLAY-<unset>}" >"$1"
if [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
    exit 3
fi
exec python3 -c 'import socket, sys, time
sock = sys.argv[1]
s = socket.socket(socket.AF_UNIX)
s.bind(sock)
s.listen(1)
time.sleep(10 ** 9)' "$XDG_RUNTIME_DIR/wayland-1"
STUB
chmod +x "$stub_compositor_records_display"

# --- Small assertion helpers ------------------------------------------------
is_alive() { kill -0 "$1" 2>/dev/null; }

wait_for_file() {
    local path="$1" timeout="${2:-10}"
    local deadline=$((SECONDS + timeout))
    while ((SECONDS < deadline)); do
        [[ -e "$path" ]] && return 0
        sleep 0.05
    done
    return 1
}

wait_until_gone() {
    local path="$1" timeout="${2:-10}"
    local deadline=$((SECONDS + timeout))
    while ((SECONDS < deadline)); do
        [[ -e "$path" ]] || return 0
        sleep 0.05
    done
    return 1
}

wait_pid_dead() {
    local pid="$1" timeout="${2:-10}"
    local deadline=$((SECONDS + timeout))
    while ((SECONDS < deadline)); do
        is_alive "$pid" || return 0
        sleep 0.05
    done
    return 1
}

# True when the current scenario's lock is free: acquires it non-blocking in a
# subshell and lets the subshell's exit release it.
lock_is_free() {
    local lock
    lock="$(lock_path)"
    (
        exec {probe}>"$lock"
        flock -n "$probe"
    )
}

wait_lock_free() {
    local timeout="${1:-10}"
    local deadline=$((SECONDS + timeout))
    while ((SECONDS < deadline)); do
        lock_is_free && return 0
        sleep 0.05
    done
    return 1
}

count_run_dirs() {
    local n=0 d
    for d in "$active_state"/runs/run.*; do
        [[ -d "$d" ]] && n=$((n + 1))
    done
    printf '%s' "$n"
}

single_run_dir() {
    local d
    for d in "$active_state"/runs/run.*; do
        [[ -d "$d" ]] && {
            printf '%s' "$d"
            return 0
        }
    done
    return 1
}

# Runs the harness in the foreground against the current scenario's state, with
# the stub compositor, returning its status without tripping errexit.
run_harness_fg() {
    local status=0
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        "$@" || status=$?
    return "$status"
}

# --- Scenario 1: socket declaration, private runtime, and clean teardown ----
scenario_success_and_env() {
    new_state success
    local envfile="$sandbox/env1.txt" log="$sandbox/log1.txt" status=0
    run_harness_fg bash "$harness" "$gate_record" "$envfile" 0 2>"$log" || status=$?

    if ((status != 0)); then
        report FAIL "clean run exits with the gate's status (expected 0, got $status)"
        return
    fi
    local iso wd xrd
    iso="$(grep '^ODYSEA_ISOLATED_COMPOSITOR=' "$envfile" | cut -d= -f2-)"
    wd="$(grep '^WAYLAND_DISPLAY=' "$envfile" | cut -d= -f2-)"
    xrd="$(grep '^XDG_RUNTIME_DIR=' "$envfile" | cut -d= -f2-)"
    if [[ "$iso" != "$wd" ]]; then
        report FAIL "the declaration did not name the gate's WAYLAND_DISPLAY socket (declaration '$iso', display '$wd')"
        return
    fi
    if [[ "$wd" != "wayland-1" ]]; then
        report FAIL "the gate's WAYLAND_DISPLAY was not the private socket (got '$wd')"
        return
    fi
    if [[ "$xrd" != "$active_state"/runs/run.* ]]; then
        report FAIL "the gate's XDG_RUNTIME_DIR was not a private run directory (got '$xrd')"
        return
    fi
    local nonce_env nonce_file display rundir lockstate
    nonce_env="$(grep '^ODYSEA_ISOLATED_COMPOSITOR_NONCE=' "$envfile" | cut -d= -f2-)"
    nonce_file="$(grep '^NONCE_FILE=' "$envfile" | cut -d= -f2-)"
    display="$(grep '^DISPLAY=' "$envfile" | cut -d= -f2-)"
    rundir="$(grep '^ODYSEA_ISOLATED_COMPOSITOR_RUNDIR=' "$envfile" | cut -d= -f2-)"
    lockstate="$(grep '^HARNESS_LOCK=' "$envfile" | cut -d= -f2-)"
    if [[ "$rundir" != "$xrd" ]]; then
        report FAIL "the exported run directory did not match the gate's runtime directory (run dir '$rundir', runtime '$xrd')"
        return
    fi
    # The accepting answer is that the lock is HELD: a free lock means no live
    # harness owns the directory, which is exactly what a hand-built declaration
    # produces.
    if [[ "$lockstate" != "held" ]]; then
        report FAIL "the harness's liveness lock was not held while the gate ran (state '$lockstate')"
        return
    fi
    if ((${#nonce_env} < 32)); then
        report FAIL "the gate received no usable per-run token (got '$nonce_env')"
        return
    fi
    if [[ "$nonce_env" != "$nonce_file" ]]; then
        report FAIL "the exported token did not match the one beside the socket (exported '$nonce_env', beside the socket '$nonce_file')"
        return
    fi
    if [[ "$display" != "<unset>" ]]; then
        report FAIL "an inherited X display reached the gate (DISPLAY='$display')"
        return
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "a run directory survived a clean run"
        return
    fi
    if ! lock_is_free; then
        report FAIL "the lock was not released after a clean run"
        return
    fi
    report PASS "clean run declares the private socket, points the gate at its runtime, and tears down"
}

# --- Scenario 2: no socket means no declaration reaches the gate ------------
scenario_missing_socket_refuses_gate() {
    new_state missing_socket
    local marker="$sandbox/gate_missing_socket.marker" log="$sandbox/log_missing_socket.txt" status=0
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$no_socket_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=1 \
        bash "$harness" "$gate_block" "$marker" 2>"$log" || status=$?

    local ok=1
    if ((status != 1)); then
        report FAIL "a compositor that advertises no socket should make the harness refuse (expected 1, got $status)"
        ok=0
    fi
    if [[ -e "$marker" ]]; then
        report FAIL "the gate ran even though the compositor never created a socket"
        ok=0
    fi
    if ! grep -q "timed out.*waiting for the compositor socket" "$log"; then
        report FAIL "the missing-socket refusal did not name the missing socket"
        ok=0
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "a missing-socket run left a private directory behind"
        ok=0
    fi
    if ! lock_is_free; then
        report FAIL "a missing-socket run left the cross-run lock held"
        ok=0
    fi
    if ((ok)); then
        report PASS "a compositor that creates no socket is refused before the gate can run"
    fi
}

# --- Scenario 3: an assertion failure is passed through, and still tears down
scenario_failure_passthrough() {
    new_state failure
    local envfile="$sandbox/env2.txt" status=0
    run_harness_fg bash "$harness" "$gate_record" "$envfile" 7 2>/dev/null || status=$?
    if ((status != 7)); then
        report FAIL "a gate that exits 7 should make the harness exit 7 (got $status)"
        return
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "a run directory survived a failing run"
        return
    fi
    report PASS "a failing gate's status is passed through and the run is still torn down"
}

# --- Scenario 4: a second run skips by name; the lock releases afterward -----
scenario_lock_excludes_second_run() {
    new_state lock
    local marker="$sandbox/gate3.marker" logA="$sandbox/log3a.txt"
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        bash "$harness" "$gate_block" "$marker" 2>"$logA" &
    local a_pid=$!
    harness_pids+=("$a_pid")

    if ! wait_for_file "$marker" 10; then
        report FAIL "the holding run never reached its gate"
        kill -TERM "$a_pid" 2>/dev/null || true
        return
    fi
    local run_dir
    run_dir="$(single_run_dir)" || true

    # A second run, while the first holds the lock, must skip by name and never
    # create a run directory of its own.
    local logB="$sandbox/log3b.txt" statusB=0
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_LOCK_WAIT=0 \
        bash "$harness" "$gate_record" "$sandbox/env3.txt" 0 2>"$logB" || statusB=$?

    local ok=1
    if ((statusB != 77)); then
        report FAIL "the second run should skip (77) while the lock is held (got $statusB)"
        ok=0
    elif ! grep -q "SKIP -- another compositor gate run holds" "$logB"; then
        report FAIL "the second run skipped without naming the reason"
        ok=0
    elif (($(count_run_dirs) != 1)); then
        report FAIL "the second run created a run directory instead of declining before setup"
        ok=0
    fi

    # Release the first run and confirm the lock frees so a later run is not
    # blocked forever -- the other half of a lock that is not vacuous.
    kill -TERM "$a_pid" 2>/dev/null || true
    wait "$a_pid" 2>/dev/null || true
    if [[ -n "${run_dir:-}" ]] && ! wait_until_gone "$run_dir" 10; then
        report FAIL "the holding run did not tear down its directory on release"
        ok=0
    fi
    if ! wait_lock_free 10; then
        report FAIL "the lock did not release after the holding run ended"
        ok=0
    fi
    # An `if`, not `((ok)) && report`, so the function returns zero whether the
    # scenario passed or failed; a non-zero return here would trip errexit and
    # abort every later scenario, hiding their result behind this one.
    if ((ok)); then
        report PASS "a second run skips by name while the lock is held, and the lock releases after"
    fi
}

# --- Scenarios 5 and 6: a signal mid-run tears the compositor down -----------
scenario_signal_teardown() {
    local signal="$1" label="$2"
    new_state "signal_$signal"
    local marker="$sandbox/gate_${signal}.marker" log="$sandbox/log_${signal}.txt"
    # A shell sets SIGINT (and SIGQUIT) to ignored in a command it backgrounds,
    # and a signal ignored on entry cannot be trapped, so a plain `&` would make
    # the harness deaf to SIGINT here even though a foreground run receives it
    # normally. --default-signal restores the default disposition so the trap
    # under test is the thing being exercised. It is a no-op for SIGTERM, which
    # a shell does not ignore in a background job.
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        env --default-signal="$signal" bash "$harness" "$gate_block" "$marker" 2>"$log" &
    local h_pid=$!
    harness_pids+=("$h_pid")

    if ! wait_for_file "$marker" 10; then
        report FAIL "$label: the run never reached its gate"
        kill -KILL "$h_pid" 2>/dev/null || true
        return
    fi
    local run_dir stub_pid gate_pid
    run_dir="$(single_run_dir)" || {
        report FAIL "$label: no run directory was created"
        kill -KILL "$h_pid" 2>/dev/null || true
        return
    }
    read -r stub_pid _ <"$run_dir/compositor.pid"
    read -r gate_pid _ <"$run_dir/gate.pid" 2>/dev/null || gate_pid=""

    kill "-$signal" "$h_pid" 2>/dev/null || true
    wait "$h_pid" 2>/dev/null || true

    local ok=1
    if ! wait_until_gone "$run_dir" 10; then
        report FAIL "$label: the run directory survived the signal"
        ok=0
    fi
    if ! wait_pid_dead "$stub_pid" 10; then
        report FAIL "$label: the compositor was left running after the signal"
        ok=0
    fi
    if [[ -n "$gate_pid" ]] && ! wait_pid_dead "$gate_pid" 10; then
        report FAIL "$label: the gate subtree was left running after the signal"
        ok=0
    fi
    if ((ok)); then
        report PASS "$label tears the compositor and gate down"
    fi
}

# --- Scenario 7: SIGKILL releases the lock; the reaper disposes of the residue
scenario_sigkill_and_reaper() {
    new_state sigkill
    local marker="$sandbox/gate6.marker" logA="$sandbox/log6a.txt"
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        bash "$harness" "$gate_block" "$marker" 2>"$logA" &
    local a_pid=$!
    harness_pids+=("$a_pid")

    if ! wait_for_file "$marker" 10; then
        report FAIL "SIGKILL: the run never reached its gate"
        kill -KILL "$a_pid" 2>/dev/null || true
        return
    fi
    local run_dir stub_pid gate_pid
    run_dir="$(single_run_dir)" || {
        report FAIL "SIGKILL: no run directory was created"
        kill -KILL "$a_pid" 2>/dev/null || true
        return
    }
    read -r stub_pid _ <"$run_dir/compositor.pid"
    read -r gate_pid _ <"$run_dir/gate.pid" 2>/dev/null || gate_pid=""

    # The one path a trap cannot cover.
    kill -KILL "$a_pid" 2>/dev/null || true
    wait "$a_pid" 2>/dev/null || true

    local ok=1
    # The kernel drops the flock when the process dies, so the lock is free even
    # though no cleanup ran.
    if ! lock_is_free; then
        report FAIL "SIGKILL: the lock was not released by the kernel"
        ok=0
    fi
    # The residue a kill leaves behind, recorded rather than hidden: the run
    # directory and the orphaned compositor are still present.
    if [[ ! -d "$run_dir" ]]; then
        report FAIL "SIGKILL: expected the run directory to remain (it is the residue the reaper handles)"
        ok=0
    fi
    if ! is_alive "$stub_pid"; then
        report FAIL "SIGKILL: expected the compositor to be orphaned and still alive before the reaper runs"
        ok=0
    fi

    # The next run reaps it on startup: same fixed state directory, a fresh run.
    local status=0
    run_harness_fg bash "$harness" "$gate_record" "$sandbox/env6.txt" 0 2>"$sandbox/log6b.txt" || status=$?
    if ((status != 0)); then
        report FAIL "SIGKILL: the reaping run did not complete cleanly (got $status)"
        ok=0
    fi
    if is_alive "$stub_pid" && ! wait_pid_dead "$stub_pid" 5; then
        report FAIL "SIGKILL: the reaper did not stop the orphaned compositor"
        ok=0
    fi
    if [[ -d "$run_dir" ]]; then
        report FAIL "SIGKILL: the reaper did not remove the abandoned run directory"
        ok=0
    fi
    if [[ -n "$gate_pid" ]] && is_alive "$gate_pid" && ! wait_pid_dead "$gate_pid" 5; then
        report FAIL "SIGKILL: the reaper did not stop the orphaned gate subtree"
        ok=0
    fi
    if ! grep -q "reaped an abandoned run directory" "$sandbox/log6b.txt"; then
        report FAIL "SIGKILL: the reaping run did not report what it reaped"
        ok=0
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "SIGKILL: a run directory survived the reaping run"
        ok=0
    fi
    if ((ok)); then
        report PASS "SIGKILL leaves the lock released by the kernel, and the next run reaps the residue"
    fi
}

# --- Scenario 9: the headless proof, in both directions ----------------------
# The harness's first refusal asks whether the program named in the compositor
# command actually reads the variable that command sets to select a backend. It
# exists because the shipped default set WLR_BACKENDS for a compositor that does
# not read it: the assignment was inert, and what would have started was a
# compositor free to take the seat, the VT, and DRM master on the display in
# use. So the check is measured with two commands that differ in exactly that
# one respect — one program that contains the selector and one that does not —
# and neither of them declares itself a stub.
scenario_headless_proof() {
    new_state headless_proof
    local reads_selector="$sandbox/reads_selector.sh" log="$sandbox/log_headless.txt"
    # Stands in for a compositor that reads the selector: the string is present
    # in the program, which is the trace the harness looks for. It then behaves
    # like the socket stub so the run can proceed past the proof.
    # The selector appears as a standalone string, which is how a compiled
    # program that calls getenv on it appears to `strings`: a NUL-terminated
    # literal on a line of its own. A mention inside a longer line is not the
    # same trace and deliberately does not satisfy the proof.
    cat >"$reads_selector" <<'STUB'
#!/usr/bin/env bash
: <<'SELECTOR'
WLR_BACKENDS
SELECTOR
set -eu
exec python3 -c 'import socket, sys, time
sock = sys.argv[1]
s = socket.socket(socket.AF_UNIX)
s.bind(sock)
s.listen(1)
time.sleep(10 ** 9)' "$XDG_RUNTIME_DIR/wayland-1"
STUB
    chmod +x "$reads_selector"

    local status=0
    env -u ODYSEA_GATE_STUB_COMPOSITOR \
        ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="env WLR_BACKENDS=headless /bin/true" \
        ODYSEA_GATE_READY_TIMEOUT=5 \
        bash "$harness" /bin/true >/dev/null 2>"$log" || status=$?
    if ((status == 0)); then
        report FAIL "the harness started a compositor whose backend selector it does not read"
        return
    fi
    if ! grep -q "does not read WLR_BACKENDS" "$log"; then
        report FAIL "the refusal did not name the selector the program ignores"
        return
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "a refused compositor command still created a run directory"
        return
    fi

    new_state headless_proof_positive
    local envfile="$sandbox/env_headless.txt"
    status=0
    env -u ODYSEA_GATE_STUB_COMPOSITOR \
        ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="env WLR_BACKENDS=headless $reads_selector" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        bash "$harness" "$gate_record" "$envfile" 0 2>>"$log" || status=$?
    if ((status != 0)); then
        report FAIL "a compositor command whose program reads the selector was refused too (status $status)"
        return
    fi
    report PASS "the headless proof refuses an inert selector and admits one the program reads"
}

# --- Scenario 10: the selector's VALUE decides where the compositor renders ---
# The name proves only that the compositor reads the variable. Nothing checked
# the value at all until a measurement showed WLR_BACKENDS=drm reaching "RUN --
# compositor ready" and exiting 0 against the shipped harness. On a genuine
# wlroots compositor drm is the backend that takes the seat, the VT and DRM
# master, so the check that was meant to keep this harness off the session
# admitted the exact command that would have taken it — through the documented
# pair of knobs, not by sabotage.
#
# Both directions, against a program that DOES mention the selector, so the
# earlier check cannot be what refuses and only the value can be.
scenario_headless_value_allowlist() {
    new_state headless_value
    local mentions="$sandbox/mentions_selector.sh" log="$sandbox/log_value.txt"
    cat >"$mentions" <<'STUB'
#!/usr/bin/env bash
: <<'SELECTOR'
WLR_BACKENDS
SELECTOR
set -eu
exec python3 -c 'import socket, sys, time
sock = sys.argv[1]
s = socket.socket(socket.AF_UNIX)
s.bind(sock)
s.listen(1)
time.sleep(10 ** 9)' "$XDG_RUNTIME_DIR/wayland-1"
STUB
    chmod +x "$mentions"

    local status=0
    env -u ODYSEA_GATE_STUB_COMPOSITOR \
        ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="env WLR_BACKENDS=drm $mentions" \
        ODYSEA_GATE_READY_TIMEOUT=5 \
        bash "$harness" /bin/true >/dev/null 2>"$log" || status=$?
    if ((status == 0)); then
        report FAIL "the harness started a compositor asked for the drm backend"
        return
    fi
    if ! grep -q "does not select a headless backend" "$log"; then
        report FAIL "the refusal did not name the value as the reason"
        return
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "a value-refused command still created a run directory"
        return
    fi

    # An unrecognised selector has no confirmable headless spelling and is
    # refused rather than trusted.
    new_state headless_value_unknown
    status=0
    env -u ODYSEA_GATE_STUB_COMPOSITOR \
        ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_HEADLESS_ENV=SOME_OTHER_BACKEND \
        ODYSEA_GATE_COMPOSITOR_CMD="env SOME_OTHER_BACKEND=headless $mentions" \
        ODYSEA_GATE_READY_TIMEOUT=5 \
        bash "$harness" /bin/true >/dev/null 2>>"$log" || status=$?
    if ((status == 0)); then
        report FAIL "the harness trusted a selector it does not recognise"
        return
    fi

    new_state headless_value_positive
    local envfile="$sandbox/env_value.txt"
    status=0
    env -u ODYSEA_GATE_STUB_COMPOSITOR \
        ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="env WLR_BACKENDS=headless $mentions" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        bash "$harness" "$gate_record" "$envfile" 0 2>>"$log" || status=$?
    if ((status != 0)); then
        report FAIL "the permitted headless value was refused as well (status $status)"
        return
    fi
    report PASS "the selector's value is allow-listed: drm and unknown selectors refused, headless admitted"
}

# --- Scenario 11: the stub bypass is not an unqualified environment switch ----
# ODYSEA_GATE_STUB_COMPOSITOR disables the only control keeping this harness off
# the seat. One exported variable from anywhere must not be enough, so it is
# honoured only alongside a state directory that is not the production one.
scenario_stub_bypass_is_qualified() {
    local log="$sandbox/log_stub.txt" status=0
    env -u ODYSEA_GATE_STATE_DIR ODYSEA_GATE_STUB_COMPOSITOR=1 \
        ODYSEA_GATE_COMPOSITOR_CMD="env WLR_BACKENDS=drm /bin/true" \
        bash "$harness" /bin/true >/dev/null 2>"$log" || status=$?
    if ((status == 0)); then
        report FAIL "an exported stub declaration alone disabled the headless proof"
        return
    fi
    if ! grep -q "non-default ODYSEA_GATE_STATE_DIR" "$log"; then
        report FAIL "the refusal did not name the state directory as the missing half"
        return
    fi
    report PASS "the stub bypass is honoured only with a non-production state directory"
}

# --- Scenario 8: a bare invocation with no gate command is refused -----------
scenario_requires_a_gate_command() {
    new_state usage
    local status=0
    ODYSEA_GATE_STATE_DIR="$active_state" bash "$harness" 2>/dev/null || status=$?
    if ((status == 0)); then
        report FAIL "the harness ran with no gate command"
        return
    fi
    report PASS "the harness refuses an invocation with no gate command"
}

# --- Helpers for the teardown and reaper scenarios ---------------------------
# A live process in its own session, so a group signal aimed at it cannot reach
# this self-test, and its pid can be recorded the way the harness records one.
spawn_detached_blocker() {
    local pidfile="$1"
    rm -f -- "$pidfile"
    # Streams go to /dev/null: this function is called inside a command
    # substitution, and a detached process holding that pipe's write end open
    # blocks the caller's read on it forever. The first version of this helper
    # did exactly that and hung the suite.
    setsid bash -c 'printf "%s" "$$" >"$1"; exec sleep 100000' _ "$pidfile" >/dev/null 2>&1 &
    wait_for_file "$pidfile" 10 || return 1
    local pid
    read -r pid <"$pidfile"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    printf '%s' "$pid"
}

# proc(5) field 22 of a live process, read the same way the harness reads it.
starttime_field() {
    local stat rest
    stat="$(cat "/proc/$1/stat" 2>/dev/null)" || return 1
    rest="${stat##*') '}"
    # shellcheck disable=SC2086
    set -- $rest
    printf '%s' "${20:-}"
}

# Plants a run directory that looks abandoned: a liveness lock no process holds,
# and one recorded process group. Whatever the reaper does with it is then
# observable against a process the scenario controls.
plant_abandoned_run() {
    local record="$1" dir="$active_state/runs/run.planted"
    mkdir -p "$dir"
    : >"$dir/harness.lock"
    printf '%s\n' "$record" >"$dir/compositor.pid"
    printf '%s' "$dir"
}

# True when no live process names this scenario's run tree in its command line.
# The stub compositor is started with the socket path as an argument, so a
# survivor of a refused run is visible this way even after its record is gone.
no_process_mentions_state() {
    ! pgrep -f -- "$active_state/runs" >/dev/null 2>&1
}

kill_group_hard() {
    local pid="${1:-}"
    [[ "$pid" =~ ^[0-9]+$ ]] || return 0
    ((pid > 1)) || return 0
    kill -KILL "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
}

# --- Scenario 18: teardown that cannot account for a process says so ---------
# The record can stop being checkable while the run is in progress, not only at
# the start: /proc is the only thing that identifies a pid, and a pid that
# cannot be identified must be neither signalled nor forgotten. What the harness
# owes in that case is the truth -- keep the record, name it, and do not report
# the run as clean, even though the gate itself passed.
scenario_unaccountable_process_is_reported() {
    new_state unaccountable
    local proc_root_link="$sandbox/proc_root_link" empty_proc="$sandbox/empty_proc_late"
    mkdir -p "$empty_proc"
    ln -sfn /proc "$proc_root_link"
    local marker="$sandbox/unaccountable.marker" release="$sandbox/unaccountable.release"
    local log="$sandbox/log_unaccountable.txt"
    rm -f -- "$marker" "$release"
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        ODYSEA_GATE_PROC_ROOT="$proc_root_link" \
        bash "$harness" "$gate_until_released" "$marker" "$release" 2>"$log" &
    local h_pid=$!
    harness_pids+=("$h_pid")

    if ! wait_for_file "$marker" 10; then
        report FAIL "unaccountable process: the run never reached its gate"
        kill -KILL "$h_pid" 2>/dev/null || true
        return
    fi
    local run_dir stub_pid
    run_dir="$(single_run_dir)" || {
        report FAIL "unaccountable process: no run directory was created"
        kill -KILL "$h_pid" 2>/dev/null || true
        return
    }
    read -r stub_pid _ <"$run_dir/compositor.pid"

    # From here on every record reads as unreadable, while the processes they
    # name are still running. The gate is then released and exits 0, so the only
    # thing wrong with this run is that its teardown cannot account for the
    # compositor.
    ln -sfn "$empty_proc" "$proc_root_link"
    : >"$release"
    local status=0
    wait "$h_pid" 2>/dev/null || status=$?

    local ok=1
    if ((status == 0)); then
        report FAIL "unaccountable process: a gate that passed hid a teardown that did not happen"
        ok=0
    fi
    if [[ ! -d "$run_dir" ]]; then
        report FAIL "unaccountable process: the record was deleted with the process it names still running"
        ok=0
    fi
    if ! grep -q "$run_dir" "$log"; then
        report FAIL "unaccountable process: the run directory it kept was not named"
        ok=0
    fi
    if ! is_alive "$stub_pid"; then
        report FAIL "unaccountable process: an unverifiable process group was signalled anyway"
        ok=0
    fi
    # This scenario deliberately leaves a live process and its record behind;
    # dispose of both here rather than leaving them for the sandbox teardown.
    kill_group_hard "$stub_pid"
    ln -sfn /proc "$proc_root_link"
    rm -rf -- "$run_dir"
    if ((ok)); then
        report PASS "a process teardown cannot account for is kept, named, and not reported as a clean run"
    fi
}

# --- Scenario 10: the gate is stopped before the compositor it is using ------
# Teardown order is not cosmetic. Stopping the compositor first pulls the
# display out from under a gate that is still rendering against it, and then
# deletes the runtime directory it is still reading. The gate answers from its
# own side: on SIGTERM it waits, connects to the socket it was handed, and
# records whether a compositor was still listening.
scenario_gate_stopped_before_compositor() {
    new_state teardown_order
    local marker="$sandbox/order.marker" result="$sandbox/order.result" log="$sandbox/log_order.txt"
    rm -f -- "$marker" "$result"
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        bash "$harness" "$gate_probes_compositor" "$result" "$marker" 2>"$log" &
    local h_pid=$!
    harness_pids+=("$h_pid")

    if ! wait_for_file "$marker" 10; then
        report FAIL "teardown order: the run never reached its gate"
        kill -KILL "$h_pid" 2>/dev/null || true
        return
    fi
    local run_dir stub_pid gate_pid
    run_dir="$(single_run_dir)" || {
        report FAIL "teardown order: no run directory was created"
        kill -KILL "$h_pid" 2>/dev/null || true
        return
    }
    read -r stub_pid _ <"$run_dir/compositor.pid"
    read -r gate_pid _ <"$run_dir/gate.pid" 2>/dev/null || gate_pid=""

    kill -TERM "$h_pid" 2>/dev/null || true
    wait "$h_pid" 2>/dev/null || true

    local ok=1
    if ! wait_for_file "$result" 10; then
        report FAIL "teardown order: the gate was never told to stop, so it recorded nothing"
        ok=0
    else
        local observed
        observed="$(cat "$result")"
        if [[ "$observed" != "ok" ]]; then
            report FAIL "teardown order: the compositor was already gone when the gate was stopped (connection '$observed')"
            ok=0
        fi
    fi
    if ! wait_until_gone "$run_dir" 10; then
        report FAIL "teardown order: the run directory survived"
        ok=0
    fi
    if ! wait_pid_dead "$stub_pid" 10; then
        report FAIL "teardown order: the compositor was left running"
        ok=0
    fi
    kill_group_hard "$gate_pid"
    if ((ok)); then
        report PASS "teardown stops the gate while its compositor is still up, then stops the compositor"
    fi
}

# --- Scenario 11: a gate that ignores SIGTERM is escalated, not abandoned ----
# The harness deletes the runtime directory the gate is using as part of
# teardown. A polite signal it never waits for means the gate can still be
# running, on a compositor being stopped, in a directory being removed, with
# nothing said about it.
scenario_gate_escalated_to_kill() {
    new_state teardown_escalation
    local marker="$sandbox/escalate.marker" log="$sandbox/log_escalate.txt"
    rm -f -- "$marker"
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        bash "$harness" "$gate_ignores_term" "$marker" 2>"$log" &
    local h_pid=$!
    harness_pids+=("$h_pid")

    if ! wait_for_file "$marker" 10; then
        report FAIL "gate escalation: the run never reached its gate"
        kill -KILL "$h_pid" 2>/dev/null || true
        return
    fi
    local run_dir stub_pid gate_pid
    run_dir="$(single_run_dir)" || {
        report FAIL "gate escalation: no run directory was created"
        kill -KILL "$h_pid" 2>/dev/null || true
        return
    }
    read -r stub_pid _ <"$run_dir/compositor.pid"
    read -r gate_pid _ <"$run_dir/gate.pid"

    kill -TERM "$h_pid" 2>/dev/null || true
    wait "$h_pid" 2>/dev/null || true

    local ok=1
    if ! wait_pid_dead "$gate_pid" 15; then
        report FAIL "gate escalation: a gate that ignores SIGTERM outlived the harness"
        ok=0
    fi
    if ! wait_pid_dead "$stub_pid" 10; then
        report FAIL "gate escalation: the compositor was left running"
        ok=0
    fi
    if ! wait_until_gone "$run_dir" 10; then
        report FAIL "gate escalation: the run directory survived"
        ok=0
    fi
    kill_group_hard "$gate_pid"
    if ((ok)); then
        report PASS "a gate that ignores SIGTERM is escalated to SIGKILL before the run directory goes"
    fi
}

# --- Scenario 12: the reaper escalates to SIGKILL ---------------------------
# The residue of an untrappable kill is disposed of by the next run. A polite
# signal is not disposal: a compositor that ignores it holds the GPU, the seat
# and its runtime directory, and deleting the record makes it unreachable.
scenario_reaper_escalates_to_kill() {
    new_state reaper_escalation
    local marker="$sandbox/reap_kill.marker" log="$sandbox/log_reap_kill_a.txt"
    rm -f -- "$marker"
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor_ignores_term" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        bash "$harness" "$gate_block" "$marker" 2>"$log" &
    local a_pid=$!
    harness_pids+=("$a_pid")

    if ! wait_for_file "$marker" 10; then
        report FAIL "reaper escalation: the run never reached its gate"
        kill -KILL "$a_pid" 2>/dev/null || true
        return
    fi
    local run_dir stub_pid
    run_dir="$(single_run_dir)" || {
        report FAIL "reaper escalation: no run directory was created"
        kill -KILL "$a_pid" 2>/dev/null || true
        return
    }
    read -r stub_pid _ <"$run_dir/compositor.pid"

    kill -KILL "$a_pid" 2>/dev/null || true
    wait "$a_pid" 2>/dev/null || true

    local ok=1
    if ! is_alive "$stub_pid"; then
        report FAIL "reaper escalation: the orphan was expected to survive the kill of its harness"
        ok=0
    fi

    local status=0 log_b="$sandbox/log_reap_kill_b.txt"
    run_harness_fg bash "$harness" "$gate_record" "$sandbox/env_reap_kill.txt" 0 2>"$log_b" || status=$?
    if ((status != 0)); then
        report FAIL "reaper escalation: the reaping run did not complete cleanly (got $status)"
        ok=0
    fi
    if ! wait_pid_dead "$stub_pid" 10; then
        report FAIL "reaper escalation: an orphaned compositor that ignores SIGTERM survived the reaper"
        ok=0
    fi
    if [[ -d "$run_dir" ]]; then
        report FAIL "reaper escalation: the abandoned run directory was not removed"
        ok=0
    fi
    kill_group_hard "$stub_pid"
    if ((ok)); then
        report PASS "the reaper escalates to SIGKILL and only then disposes of the record"
    fi
}

# --- Scenario 13: a reap that did not happen is never reported as one --------
# The record is the only thing that makes an orphan reachable by a later run.
# Deleting it while the process it names is still unaccounted for turns a
# recoverable orphan into a permanent one, and reporting that as a reap puts the
# opposite of what happened into the log a person reads afterwards.
scenario_unverified_reap_is_refused() {
    new_state reaper_honesty
    local blocker_pid
    blocker_pid="$(spawn_detached_blocker "$sandbox/blocker_honesty.pid")" || {
        report FAIL "unverified reap: could not start the process that stands in for an orphan"
        return
    }
    # A record with no start time: exactly what one transient /proc read failure
    # used to write, and the value against which no comparison can ever match.
    local dir
    dir="$(plant_abandoned_run "$blocker_pid")"

    local status=0 log="$sandbox/log_reap_honesty.txt"
    run_harness_fg bash "$harness" "$gate_record" "$sandbox/env_reap_honesty.txt" 0 2>"$log" || status=$?

    local ok=1
    if ((status == 0)); then
        report FAIL "unverified reap: the harness proceeded past a record it could not act on"
        ok=0
    fi
    if grep -q "reaped an abandoned run directory" "$log"; then
        report FAIL "unverified reap: the log claims a reap that did not happen"
        ok=0
    fi
    if [[ ! -d "$dir" ]]; then
        report FAIL "unverified reap: the record was deleted, leaving the process it named unreachable"
        ok=0
    fi
    if ! is_alive "$blocker_pid"; then
        report FAIL "unverified reap: the process was stopped even though its record could not identify it"
        ok=0
    fi
    if ! grep -q "$dir" "$log"; then
        report FAIL "unverified reap: the refusal did not name the run directory it kept"
        ok=0
    fi
    kill_group_hard "$blocker_pid"
    if ((ok)); then
        report PASS "a record the reaper cannot act on is kept, named, and never reported as reaped"
    fi
}

# --- Scenario 14: an unreadable start time is a hard failure at record time --
# starttime_of returns empty when /proc cannot be read. An empty value recorded
# beside a pid can never match anything, which silently disables cleanup, the
# escalation, and every later reap of that run -- and the run directory is then
# deleted, so nothing is left to find the process by.
scenario_unreadable_starttime_refuses() {
    new_state starttime_failure
    local empty_proc="$sandbox/empty_proc"
    mkdir -p "$empty_proc"
    local status=0 log="$sandbox/log_starttime.txt" envfile="$sandbox/env_starttime.txt"
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        ODYSEA_GATE_PROC_ROOT="$empty_proc" \
        bash "$harness" "$gate_record" "$envfile" 0 2>"$log" || status=$?

    local ok=1
    if ((status == 0)); then
        report FAIL "unreadable start time: the harness ran a gate it would not have been able to tear down"
        ok=0
    fi
    if ! grep -qi "start time" "$log"; then
        report FAIL "unreadable start time: the refusal did not name what could not be read"
        ok=0
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "unreadable start time: a run directory was left behind"
        ok=0
    fi
    local waited=0
    while ((waited < 100)); do
        no_process_mentions_state && break
        sleep 0.05
        ((waited += 1))
    done
    if ! no_process_mentions_state; then
        report FAIL "unreadable start time: the compositor it had already started was left running"
        pkill -KILL -f -- "$active_state/runs" 2>/dev/null || true
        ok=0
    fi
    if ! lock_is_free; then
        report FAIL "unreadable start time: the cross-run lock was left held"
        ok=0
    fi
    if ((ok)); then
        report PASS "a start time that cannot be read stops the run instead of disabling every teardown path"
    fi
}

# Opens a hole in what the harness can read, positioned so that it covers the
# gate and nothing else. The compositor is recorded before its socket exists, so
# waiting for that record and only then replacing the process-record root gives
# a root that answers for the compositor -- from a snapshot of its real entry,
# so the recorded start time still matches -- and answers for nothing else. The
# release file is written last, so the compositor cannot advertise its socket,
# and the harness cannot reach the gate, until the hole is open.
#
# Nothing here is timed against the machine: each step waits for the step before
# it to have happened.
open_gate_record_hole() {
    local link="$1" fake="$2" done_marker="$3"
    local dir="" pid="" rest="" waited=0
    while ((waited < 500)); do
        dir="$(single_run_dir 2>/dev/null)" || dir=""
        if [[ -n "$dir" && -f "$dir/compositor.pid" ]]; then
            pid=""
            rest=""
            read -r pid rest <"$dir/compositor.pid" || true
            if [[ "${pid:-}" =~ ^[0-9]+$ ]]; then
                mkdir -p "$fake/$pid"
                cat "/proc/$pid/stat" >"$fake/$pid/stat" 2>/dev/null || true
                ln -sfn "$fake" "$link"
                : >"$done_marker"
                : >"$dir/go"
                return 0
            fi
        fi
        sleep 0.02
        ((waited += 1))
    done
    return 1
}

# --- Scenario 17: a gate that has finished is not an unidentifiable one ------
# The harness reads a child's start time straight after starting it, and an
# empty reading used to mean one thing: refuse, because a pid that cannot be
# identified must never be signalled and a run that cannot be torn down must not
# start. That is right for a child that is still running and wrong for one that
# has already finished, and the two were indistinguishable.
#
# The difference is not academic. A gate that exits in a few milliseconds can be
# reaped by the harness's own shell before the reading happens, which removes
# its /proc entry; the run had already done everything it exists to do, and was
# reported as a failure. Measured against a parallel test battery it happened in
# 5 of 400 runs, and never once on an idle machine, so the harness's answer
# depended on what else the machine was doing.
#
# Both directions are checked here, because the fix is a distinction rather than
# a relaxation: a finished gate is a completed run, and a gate that is alive and
# cannot be identified is still refused, still stopped, and still leaves nothing
# behind.
scenario_finished_gate_is_not_unidentifiable() {
    new_state gate_finished
    local link="$sandbox/proc_link_finished" fake="$sandbox/fake_proc_finished"
    local opened="$sandbox/hole_opened_finished"
    local envfile="$sandbox/env_gate_finished.txt" log="$sandbox/log_gate_finished.txt"
    rm -rf -- "$fake" "$link" "$opened" "$envfile"
    mkdir -p "$fake"
    ln -sfn /proc "$link"

    open_gate_record_hole "$link" "$fake" "$opened" &
    local opener_pid=$!
    local status=0
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor_waits_to_bind" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        ODYSEA_GATE_PROC_ROOT="$link" \
        bash "$harness" "$gate_record" "$envfile" 0 2>"$log" || status=$?
    wait "$opener_pid" || true

    local ok=1
    if [[ ! -e "$opened" ]]; then
        report FAIL "finished gate: the record hole was never opened, so nothing was measured"
        return
    fi
    if ((status != 0)); then
        report FAIL "finished gate: a gate that had already exited 0 was reported as a failed run (got $status)"
        ok=0
    fi
    if [[ ! -e "$envfile" ]]; then
        report FAIL "finished gate: the gate never ran"
        ok=0
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "finished gate: the run directory was kept for a process that had already ended"
        ok=0
    fi
    if ! lock_is_free; then
        report FAIL "finished gate: the cross-run lock was left held"
        ok=0
    fi

    # The other direction, through the same hole: a gate that is still running
    # cannot be identified, so it is refused and stopped rather than run.
    new_state gate_alive
    local link2="$sandbox/proc_link_alive" fake2="$sandbox/fake_proc_alive"
    local opened2="$sandbox/hole_opened_alive" gate_pid_file="$sandbox/gate_alive.pid"
    local log2="$sandbox/log_gate_alive.txt"
    rm -rf -- "$fake2" "$link2" "$opened2" "$gate_pid_file"
    mkdir -p "$fake2"
    ln -sfn /proc "$link2"

    open_gate_record_hole "$link2" "$fake2" "$opened2" &
    opener_pid=$!
    local status2=0
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor_waits_to_bind" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        ODYSEA_GATE_PROC_ROOT="$link2" \
        bash "$harness" "$gate_records_pid" "$gate_pid_file" 2>"$log2" || status2=$?
    wait "$opener_pid" || true

    if [[ ! -e "$opened2" ]]; then
        report FAIL "unidentifiable gate: the record hole was never opened, so nothing was measured"
        return
    fi
    if ((status2 == 0)); then
        report FAIL "unidentifiable gate: a gate that could not be identified was reported as a clean run"
        ok=0
    fi
    if ! grep -q "could not read the start time of the gate" "$log2"; then
        report FAIL "unidentifiable gate: the refusal did not name the gate as what could not be read"
        ok=0
    fi
    local running_gate=""
    if [[ -f "$gate_pid_file" ]]; then
        read -r running_gate <"$gate_pid_file" || true
    fi
    if [[ "${running_gate:-}" =~ ^[0-9]+$ ]]; then
        if ! wait_pid_dead "$running_gate" 10; then
            report FAIL "unidentifiable gate: the gate it refused to record was left running"
            kill_group_hard "$running_gate"
            ok=0
        fi
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "unidentifiable gate: a refused run left a private directory behind"
        ok=0
    fi
    if ((ok)); then
        report PASS "a gate that has finished is a completed run; one that is alive and unidentifiable is still refused"
    fi
}

# --- Scenario 15: the reaper spares a pid that was reused --------------------
# The reaper signals a NEGATIVE pid, which is a whole process group. Between
# that and an unrelated group in the live session there is one thing: the
# recorded start time. Without it, a run directory left over from a previous
# boot names whichever process now holds that number.
scenario_reaper_spares_a_reused_pid() {
    new_state reaper_reuse
    local blocker_pid
    blocker_pid="$(spawn_detached_blocker "$sandbox/blocker_reuse.pid")" || {
        report FAIL "reused pid: could not start the process that stands in for an unrelated one"
        return
    }
    local real_starttime
    real_starttime="$(starttime_field "$blocker_pid")" || real_starttime=""
    if [[ -z "$real_starttime" ]]; then
        report FAIL "reused pid: could not read the start time of the process under test"
        kill_group_hard "$blocker_pid"
        return
    fi
    # The recorded start time belongs to a process that is gone; the number now
    # names something else. 1 is a start time no live process can have, since
    # the boot-time process that could is init.
    local dir
    dir="$(plant_abandoned_run "$blocker_pid 1")"

    local status=0 log="$sandbox/log_reuse.txt"
    run_harness_fg bash "$harness" "$gate_record" "$sandbox/env_reuse.txt" 0 2>"$log" || status=$?

    local ok=1
    if ((status != 0)); then
        report FAIL "reused pid: the run did not complete cleanly (got $status)"
        ok=0
    fi
    if ! is_alive "$blocker_pid"; then
        report FAIL "reused pid: the reaper signalled a process group that was not the one recorded"
        ok=0
    fi
    if [[ -d "$dir" ]]; then
        report FAIL "reused pid: a record naming nothing live was kept instead of reaped"
        ok=0
    fi
    kill_group_hard "$blocker_pid"
    if ((ok)); then
        report PASS "a recorded pid whose start time no longer matches is spared, and its record is reaped"
    fi
}

# --- Scenario 16: the compositor inherits no ambient display name ------------
# WAYLAND_DISPLAY is the selector for a nested Wayland backend. A compositor
# started with the ambient name still set can attach to the session this harness
# exists to stay away from, and the private runtime directory does not prevent
# it, because the name resolves against the parent's directory first.
scenario_compositor_gets_no_ambient_display() {
    new_state ambient_display
    local record="$sandbox/inherited_display.txt" status=0 log="$sandbox/log_ambient_display.txt"
    rm -f -- "$record"
    WAYLAND_DISPLAY="wayland-ambient-selftest" \
        ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor_records_display $record" \
        ODYSEA_GATE_READY_TIMEOUT=10 \
        bash "$harness" "$gate_record" "$sandbox/env_ambient_display.txt" 0 2>"$log" || status=$?

    local ok=1
    if ((status != 0)); then
        report FAIL "ambient display: the run did not complete (got $status); the compositor refused the display it inherited"
        ok=0
    fi
    if [[ ! -f "$record" ]]; then
        report FAIL "ambient display: the compositor recorded nothing"
        ok=0
    else
        local inherited
        inherited="$(cat "$record")"
        if [[ "$inherited" == "wayland-ambient-selftest" ]]; then
            report FAIL "ambient display: the compositor inherited the ambient display name"
            ok=0
        elif [[ -n "$inherited" && "$inherited" != "<unset>" ]]; then
            report FAIL "ambient display: the compositor inherited a display name ('$inherited')"
            ok=0
        fi
    fi
    if ((ok)); then
        report PASS "the compositor is started with no ambient display name to attach to"
    fi
}

# --- Scenario 17: readiness requires a socket, not a name --------------------
# The readiness wait is what decides that a compositor exists. If it accepts any
# file with the right name, a compositor that failed halfway -- or anything else
# that created that name -- produces a declaration for a display nothing is
# listening on, and the gate that receives it looks elsewhere for a session.
scenario_readiness_requires_a_socket() {
    new_state readiness_socket
    local envfile="$sandbox/env_readiness.txt" log="$sandbox/log_readiness.txt" status=0
    rm -f -- "$envfile"
    # The gate here exits as soon as it starts rather than blocking. A readiness
    # check that wrongly accepted a regular file would otherwise hand a
    # long-running gate a display nothing answers on, and this scenario would
    # report that by hanging until a timeout instead of by name.
    ODYSEA_GATE_STATE_DIR="$active_state" \
        ODYSEA_GATE_COMPOSITOR_CMD="$stub_compositor_regular_file" \
        ODYSEA_GATE_READY_TIMEOUT=2 \
        bash "$harness" "$gate_record" "$envfile" 0 2>"$log" || status=$?

    local ok=1
    if ((status != 1)); then
        report FAIL "readiness: a regular file where the socket belongs should make the harness refuse (expected 1, got $status)"
        ok=0
    fi
    if [[ -e "$envfile" ]]; then
        report FAIL "readiness: the gate was handed a declaration for a name nothing is listening on"
        ok=0
    fi
    if ! grep -q "timed out.*waiting for the compositor socket" "$log"; then
        report FAIL "readiness: the refusal did not name the missing socket"
        ok=0
    fi
    if (($(count_run_dirs) != 0)); then
        report FAIL "readiness: a refused run left a private directory behind"
        ok=0
    fi
    if ((ok)); then
        report PASS "a regular file with the socket's name does not satisfy the readiness wait"
    fi
}

scenario_success_and_env
scenario_missing_socket_refuses_gate
scenario_failure_passthrough
scenario_lock_excludes_second_run
scenario_signal_teardown TERM "SIGTERM (the CTest timeout signal)"
scenario_signal_teardown INT "SIGINT"
scenario_sigkill_and_reaper
scenario_requires_a_gate_command
scenario_headless_proof
scenario_headless_value_allowlist
scenario_stub_bypass_is_qualified
scenario_gate_stopped_before_compositor
scenario_gate_escalated_to_kill
scenario_reaper_escalates_to_kill
scenario_unverified_reap_is_refused
scenario_unreadable_starttime_refuses
scenario_finished_gate_is_not_unidentifiable
scenario_reaper_spares_a_reused_pid
scenario_compositor_gets_no_ambient_display
scenario_readiness_requires_a_socket
scenario_unaccountable_process_is_reported

if ((failures == 0)); then
    # Only meaningful on the passing path: a failing scenario reports each
    # broken expectation separately, so the count exceeds the number of
    # scenarios and the suite fails on its own terms anyway.
    if ((reported != expected_scenario_count)); then
        echo "compositor_gate_harness_self_test: $reported scenario(s) reported a result, expected $expected_scenario_count; the suite did not run in full" >&2
        exit 1
    fi
    echo "compositor_gate_harness_self_test: all $reported scenarios passed"
    exit 0
fi

echo "compositor_gate_harness_self_test: $failures scenario(s) failed" >&2
exit 1

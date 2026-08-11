#!/usr/bin/env bash
# Proves tools/isolated_compositor_gate.sh by mutation, with no real compositor
# running. A stub stands in for the compositor: it binds a genuine AF_UNIX
# socket where a wlroots compositor would, so the harness's own readiness check
# is exercised, and then holds it. Every property the harness exists for is
# checked by observing a specific consequence, not merely an exit status, so a
# harness that quietly dropped the property fails here:
#
#   - the declaration the gate requires (ODYSEA_ISOLATED_COMPOSITOR) is set, and
#     the gate is pointed at the private runtime directory, never the ambient;
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
for tool in flock setsid python3 env; do
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

failures=0
report() {
    local outcome="$1" scenario="$2"
    printf '%-5s %s\n' "$outcome" "$scenario"
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

# --- Scenario 1: declaration set, gate pointed at the private runtime, and a
# clean success tears everything down ---------------------------------------
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
    if [[ "$iso" != "1" ]]; then
        report FAIL "the gate did not receive ODYSEA_ISOLATED_COMPOSITOR=1 (got '$iso')"
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
    if (($(count_run_dirs) != 0)); then
        report FAIL "a run directory survived a clean run"
        return
    fi
    if ! lock_is_free; then
        report FAIL "the lock was not released after a clean run"
        return
    fi
    report PASS "clean run sets the declaration, points the gate at the private runtime, and tears down"
}

# --- Scenario 2: an assertion failure is passed through, and still tears down
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

# --- Scenario 3: a second run skips by name; the lock releases afterward -----
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

# --- Scenarios 4 and 5: a signal mid-run tears the compositor down -----------
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

# --- Scenario 6: SIGKILL releases the lock; the reaper disposes of the residue
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

# --- Scenario 7: a bare invocation with no gate command is refused -----------
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

scenario_success_and_env
scenario_failure_passthrough
scenario_lock_excludes_second_run
scenario_signal_teardown TERM "SIGTERM (the CTest timeout signal)"
scenario_signal_teardown INT "SIGINT"
scenario_sigkill_and_reaper
scenario_requires_a_gate_command

if ((failures == 0)); then
    echo "compositor_gate_harness_self_test: all scenarios passed"
    exit 0
fi

echo "compositor_gate_harness_self_test: $failures scenario(s) failed" >&2
exit 1

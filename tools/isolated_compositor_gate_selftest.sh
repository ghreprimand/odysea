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

if ((failures == 0)); then
    echo "compositor_gate_harness_self_test: all scenarios passed"
    exit 0
fi

echo "compositor_gate_harness_self_test: $failures scenario(s) failed" >&2
exit 1

#!/usr/bin/env bash

# Battery coverage reconciler.
#
# The verification battery reports "100% tests passed" whether it ran every
# entry or a third of them: a skipped entry is named only in a trailing block
# below the headline, and a drop from the full set to a subset changes no
# number a human or a script watches. A git-less tree skips fourteen corpus
# guards; a tree with no display skips every GPU gate; both still read as a
# clean pass. The executed count falling below the registered count is
# invisible to every other check in the tree.
#
# This reconciler makes it visible and fails on it. It takes the registered
# set — the authoritative list `ctest -N` prints, captured live so it can
# never lag the real suite — and the JUnit results of a battery run, and it
# accounts for every registered entry:
#
#   ran           status run: the entry executed (passed or failed; a failure
#                 is ctest's to report, but the entry did run).
#   declined      status notrun with a DECLARED REFUSAL on the entry's own
#                 output: a line of the exact form
#                     <gate-name>: DECL -- declined: <reason>
#                 This is the only tolerated shortfall, and it is tolerated
#                 because it is a policy the project enforces rather than a
#                 capability the machine is missing. The compositor gates
#                 decline a session they were not given to own; refusing to
#                 render an activating window into somebody's live session is
#                 the correct behaviour, and turning it red would only pressure
#                 the next reader into deleting the refusal. Listed by name and
#                 reason, always, so the cost of the refusal stays visible.
#   UNDECLARED    status notrun with output that carries no such line. A
#                 capability skip — "no display server reachable", "another run
#                 holds the lock" — lands here and FAILS. This is deliberate
#                 and is the tight half of the classification: an entry that
#                 could not run is a hole in the battery's coverage, and the
#                 reconciler exists to turn a hole red. Accepting any skip that
#                 printed something would let one `echo` reopen the whole
#                 failure class this gate was built to close.
#   SILENT skip   status notrun with nothing on either stream. Fails, and is
#                 reported separately from UNDECLARED so a log says which of
#                 the two happened.
#   MISSING       registered but absent from the results — deselected or
#                 vanished. Fails.
#   UNEXPECTED    present in the results but not registered — a stale captured
#                 list. Fails, because a reconciler working from a wrong roster
#                 proves nothing.
#
# The bound is two-sided by construction, so a stopped counter cannot satisfy
# it: an empty registered list fails (there is nothing to have run), an empty
# or missing JUnit fails (everything is MISSING), and the results must match
# the roster exactly rather than merely not exceed it.
#
# WHAT THIS CANNOT CATCH, stated plainly:
#   * It cannot judge whether a declared refusal was warranted. A gate that
#     prints the DECL line while being merely unable to run is accepted here.
#     What keeps that honest is elsewhere: the refusal and the inability are
#     separate exit paths in the gate itself, with different text, and
#     ODYSEA_REQUIRE_COMPOSITOR turns the inability red without touching the
#     refusal.
#   * It does not bound how many entries may decline, only that each one says
#     so. A tree where every entry declined still fails, but through the
#     "nothing ran" floor rather than through a decline budget.
#   * It reconciles one run. A roster that shrank because a test was deleted
#     reconciles cleanly; the registered count is printed on every run so that
#     drop is at least readable, and the gate registry is what pins it.
#
# Usage:
#   check_battery_coverage.sh <junit-results.xml> <registered-list.txt>
#
# registered-list.txt holds one registered test name per line, as produced by
#   ctest --test-dir <build> -N | sed -n 's/^ *Test *#[0-9]*: //p'

set -euo pipefail

if [[ "$#" -ne 2 ]]; then
    printf 'usage: %s <junit-results.xml> <registered-list.txt>\n' "$0" >&2
    exit 2
fi

junit_file="$1"
registered_file="$2"

if [[ ! -f "$junit_file" ]]; then
    printf 'battery_coverage: FAIL -- JUnit results not found: %s\n' "$junit_file" >&2
    printf '  A battery that produced no results file ran nothing this reconciler can trust.\n' >&2
    exit 1
fi
if [[ ! -f "$registered_file" ]]; then
    printf 'battery_coverage: FAIL -- registered list not found: %s\n' "$registered_file" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf 'battery_coverage: FAIL -- python3 is required to parse the JUnit results\n' >&2
    exit 1
fi

python3 - "$junit_file" "$registered_file" <<'PYTHON'
import re
import sys
import xml.etree.ElementTree as ET

# The declared-refusal form, anchored at the start of a line. A gate name, the
# DECL token, and a non-empty reason: nothing else is accepted, so a skip
# cannot buy tolerance by printing arbitrary text.
DECLINE_PATTERN = re.compile(r"^[A-Za-z0-9_.-]+: DECL -- declined: *\S", re.MULTILINE)

junit_path, registered_path = sys.argv[1], sys.argv[2]

with open(registered_path, encoding="utf-8") as handle:
    registered = [line.strip() for line in handle if line.strip()]
registered_set = set(registered)

if not registered:
    print("battery_coverage: FAIL -- the registered list is empty", file=sys.stderr)
    print("  `ctest -N` named no tests; the enumeration is broken, not the suite empty.",
          file=sys.stderr)
    sys.exit(1)

try:
    tree = ET.parse(junit_path)
except ET.ParseError as error:
    print(f"battery_coverage: FAIL -- JUnit results are not parseable: {error}", file=sys.stderr)
    sys.exit(1)

ran = []
declined = []         # (name, the declared refusal line)
undeclared_skips = [] # (name, first captured line)
silent_skips = []
disabled = []         # intentionally disabled in the build configuration
unexpected = []
seen = set()

for testcase in tree.iter("testcase"):
    name = testcase.get("name", "")
    if not name:
        continue
    seen.add(name)
    status = testcase.get("status", "")
    if name not in registered_set:
        unexpected.append(name)
    # A test disabled in the build configuration is an intentional, declared
    # opt-out — for example static_analysis under the sanitizer preset, where
    # the sanitizer runtime is the gate and the static pass would only slow the
    # build. It is listed so the omission stays visible, and it does not fail
    # the reconciliation, but it is not counted as having run.
    if status == "disabled":
        disabled.append(name)
        continue
    system_out = testcase.findtext("system-out") or ""
    system_err = testcase.findtext("system-err") or ""
    captured = (system_out + system_err).strip()
    is_skip = status == "notrun" or testcase.find("skipped") is not None
    if is_skip and status != "run":
        declaration = DECLINE_PATTERN.search(captured)
        if declaration is not None:
            line_end = captured.find("\n", declaration.start())
            line = captured[declaration.start():] if line_end < 0 \
                else captured[declaration.start():line_end]
            declined.append((name, line.strip()))
        elif captured:
            first_line = next((line for line in captured.splitlines() if line.strip()), "")
            undeclared_skips.append((name, first_line.strip()))
        else:
            silent_skips.append(name)
    else:
        ran.append(name)

missing = [name for name in registered if name not in seen]

# The reconciliation table is always printed, so the executed count is never
# invisible again regardless of the result.
print("battery_coverage: reconciliation")
print(f"  registered : {len(registered)}")
print(f"  ran        : {len(ran)}")
print(f"  declined   : {len(declined)}")
for name, reason in sorted(declined):
    print(f"    - {name}: {reason}")
if undeclared_skips:
    print(f"  UNDECLARED skips: {len(undeclared_skips)}")
    for name, first_line in sorted(undeclared_skips):
        print(f"    - {name}: skipped without a declared refusal; said: {first_line}")
if disabled:
    print(f"  disabled   : {len(disabled)}")
    for name in sorted(disabled):
        print(f"    - {name}: disabled in the build configuration")
if silent_skips:
    print(f"  SILENT skips: {len(silent_skips)}")
    for name in sorted(silent_skips):
        print(f"    - {name}: skipped with no cause on either stream")
if missing:
    print(f"  MISSING: {len(missing)}")
    for name in sorted(missing):
        print(f"    - {name}: registered but absent from the results")
if unexpected:
    print(f"  UNEXPECTED: {len(unexpected)}")
    for name in sorted(unexpected):
        print(f"    - {name}: in the results but not registered")

problems = []
if undeclared_skips:
    problems.append(f"{len(undeclared_skips)} skip(s) without a declared refusal")
if silent_skips:
    problems.append(f"{len(silent_skips)} silent skip(s)")
if missing:
    problems.append(f"{len(missing)} missing entr(ies)")
if unexpected:
    problems.append(f"{len(unexpected)} unexpected entr(ies)")

# A named skip is an allowed opt-out, but a run where NOTHING ran and every
# entry opted out is not coverage; the executed count has fallen to the floor.
if not ran:
    problems.append("nothing ran (0 executed entries)")

if problems:
    print(f"battery_coverage: FAIL -- {', '.join(problems)}", file=sys.stderr)
    sys.exit(1)

accounted = len(ran) + len(declined) + len(disabled)
disabled_note = f", {len(disabled)} disabled" if disabled else ""
print(f"battery_coverage: PASS -- {accounted}/{len(registered)} registered entries accounted for "
      f"({len(ran)} ran, {len(declined)} declared refusal(s){disabled_note})")
PYTHON

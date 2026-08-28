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
# set — the authoritative list `ctest` prints, captured live so it can never
# lag the real suite — the declared skip capabilities, and the JUnit results of
# a battery run, and it accounts for every registered entry:
#
#   ran           status run: the entry executed (passed or failed; a failure
#                 is ctest's to report, but the entry did run).
#   declined      status notrun, declared in the registry with tolerance
#                 `refusal`, and carrying a DECLARED REFUSAL plus a proof that
#                 the isolated-compositor policy boundary, rather than an
#                 unavailable renderer, produced it. Its own output must carry
#                 both exact forms
#                     <gate-name>: DECL -- declined: <reason>
#                    <gate-name>: REFUSAL-PROOF -- isolated-compositor interlock rejected the session before renderer setup
#                 This is the only tolerated shortfall, and it is tolerated
#                 because it is a policy the project enforces rather than a
#                 capability the machine is missing. The compositor gates
#                 decline a session they were not given to own; refusing to
#                 render an activating window into somebody's live session is
#                 the correct behaviour, and turning it red would only pressure
#                 the next reader into deleting the refusal. Listed by name and
#                 reason, always, so the cost of the refusal stays visible.
#   UNMET         status notrun from an entry declared with tolerance
#                 `capability`. FAILS, and prints the precondition the registry
#                 records, so the reader is told what to provide instead of
#                 having to rediscover it from the entry's own text.
#   UNPROVEN      status notrun declared as a refusal but missing either the
#                 declaration or matching interlock proof. FAILS. A capability
#                 skip — "no display server reachable", "another run holds the
#                 lock" — lands here when relabelled as a refusal. This is the
#                 tight half of the classification: an entry that could not run
#                 is a hole in the battery's coverage, and the reconciler exists
#                 to turn a hole red. A bare DECL `echo` cannot reopen it.
#   SILENT skip   status notrun with nothing on either stream. Fails, and is
#                 reported separately from UNPROVEN so a log says which of
#                 the two happened.
#   MISSING       registered but absent from the results — deselected or
#                 vanished. Fails.
#   UNEXPECTED    present in the results but not registered — a stale captured
#                 list. Fails.
#
# THE DECLARATION REGISTRY, and why it exists.
#
# Everything above judges a skip after it has happened. That left the mechanism
# itself unwatched: an entry could be given SKIP_RETURN_CODE in the build
# configuration and the reconciler would learn of it only on the first run
# where it actually skipped — and if that skip printed a bare refusal line, it
# was tolerated from then on without anyone deciding it should be. An honest skip
# and a completed check read identically in a summary, so the defect was never
# the skip mechanism; it was a skip the reconciler did not know existed.
#
# So the capability is declared up front, in tools/skip_declarations.txt, and
# the roster carries each entry's skip return code from the live configuration.
# The two are reconciled before any result is read:
#
#   * an entry that can skip and is not declared fails;
#   * a declaration naming an entry that is not registered fails;
#   * a declaration naming a registered entry that cannot skip fails, so a
#     declaration cannot outlive the mechanism it describes;
#   * a skip return code other than the project's own fails, so a second skip
#     convention cannot be introduced beside the declared one;
#   * a not-run entry with no declaration at all fails by that name, which
#     covers the skips ctest itself invents — a missing executable, a
#     dependency that did not run — and which no output-shaped rule can see.
#
# WHAT THIS CANNOT CATCH, stated plainly:
#   * It cannot judge whether an interlock refusal was warranted. It does,
#     however, require the refusal path to prove that the interlock produced
#     it. A bare DECL line cannot change an unavailable renderer into an
#     accounted policy refusal. The refusal and inability remain separate exit
#     paths in the gate itself, and ODYSEA_REQUIRE_COMPOSITOR turns inability
#     red without touching an interlock refusal.
#   * It cannot prove that a recorded precondition is the true one. It warns
#     when the recorded precondition and the entry's first reported line have
#     no significant token in common; that catches a wrong pointer, but is
#     deliberately partial rather than a claim of semantic agreement.
#   * It does not bound how many entries may decline, only that each one says
#     so. A tree where every entry declined still fails, but through the
#     "nothing ran" floor rather than through a decline budget.
#   * It reconciles one run. A roster that shrank because a test was deleted
#     reconciles cleanly; the registered count is printed on every run so that
#     drop is at least readable, and the gate registry is what pins it.
#
# The bound is two-sided by construction, so a stopped counter cannot satisfy
# it: an empty registered list fails (there is nothing to have run), an empty
# or missing JUnit fails (everything is MISSING), and the results must match
# the roster exactly rather than merely not exceed it.
#
# Usage:
#   check_battery_coverage.sh <junit-results.xml> <roster.txt> <declarations.txt>
#
# roster.txt holds one registered test per line as
#   <name>[<TAB><skip-return-code>]
# with the second field present only for an entry that declares one. It is
# produced from `ctest --show-only=json-v1`, which reports test properties, so
# the roster cannot disagree with the configuration it was taken from.

set -euo pipefail

if [[ "$#" -ne 3 ]]; then
    printf 'usage: %s <junit-results.xml> <roster.txt> <declarations.txt>\n' "$0" >&2
    exit 2
fi

junit_file="$1"
registered_file="$2"
declarations_file="$3"

if [[ ! -f "$junit_file" ]]; then
    printf 'battery_coverage: FAIL -- JUnit results not found: %s\n' "$junit_file" >&2
    printf '  A battery that produced no results file ran nothing this reconciler can trust.\n' >&2
    exit 1
fi
if [[ ! -f "$registered_file" ]]; then
    printf 'battery_coverage: FAIL -- roster not found: %s\n' "$registered_file" >&2
    exit 1
fi
if [[ ! -f "$declarations_file" ]]; then
    printf 'battery_coverage: FAIL -- skip declarations not found: %s\n' "$declarations_file" >&2
    printf '  Without them no skip capability is declared, and an undeclared one is what this fails on.\n' >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf 'battery_coverage: FAIL -- python3 is required to parse the JUnit results\n' >&2
    exit 1
fi

python3 - "$junit_file" "$registered_file" "$declarations_file" <<'PYTHON'
import re
import sys
import xml.etree.ElementTree as ET

# A policy refusal is not an arbitrary DECL line. The declaration and following
# proof share a gate name, and the proof is emitted only by the
# isolated-compositor interlock path before the launcher can prepare a renderer.
# This separates that policy boundary from a GPU gate that never acquired an
# OpenGL context.
DECLINE_PATTERN = re.compile(
    r"^(?P<gate>[A-Za-z0-9_.-]+): DECL -- declined: *\S", re.MULTILINE)
REFUSAL_PROOF_PATTERN = re.compile(
    r"^(?P<gate>[A-Za-z0-9_.-]+): REFUSAL-PROOF -- "
    r"isolated-compositor interlock rejected the session before renderer setup$",
    re.MULTILINE)

# A precondition check cannot establish semantic equivalence, but words shorter
# than three characters and connective prose do not help a reader distinguish
# a declared requirement from an unrelated report.
SIGNIFICANT_TOKEN_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{2,}")
INSIGNIFICANT_TOKENS = frozenset({
    "and", "are", "but", "can", "could", "for", "from", "has", "have",
    "into", "its", "not", "that", "the", "this", "was", "with",
})

# The one skip return code this project uses. A second convention would be a
# second mechanism to keep honest, so the roster is required to agree with it.
PROJECT_SKIP_CODE = "77"

TOLERANCES = ("refusal", "capability")

junit_path, registered_path, declarations_path = sys.argv[1], sys.argv[2], sys.argv[3]


def fail(summary, *detail):
    print(f"battery_coverage: FAIL -- {summary}", file=sys.stderr)
    for line in detail:
        print(f"  {line}", file=sys.stderr)
    sys.exit(1)


# --- the roster -------------------------------------------------------------
registered = []
skip_code_of = {}
with open(registered_path, encoding="utf-8") as handle:
    for raw in handle:
        line = raw.rstrip("\n")
        if not line.strip():
            continue
        fields = line.split("\t")
        name = fields[0].strip()
        if not name:
            continue
        registered.append(name)
        code = fields[1].strip() if len(fields) > 1 else ""
        if code:
            skip_code_of[name] = code
registered_set = set(registered)

if not registered:
    fail("the roster is empty",
         "`ctest` named no tests; the enumeration is broken, not the suite empty.")

# --- the declarations -------------------------------------------------------
declared = {}  # name -> (tolerance, precondition)
malformed = []
with open(declarations_path, encoding="utf-8") as handle:
    for number, raw in enumerate(handle, start=1):
        line = raw.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = [field.strip() for field in line.split("\t")]
        if len(fields) != 3 or not all(fields):
            malformed.append(f"line {number}: expected name<TAB>tolerance<TAB>precondition")
            continue
        name, tolerance, precondition = fields
        if tolerance not in TOLERANCES:
            malformed.append(f"line {number}: tolerance {tolerance!r} is not one of "
                             + ", ".join(TOLERANCES))
            continue
        if name in declared:
            malformed.append(f"line {number}: {name} is declared more than once")
            continue
        declared[name] = (tolerance, precondition)

if malformed:
    fail(f"the skip declarations are malformed ({len(malformed)} line(s))", *malformed)

# --- static reconciliation, before a single result is read ------------------
undeclared_capability = sorted(name for name in skip_code_of if name not in declared)
stale_declarations = sorted(name for name in declared if name not in registered_set)
inoperative_declarations = sorted(
    name for name in declared if name in registered_set and name not in skip_code_of)
foreign_skip_codes = sorted(
    (name, code) for name, code in skip_code_of.items() if code != PROJECT_SKIP_CODE)

static_problems = []
if undeclared_capability:
    static_problems.append(
        f"{len(undeclared_capability)} entr(ies) can skip without being declared")
if stale_declarations:
    static_problems.append(f"{len(stale_declarations)} declaration(s) name no registered entry")
if inoperative_declarations:
    static_problems.append(
        f"{len(inoperative_declarations)} declaration(s) name an entry that cannot skip")
if foreign_skip_codes:
    static_problems.append(f"{len(foreign_skip_codes)} entr(ies) use a foreign skip return code")

if static_problems:
    detail = []
    for name in undeclared_capability:
        detail.append(f"{name}: carries a skip return code with no line in the declarations; "
                      "declare it as a refusal or a capability, in the change that added it")
    for name in stale_declarations:
        detail.append(f"{name}: declared, but no entry of that name is registered")
    for name in inoperative_declarations:
        detail.append(f"{name}: declared, but the entry carries no skip return code; "
                      "the declaration outlived the mechanism it describes")
    for name, code in foreign_skip_codes:
        detail.append(f"{name}: skip return code {code}, expected {PROJECT_SKIP_CODE}")
    fail(", ".join(static_problems), *detail)

# --- the results ------------------------------------------------------------
try:
    tree = ET.parse(junit_path)
except ET.ParseError as error:
    fail(f"JUnit results are not parseable: {error}")

ran = []
declined = []          # (name, the declared refusal line)
unmet = []             # (name, precondition, first captured line)
precondition_warnings = []  # (name, precondition, first captured line)
unproven_refusals = []  # (name, first captured line)
undeclared_notrun = [] # (name, first captured line)
silent_skips = []
disabled = []          # intentionally disabled in the build configuration
unexpected = []
seen = set()


def first_line(text):
    return next((line.strip() for line in text.splitlines() if line.strip()), "")


def significant_tokens(text):
    return {
        token.lower()
        for token in SIGNIFICANT_TOKEN_PATTERN.findall(text)
        if token.lower() not in INSIGNIFICANT_TOKENS
    }


def declared_refusal(captured):
    declaration = DECLINE_PATTERN.search(captured)
    if declaration is None:
        return "", False
    proof = REFUSAL_PROOF_PATTERN.search(captured)
    if (proof is None or proof.group("gate") != declaration.group("gate")
            or proof.start() <= declaration.end()):
        return "", False
    line_end = captured.find("\n", declaration.start())
    line = captured[declaration.start():] if line_end < 0 \
        else captured[declaration.start():line_end]
    return line.strip(), True


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
        tolerance, precondition = declared.get(name, (None, ""))
        if tolerance is None:
            # Not declared, and therefore not a skip the configuration permits.
            # This is the class no output-shaped rule can see: ctest reports a
            # missing executable or an unsatisfied dependency as not-run too.
            undeclared_notrun.append((name, first_line(captured)))
        elif tolerance == "capability":
            said = first_line(captured)
            unmet.append((name, precondition, said))
            if not significant_tokens(precondition) & significant_tokens(said):
                precondition_warnings.append((name, precondition, said))
        else:
            declaration, has_proof = declared_refusal(captured)
            if has_proof:
                declined.append((name, declaration))
            elif captured:
                unproven_refusals.append((name, first_line(captured)))
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
print(f"  declared   : {len(declared)} skip capabilit(ies) "
      f"({sum(1 for tolerance, _ in declared.values() if tolerance == 'refusal')} refusal, "
      f"{sum(1 for tolerance, _ in declared.values() if tolerance == 'capability')} capability)")
print(f"  declined   : {len(declined)}")
for name, reason in sorted(declined):
    print(f"    - {name}: {reason}")
if unmet:
    print(f"  UNMET preconditions: {len(unmet)}")
    for name, precondition, said in sorted(unmet):
        print(f"    - {name}: declared to require {precondition}")
        if said:
            print(f"      said: {said}")
if precondition_warnings:
    print(f"  WARNING precondition reports that do not resemble their declarations: "
          f"{len(precondition_warnings)}")
    for name, precondition, said in sorted(precondition_warnings):
        print(f"    - {name}: declared precondition does not resemble what the entry reported")
        print(f"      declared: {precondition}")
        print(f"      said: {said or '(no captured output)'}")
if undeclared_notrun:
    print(f"  UNDECLARED not-run: {len(undeclared_notrun)}")
    for name, said in sorted(undeclared_notrun):
        print(f"    - {name}: did not run and declares no skip capability; said: {said}")
if unproven_refusals:
    print(f"  UNPROVEN refusals: {len(unproven_refusals)}")
    for name, said in sorted(unproven_refusals):
        print(f"    - {name}: declaration did not carry an interlock proof; said: {said}")
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
if unmet:
    problems.append(f"{len(unmet)} unmet declared precondition(s)")
if undeclared_notrun:
    problems.append(f"{len(undeclared_notrun)} not-run entr(ies) with no declared skip capability")
if unproven_refusals:
    problems.append(f"{len(unproven_refusals)} unproven declared refusal(s)")
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
      f"({len(ran)} ran, {len(declined)} declared refusal(s){disabled_note}); "
      f"{len(declared)} declared skip capabilit(ies) match the configuration")
PYTHON

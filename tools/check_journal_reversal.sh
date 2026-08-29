#!/usr/bin/env bash

# Reversal-guard mutation gate for the operation journal.
#
# The journal's tests demonstrate that a reversal happens and that it refuses.
# They cannot show that any individual guard is what produces the refusal. A
# guard that stopped being read would leave every scenario green, and the guards
# here are the ones whose absence destroys data: a reversal removes what a copy
# created, so a verification field that is recorded but never compared is a file
# that gets deleted after being edited.
#
# So each guard is deleted in turn, one at a time, and the suite is required to
# fail. The mutation is applied to a copy of the source in a scratch directory:
# nothing tracked is modified, and a parallel run of the rest of the battery
# cannot see this gate at all.
#
# Two properties this gate holds itself to, because a mutation harness fails
# silently in exactly the ways it is meant to detect:
#
#   A mutation that did not change the file measured nothing. Every mutation is
#   compared against the original and the gate fails if the text did not move.
#   A mutation that failed to compile also measured nothing, and is reported as
#   a harness fault rather than counted either way.
#
#   A guard with no reachable failing state is declared, not omitted. Such a
#   mutation is required to SURVIVE, and the gate fails if one starts being
#   caught, because that means the declaration is stale and the record that
#   describes it is wrong. A declaration cannot quietly become a free pass.
#
# Usage: check_journal_reversal.sh

set -euo pipefail

readonly source_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly journal_source="core/src/operation_journal.cpp"
readonly journal_header="core/include/odysea/core/operation_journal.hpp"

compiler="${CXX:-c++}"
if ! command -v "$compiler" >/dev/null 2>&1; then
    printf 'journal_reversal_guard: no C++ compiler is available, so no guard was measured\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

readonly objects="$workspace/objects"
mkdir -p "$objects"

# Everything except the journal is compiled once: only the journal is mutated,
# so only the journal is recompiled per mutation.
compile_flags=(-std=c++20 -O0
    -DODYSEA_THUMBNAIL_NAMESPACE='"odysea-journal-reversal-guard"'
    "-I$source_root/core/include" "-I$source_root/core/src" "-I$source_root/tests")

fixed_objects=()
for translation_unit in "$source_root"/core/src/*.cpp; do
    if [[ "$translation_unit" == */operation_journal.cpp ]]; then
        continue
    fi
    object="$objects/$(basename "$translation_unit" .cpp).o"
    if ! "$compiler" "${compile_flags[@]}" -c "$translation_unit" -o "$object" \
        2>"$workspace/compile.log"; then
        printf 'journal_reversal_guard: the unmutated core did not compile, so no guard was measured\n' >&2
        sed 's/^/  /' "$workspace/compile.log" >&2
        exit 1
    fi
    fixed_objects+=("$object")
done

if ! "$compiler" "${compile_flags[@]}" -c "$source_root/tests/test_operation_journal.cpp" \
    -o "$objects/suite.o" 2>"$workspace/compile.log"; then
    printf 'journal_reversal_guard: the unmutated suite did not compile, so no guard was measured\n' >&2
    sed 's/^/  /' "$workspace/compile.log" >&2
    exit 1
fi
fixed_objects+=("$objects/suite.o")

# Builds and runs the suite against one journal source. Prints nothing; the
# return value is 0 when the suite passed, 1 when it failed, 2 when the build
# itself failed and therefore measured nothing.
run_suite_against() {
    local journal="$1"

    if ! "$compiler" "${compile_flags[@]}" -c "$journal" -o "$workspace/journal.o" \
        2>"$workspace/compile.log"; then
        return 2
    fi
    if ! "$compiler" "$workspace/journal.o" "${fixed_objects[@]}" -lpthread \
        -o "$workspace/suite" 2>"$workspace/compile.log"; then
        return 2
    fi
    if "$workspace/suite" >"$workspace/suite.log" 2>&1; then
        return 0
    fi
    return 1
}

status=0
measured=0
caught=0
declared=0

# The unmutated suite has to pass, or every "caught" below is meaningless: a
# suite that fails for its own reasons fails under every mutation too.
cp "$source_root/$journal_source" "$workspace/baseline.cpp"
if ! run_suite_against "$workspace/baseline.cpp"; then
    printf 'journal_reversal_guard: the unmutated suite does not pass, so no mutation result means anything\n' >&2
    sed 's/^/  /' "$workspace/suite.log" >&2
    exit 1
fi

# Applies one substitution to a copy of a file and reports whether the text
# actually moved. A substitution that matched nothing leaves the file identical,
# and `sed` reports success either way.
apply_mutation() {
    local file="$1"
    local original="$2"
    local search="$3"
    local replacement="$4"

    python3 - "$original" "$file" "$search" "$replacement" <<'PYTHON'
import pathlib
import sys

original, target, search, replacement = sys.argv[1:5]
text = pathlib.Path(original).read_text()
if search not in text:
    sys.exit(3)
mutated = text.replace(search, replacement, 1)
if mutated == text:
    sys.exit(3)
pathlib.Path(target).write_text(mutated)
PYTHON
}

# expectation is `caught` for a guard with a reachable failing state, or
# `declared` for one whose removal cannot change any outcome. A declared
# mutation that starts being caught fails this gate: the declaration is then
# stale and the record describing it needs correcting.
measure_guard() {
    local name="$1"
    local expectation="$2"
    local search="$3"
    local replacement="$4"

    local mutated="$workspace/mutated.cpp"
    if ! apply_mutation "$mutated" "$workspace/baseline.cpp" "$search" "$replacement"; then
        printf 'journal_reversal_guard: the mutation for %s did not change the source, so it measured nothing\n' \
            "$name" >&2
        status=1
        return
    fi
    if cmp -s "$mutated" "$workspace/baseline.cpp"; then
        printf 'journal_reversal_guard: the mutation for %s left the source identical, so it measured nothing\n' \
            "$name" >&2
        status=1
        return
    fi

    local result=0
    run_suite_against "$mutated" || result=$?
    measured=$((measured + 1))

    case "$result" in
        2)
            printf 'journal_reversal_guard: the mutation for %s did not compile, so it measured nothing\n' \
                "$name" >&2
            status=1
            ;;
        1)
            if [[ "$expectation" == "declared" ]]; then
                printf 'journal_reversal_guard: %s is declared unreachable but the suite caught it; the declaration is stale\n' \
                    "$name" >&2
                status=1
            else
                caught=$((caught + 1))
            fi
            ;;
        0)
            if [[ "$expectation" == "declared" ]]; then
                declared=$((declared + 1))
            else
                printf 'journal_reversal_guard: %s can be removed with the suite green\n' \
                    "$name" >&2
                status=1
            fi
            ;;
    esac
}

# --- The guards that stand between a reversal and destroyed data ------------

measure_guard "the result's recorded identity" caught \
    '!same_identity(now.identity, slot.verification.identity)) {' \
    'false) {'

measure_guard "the result root's modification time" caught \
    'now.modified_seconds != slot.verification.modified_seconds ||
        now.apparent_bytes != slot.verification.size) {' \
    'now.apparent_bytes != slot.verification.size) {'

measure_guard "the result root's size" caught \
    'now.modified_seconds != slot.verification.modified_seconds ||
        now.apparent_bytes != slot.verification.size) {' \
    'now.modified_seconds != slot.verification.modified_seconds) {'

measure_guard "the recorded size of every entry inside a copied tree" caught \
    'entry.modified_seconds, entry.size);' \
    'entry.modified_seconds);'

measure_guard "the recorded modification time of every entry inside a copied tree" caught \
    'entry.modified_seconds, entry.size);' \
    'entry.size);'

measure_guard "the comparison of a copied tree against its record" caught \
    'if (scan != TreeScan::Complete || !same_tree(slot.verification.created, current)) {' \
    'if (scan != TreeScan::Complete) {'

measure_guard "the size recorded for each entry of a copied tree" caught \
    '.size = metadata.apparent_bytes});' \
    '.size = 0});'

# --- The barriers, which decide what is never offered as reversible ---------

measure_guard "the barrier for a discarded destination" caught \
    '        return ReversalBarrier::ReplacedEntryDiscarded;' \
    '        return ReversalBarrier::None;'

measure_guard "the barrier for an operation that changed nothing" caught \
    '        return ReversalBarrier::NothingChanged;' \
    '        return ReversalBarrier::None;'

measure_guard "the barrier for a copied tree past the limit" caught \
    '            slot.record.barrier = ReversalBarrier::CreatedTreeTooLarge;' \
    '            slot.record.barrier = ReversalBarrier::None;'

measure_guard "the barrier for a crossing move of a shared entry" caught \
    '        slot.record.barrier = ReversalBarrier::HardLinksNotRestorable;' \
    '        slot.record.barrier = ReversalBarrier::None;'

measure_guard "the exclusion that keeps a directory out of the shared-entry barrier" caught \
    '!detail::mode_is_directory(before.mode) && before.link_count > 1) {' \
    'before.link_count > 1) {'

measure_guard "the refusal to act on a barred record" caught \
    'if (!slot.record.reversible()) {' \
    'if (false) {'

measure_guard "the offer that hides a barred record" caught \
    'return !slots_.empty() && slots_.back().record.reversible();' \
    'return !slots_.empty();'

# --- The history, and the steps of a reversal -------------------------------

measure_guard "the bound on the history" caught \
    '    while (slots_.size() > capacity_) {
        slots_.pop_front();
    }
' \
    ''

measure_guard "the refusal to displace whatever is in a reversal's way" caught \
    'constexpr OperationOptions undisturbing{.conflict = ConflictPolicy::Fail};' \
    'constexpr OperationOptions undisturbing{.conflict = ConflictPolicy::Overwrite};'

measure_guard "the record kept by a reversal that did not complete" caught \
    '    if (outcome.succeeded()) {
        journal.slots_.pop_back();
    }
' \
    '    journal.slots_.pop_back();
'

measure_guard "the repointing of a record after a partial reversal" caught \
    '                slot.record.result_path = landed;
' \
    ''

measure_guard "the removal of a restored entry's trash record" caught \
    'fs::remove(slot.record.trash_record_path, error);' \
    'error.clear();'

# --- Guards with no reachable failing state, declared rather than omitted ---
#
# Both need the filesystem to change underneath a running operation, or a
# result that a just-completed operation cannot examine. Neither is producible
# from a test, and both are kept because without them an operation that quietly
# replaced something could be offered as reversible. They are measured here so
# that "nothing covers these" is re-measured on every run rather than asserted
# once in a record nobody re-reads.

measure_guard "the barrier for a result that could not be examined" declared \
    '    if (!observation.result_known) {
        return ReversalBarrier::ResultNotIdentified;
    }
' \
    ''

measure_guard "the barrier for an operation that missed its predicted destination" declared \
    '    if (!observation.prediction_held) {' \
    '    if (false) {'

# A floor under the count. A gate that measured fewer guards than it lists has
# lost one to a search string that stopped matching, and would otherwise report
# a clean run over whatever remained.
readonly expected_guards=21
if ((measured != expected_guards)); then
    printf 'journal_reversal_guard: measured %d guard(s), expected %d; the gate did not run in full\n' \
        "$measured" "$expected_guards" >&2
    status=1
fi

# A floor under the declarations. Without it, every declared guard could become
# catchable and the gate would still report the same summary line.
readonly expected_declared=2
if ((declared != expected_declared)); then
    printf 'journal_reversal_guard: %d declared guard(s) remained unreachable, expected %d\n' \
        "$declared" "$expected_declared" >&2
    status=1
fi

if ((status == 0)); then
    printf 'journal_reversal_guard: %d guard(s) measured, %d caught by the suite, %d declared unreachable\n' \
        "$measured" "$caught" "$declared"
fi

exit "$status"

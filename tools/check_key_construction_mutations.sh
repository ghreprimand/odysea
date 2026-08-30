#!/usr/bin/env bash
set -euo pipefail

# Proves the key-construction self-test would notice the guard losing a check.
#
# The self-test shows that the guard rejects planted defects. It cannot show
# that it would still reject them after the guard is edited, and that is the
# failure that actually happens: a check is weakened, every scenario still
# passes because none of them depended on the part that went, and the suite
# reports assurance it no longer establishes. This battery removes one piece
# of the guard at a time and requires the self-test to fail by name.
#
# Two disciplines, both learned the hard way. Every mutation is applied and
# then DIFFED, because an edit that matched nothing exits successfully and
# leaves a survivor that never existed. And every mutation states which
# scenario must object, because a suite failing for an unrelated reason proves
# nothing about the check that was removed.

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly guard_source="$tools_directory/check_key_construction.sh"
readonly selftest_source="$tools_directory/check_key_construction_selftest.sh"
readonly library_source="$tools_directory/guard_corpus.sh"

if ! command -v git >/dev/null 2>&1; then
    echo "key_construction_mutation_guard: git is required by the scenarios this battery runs and is not installed" >&2
    exit 1
fi

# How many mutations this file plants. Compared against the number actually
# run, so a mutation dropped by an edit fails the gate instead of shrinking it
# silently.
readonly declared_mutations=7

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

planted=0
caught=0
survivors=0
unlanded=0

# Rebuilds a clean copy of the three scripts and reports the sandbox path.
sandbox() {
    local root="$workspace/case"
    rm -rf -- "$root"
    mkdir -p "$root/tools"
    cp "$guard_source" "$root/tools/check_key_construction.sh"
    cp "$selftest_source" "$root/tools/check_key_construction_selftest.sh"
    cp "$library_source" "$root/tools/guard_corpus.sh"
    printf '%s\n' "$root"
}

# Applies one mutation and requires the self-test to reject it, naming the
# expectation that must do the rejecting.
#
# `target` is the file to edit inside the sandbox, `pattern` and `replacement`
# are passed to a literal string substitution, and `expected` is text the
# self-test's own output must contain.
mutate() {
    local description="$1"
    local target="$2"
    local pattern="$3"
    local replacement="$4"
    local expected="$5"

    local root
    root="$(sandbox)"
    local path="$root/tools/$target"
    local before after
    before="$(cat "$path")"
    python3 - "$path" "$pattern" "$replacement" <<'PYTHON'
import sys

path, pattern, replacement = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, encoding="utf-8") as handle:
    body = handle.read()
with open(path, "w", encoding="utf-8") as handle:
    handle.write(body.replace(pattern, replacement, 1))
PYTHON
    after="$(cat "$path")"

    planted=$((planted + 1))
    if [[ "$before" == "$after" ]]; then
        printf 'key_construction_mutation_guard: NOT LANDED - %s changed nothing\n' "$description" >&2
        unlanded=$((unlanded + 1))
        return
    fi

    local output
    local code
    output="$(cd "$root" && bash tools/check_key_construction_selftest.sh 2>&1)" && code=0 || code=$?
    if ((code == 0)); then
        printf 'key_construction_mutation_guard: SURVIVOR - %s left the self-test green\n' \
            "$description" >&2
        survivors=$((survivors + 1))
        return
    fi
    if [[ "$output" != *"$expected"* ]]; then
        printf 'key_construction_mutation_guard: WRONG REASON - %s failed without mentioning "%s"; output: %s\n' \
            "$description" "$expected" "$output" >&2
        survivors=$((survivors + 1))
        return
    fi
    caught=$((caught + 1))
    printf 'key_construction_mutation_guard: caught - %s\n' "$description"
}

# 1. The second rule stops being applied at all. Everything it covers becomes
#    invisible, which is the state the guard was in before it had the rule.
mutate "the entry-path rule is never applied" check_key_construction.sh \
    '    seen=0
    if ! seen="$(apply_rule "$path" "$entry_path_pattern" "$entry_path_subject" \
        "${entry_path_permitted[@]}")"; then
        status=1
    fi
    entry_path_sightings=$((entry_path_sightings + seen))' \
    '    entry_path_sightings=$((entry_path_sightings + 3))' \
    "hand-spelled entry path in a reconciliation member"

# 2. One conversion is dropped from the pattern's alternation. The rule still
#    works for the spelling the corpus happens to use, and admits the others.
mutate "native() is dropped from the entry-path pattern" check_key_construction.sh \
    'string|native|c_str|u8string|generic_string' \
    'string|c_str|u8string|generic_string' \
    "entry path spelled through native()"

# 3. The permitted list for the second rule is widened until it permits the
#    member the documented bypass sat in.
mutate "the entry-path rule permits a reconciliation member" check_key_construction.sh \
    'readonly -a entry_path_permitted=(data activate selectedPaths)' \
    'readonly -a entry_path_permitted=(data activate selectedPaths applyPresentationSettings)' \
    "hand-spelled entry path in a reconciliation member"

# 4. The second rule's vacuity floor goes. A rule that matches nowhere then
#    reads as compliance rather than as a rule that has gone dead.
mutate "the entry-path vacuity floor is removed" check_key_construction.sh \
    'if ((entry_path_sightings == 0)); then' \
    'if false; then' \
    "no permitted entry-path spelling"

# 5. The violation status of the second rule is discarded while its count is
#    still collected, which is the shape that hides a failure inside an
#    assignment.
mutate "the entry-path rule's status is swallowed" check_key_construction.sh \
    '    seen=0
    if ! seen="$(apply_rule "$path" "$entry_path_pattern" "$entry_path_subject" \
        "${entry_path_permitted[@]}")"; then
        status=1
    fi' \
    '    seen="$(apply_rule "$path" "$entry_path_pattern" "$entry_path_subject" \
        "${entry_path_permitted[@]}" || true)"' \
    "hand-spelled entry path in a reconciliation member"

# 6. A match that sits outside every member definition is accepted instead of
#    reported, so a file-scope helper becomes a place to keep one.
mutate "a match outside any member is accepted" check_key_construction.sh \
    '        if [[ -z "$enclosing" ]]; then
            printf '\''key_construction_guard: %s:%s %s outside any member function\n'\'' \
                "$file" "$line" "$subject" >&2
            rule_status=1
            continue
        fi' \
    '        if [[ -z "$enclosing" ]]; then
            sightings=$((sightings + 1))
            continue
        fi' \
    "entry path in a free function: expected exit 1"

# 7. The battery turned on the suite that runs it: a scenario is deleted from
#    the self-test, and the expectation floor has to notice that fewer ran
#    than are declared. Without this the other six mutations could all be
#    caught by a suite that had quietly stopped running most of its cases.
mutate "a self-test scenario is deleted" check_key_construction_selftest.sh \
    'expect "entry path spelled through native()" "$root" 1 "in receiveScanBatch"' \
    'true' \
    "expectations, 17 are declared"

if ((planted != declared_mutations)); then
    printf 'key_construction_mutation_guard: planted %d mutations, %d are declared\n' \
        "$planted" "$declared_mutations" >&2
    exit 1
fi
if ((unlanded != 0)); then
    printf 'key_construction_mutation_guard: %d mutation(s) never landed and measured nothing\n' \
        "$unlanded" >&2
    exit 1
fi
if ((survivors != 0)); then
    printf 'key_construction_mutation_guard: %d mutation(s) survived\n' "$survivors" >&2
    exit 1
fi
if ((caught != declared_mutations)); then
    printf 'key_construction_mutation_guard: caught %d of %d mutations\n' \
        "$caught" "$declared_mutations" >&2
    exit 1
fi

printf 'key_construction_mutation_guard: %d mutations planted, %d caught, 0 survivors\n' \
    "$planted" "$caught"

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
readonly declared_mutations=21

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

# 1. The conversion half of the second rule stops being applied at all. What
#    it covers becomes invisible, which is the state the guard was in before it
#    had the rule.
mutate "the entry-path conversion rule is never applied" check_key_construction.sh \
    '    seen=0
    if ! seen="$(apply_rule "$path" "$entry_path_conversion_pattern" "$entry_path_subject" \
        "${entry_path_permitted[@]}")"; then
        status=1
    fi
    entry_path_sightings=$((entry_path_sightings + seen))' \
    '    entry_path_sightings=$((entry_path_sightings + 3))' \
    "hand-spelled entry path in a reconciliation member"

# 2. One conversion is dropped from the pattern's alternation. The rule still
#    works for the spelling the corpus happens to use, and admits the others.
mutate "native() is dropped from the entry-path pattern" check_key_construction.sh \
    '\w*string\w*|native|c_str' \
    '\w*string\w*|c_str' \
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

# 5. The violation status of the conversion rule is discarded while its count
#    is still collected, which is the shape that hides a failure inside an
#    assignment.
mutate "the entry-path conversion rule's status is swallowed" check_key_construction.sh \
    '    seen=0
    if ! seen="$(apply_rule "$path" "$entry_path_conversion_pattern" "$entry_path_subject" \
        "${entry_path_permitted[@]}")"; then
        status=1
    fi' \
    '    seen="$(apply_rule "$path" "$entry_path_conversion_pattern" "$entry_path_subject" \
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
    'expect "entry path in a free function" "$root" 1 "outside any member function"' \
    'true' \
    "expectations, 41 are declared"

# --- The first rule, which had no battery at all ----------------------------
# Every mutation above targets the entry-path rule, and the normalization rule
# - the older of the two - had none. That asymmetry is how its vacuity floor
# came to be deletable with the whole suite green: the scenario named for it
# asserted only the words both floors share, so it passed on the other rule's
# message. The four below give the first rule the same treatment as the
# second, in the same order, so the asymmetry cannot be reintroduced by
# writing one rule's battery and not the other's.

# 8. The first rule stops being applied at all.
mutate "the normalization rule is never applied" check_key_construction.sh \
    '    seen=0
    if ! seen="$(apply_rule "$path" "$normalization_pattern" "$normalization_subject" \
        "${normalization_permitted[@]}")"; then
        status=1
    fi
    normalization_sightings=$((normalization_sightings + seen))' \
    '    normalization_sightings=$((normalization_sightings + 3))' \
    "hand-spelled key in another member"

# 9. Its violation status is discarded while its count is still collected.
mutate "the normalization rule's status is swallowed" check_key_construction.sh \
    '    seen=0
    if ! seen="$(apply_rule "$path" "$normalization_pattern" "$normalization_subject" \
        "${normalization_permitted[@]}")"; then
        status=1
    fi' \
    '    seen="$(apply_rule "$path" "$normalization_pattern" "$normalization_subject" \
        "${normalization_permitted[@]}" || true)"' \
    "hand-spelled key in another member"

# 10. Its permitted list is widened until it permits a reconciliation member.
mutate "the normalization rule permits a reconciliation member" check_key_construction.sh \
    'readonly -a normalization_permitted=(entryKey normalizedFilesystemPath)' \
    'readonly -a normalization_permitted=(entryKey normalizedFilesystemPath receiveScanBatch)' \
    "hand-spelled key in another member"

# 11. Its vacuity floor goes. This is the one that was live: the scenario
#     named for this floor asserted the suffix both floors share, so the
#     entry-path floor fired on the same fixture with the same trailing words
#     and the suite reported a pass for a check that no longer ran.
mutate "the normalization vacuity floor is removed" check_key_construction.sh \
    'if ((normalization_sightings == 0)); then' \
    'if false; then' \
    "no permitted normalization"

# --- The rest of the guard --------------------------------------------------

# 12. A member definition stops ending, so the name set at one survives to the
#     end of the file and a helper below it inherits that member's name.
mutate "a member definition never ends" check_key_construction.sh \
    '        /^}/ { name = "" }
' \
    '' \
    "helper below a permitted member"

# 13. The alias branch stops being applied at all, and one idiomatic line
#     separates `.path` from the conversion again.
mutate "the alias rule is never applied" check_key_construction.sh \
    '    seen=0
    if ! seen="$(apply_alias_rule "$path" "$entry_path_alias_pattern" "$entry_path_subject" \
        "${entry_path_permitted[@]}")"; then
        status=1
    fi
    entry_path_sightings=$((entry_path_sightings + seen))' \
    '    entry_path_sightings=$((entry_path_sightings + 0))' \
    "entry path bound to a filesystem-path reference"

# 14. The explicit template argument stops being tolerated, so the member
#     template form of the same conversion walks past the rule.
mutate "an explicit template argument defeats the entry-path pattern" check_key_construction.sh \
    '[[:space:]]*(<[^>]*>)?[[:space:]]*\(\)' \
    '\(\)' \
    "entry path spelled through string<char>()"

# 15. The floor on how many files were inspected goes, so a rename that leaves
#     the guard reading nothing reads as compliance.
mutate "the inspected-files floor is removed" check_key_construction.sh \
    'if ((inspected_files == 0)); then' \
    'if false; then' \
    "no matching sources"

# 16. Coverage stops following the include, so a source that reaches into the
#     model under an unrelated name becomes invisible.
mutate "the include-glob union is dropped" check_key_construction.sh \
    "readonly include_glob='app/src/*'" \
    "readonly include_glob='app/src/no_such_source*'" \
    "unrelated name including the model header"

# 17. The battery turned on the suite again, at the enumeration this time: a
#     conversion is dropped from the self-test's own list. Without its count
#     floor the enumeration would shrink silently, which is precisely how the
#     guard came to enumerate five conversions while thirteen existed.
mutate "a conversion is dropped from the self-test enumeration" check_key_construction_selftest.sh \
    '    "generic_u16string()"
' \
    '' \
    "planted 12 conversions, 13 are declared"

# --- The alias branch's statement view --------------------------------------
# The alias branch is matched against a folded statement rather than a physical
# line, because the formatter wraps a long alias after the `=`. The four below
# remove each piece of that, in the order the guard comment introduces them.

# 18. The assignment-continuation join is reverted, so a declaration the
#     formatter wrapped after the `=` is matched as two lines and neither half
#     matches. This is the load-bearing hole: an alias exempted for growing
#     past the column limit, with no author intent.
mutate "the assignment-continuation join is reverted" check_key_construction.sh \
    '            return 1
        }
        BEGIN { pattern = ENVIRON["KG_ALIAS_PATTERN"] }' \
    '            return 0
        }
        BEGIN { pattern = ENVIRON["KG_ALIAS_PATTERN"] }' \
    "entry path bound through a wrapped reference declaration"

# 19. The initializer introducer is narrowed back to `=`, so a brace
#     initializer walks past the rule.
mutate "the brace initializer allowance is reverted" check_key_construction.sh \
    '[^=;{]*[={][^;]*\.path' \
    '[^=;{]*=[^;]*\.path' \
    "entry path bound through a brace initializer"

# 20. The trailing-punctuation allowance after `.path` is reverted to require
#     `.path` immediately before the `;`, so a `std::move` initializer - a `)`
#     between `.path` and the `;` - walks past.
mutate "the trailing-punctuation allowance is reverted" check_key_construction.sh \
    '\.path[)}[:space:]]*;' \
    '\.path[[:space:]]*;' \
    "entry path bound through std::move"

# 21. The alias type set is widened to catch a typedef, which turns the typedef
#     residual from accepted to rejected. This is the behavioural side of the
#     residual's pin: the residual is only genuinely declared if catching it
#     would break its scenario, and this proves it does.
mutate "the alias type set is widened to catch a typedef" check_key_construction.sh \
    '(auto|std::filesystem::path)[^=;{]*[={]' \
    '(auto|std::filesystem::path|FsPath)[^=;{]*[={]' \
    "an entry path bound through a typedef of the path type"

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

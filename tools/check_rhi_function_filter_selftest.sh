#!/usr/bin/env bash

# Proves the function-filter gate rejects both directions of the drift it
# exists to catch, accepts a list that agrees with its TestCase, and refuses
# every state in which it could not have checked anything.
#
# Both faults are invisible in a passing run — an unlisted function executes
# nowhere and an unmatched filter matches nothing, and either way the entry
# stays green in the time it always took. A gate for silent faults is itself
# easy to leave silently broken, so each scenario builds a throwaway project
# and requires a specific reason rather than merely a non-zero exit.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly guard="$tools_directory/check_rhi_function_filter.sh"
if [[ ! -f "$guard" ]]; then
    printf 'rhi_function_filter_self_test: the gate is missing\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

status=0
checked=0

# Writes a QML case file holding one TestCase and the functions named for it.
#
# The fixture carries the awkward shape on purpose, because the convenient one
# measures nothing:
#
#   * the TestCase's `name:` shares a line with the brace that opens it, which
#     a line-oriented parse attributes to the enclosing block;
#   * the TestCase is nested inside an OUTER block that is itself named, so
#     attributing a function to the outermost name rather than the nearest one
#     produces a different answer. Without an outer name both readings agree
#     and the attribution is never actually exercised;
#   * a real string property holds a CLOSING brace, and it sits above the
#     functions. In a `//` comment it would prove nothing, since comments are
#     stripped before the walk. A stray opening brace would prove little
#     either: it would deepen the walk into an unnamed block, and attribution
#     falls back outward to the same TestCase. A stray closing brace is the one
#     that bites — it pops the walk out of the TestCase, so every function
#     below it is attributed to the enclosing block instead.
write_case_file() {
    local path="$1" case_name="$2"
    shift 2

    {
        printf 'import QtQuick\nimport QtTest\n\n'
        printf 'Item { name: "EnclosingBlockThatIsNotTheTestCase"\n'
        printf '    TestCase { name: "%s"\n' "$case_name"
        printf '        property string closer: "}"\n'
        printf '        // A brace in a comment is stripped before the walk: {\n'
        local function_name
        for function_name in "$@"; do
            printf '        function %s() {\n            verify(true);\n        }\n' \
                "$function_name"
        done
        printf '        property string opener: "{"\n'
        printf '    }\n}\n'
    } >"$path"
}

# Builds a throwaway project: a build file with the three-hop chain the gate
# resolves, and a scope directory holding the case files.
#
# Arguments after the project name are the filter arguments written into
# add_test. The scope's own content is written by the caller through
# write_case_file, so a scenario can make the two disagree.
build_project() {
    local name="$1"
    shift

    local root="$workspace/$name"
    mkdir -p "$root/tests/validation"

    {
        printf 'qt_add_executable(suite tests/tst_quick.cpp)\n'
        printf 'target_compile_definitions(suite\n'
        printf '    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/validation")\n'
        printf 'qt_add_executable(launcher tests/tst_launcher.cpp)\n'
        printf 'target_compile_definitions(launcher\n'
        printf '    PRIVATE ODYSEA_PRESENTATION_BINARY="$<TARGET_FILE:suite>")\n'
        printf 'add_test(NAME gpu_entry\n    COMMAND launcher\n'
        local argument
        for argument in "$@"; do
            printf '        %s\n' "$argument"
        done
        printf ')\n'
    } >"$root/CMakeLists.txt"

    printf '%s' "$root"
}

expect_outcome() {
    local scenario="$1"
    local expectation="$2"
    local build_file="$3"
    local fragment="${4:-}"

    local output=""
    local exit_status=0
    output="$(bash "$guard" "$build_file" 2>&1)" || exit_status=$?

    checked=$((checked + 1))
    case "$expectation" in
        accept)
            if ((exit_status != 0)); then
                printf 'rhi_function_filter_self_test: %s should be accepted, but the gate said: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            elif [[ "$output" != *"each defined by the TestCase it names"* ]]; then
                printf 'rhi_function_filter_self_test: %s exited zero without reaching the final report: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            elif [[ -n "$fragment" && "$output" != *"$fragment"* ]]; then
                printf 'rhi_function_filter_self_test: %s should report %s, but said: %s\n' \
                    "$scenario" "$fragment" "$output" >&2
                status=1
            fi
            ;;
        reject)
            if ((exit_status != 1)); then
                printf 'rhi_function_filter_self_test: %s should be rejected, exit was %s: %s\n' \
                    "$scenario" "$exit_status" "$output" >&2
                status=1
            elif [[ -n "$fragment" && "$output" != *"$fragment"* ]]; then
                printf 'rhi_function_filter_self_test: %s should report %s, but said: %s\n' \
                    "$scenario" "$fragment" "$output" >&2
                status=1
            fi
            ;;
    esac
}

# --- The list and the TestCase agree ----------------------------------------
root="$(build_project agreeing Scaling::test_one Scaling::test_two)"
write_case_file "$root/tests/validation/tst_scaling.qml" Scaling test_one test_two
expect_outcome agreeing accept "$root/CMakeLists.txt" "list 2 function(s)"

# --- A function added to the TestCase and not listed -------------------------
# The fault the gate was built for: on the filtered path it runs nowhere, and
# for a test that is only meaningful there it runs nowhere at all.
root="$(build_project unlisted Scaling::test_one Scaling::test_two)"
write_case_file "$root/tests/validation/tst_scaling.qml" \
    Scaling test_one test_two test_addedLastWeek
expect_outcome unlisted reject "$root/CMakeLists.txt" \
    'does not list Scaling::test_addedLastWeek, so that function runs nowhere on this path'

# --- A listed function the TestCase does not define --------------------------
# Qt Quick Test does not object to a filter matching nothing, so a renamed or
# deleted function leaves an argument behind that covers nothing.
root="$(build_project absent_function Scaling::test_one Scaling::test_renamedAway)"
write_case_file "$root/tests/validation/tst_scaling.qml" Scaling test_one
expect_outcome absent_function reject "$root/CMakeLists.txt" \
    'lists Scaling::test_renamedAway, which tests/validation does not define'

# --- A listed TestCase that does not exist at all ----------------------------
# The same silent nothing one level up: the filter names a case the scope never
# defines, so every argument under it matches nothing.
root="$(build_project absent_case Ghost::test_one)"
write_case_file "$root/tests/validation/tst_scaling.qml" Scaling test_one
expect_outcome absent_case reject "$root/CMakeLists.txt" \
    'lists Ghost::test_one, which tests/validation does not define'

# --- A TestCase the filter never mentions is left alone ----------------------
# Selection at TestCase granularity is a decision the build file is allowed to
# make; only completeness within a named case is required. Without this the
# gate would demand that an unrelated suite be added to a list it does not
# belong on, and the first person to hit that would delete the gate.
root="$(build_project unmentioned_case Scaling::test_one)"
write_case_file "$root/tests/validation/tst_scaling.qml" Scaling test_one
write_case_file "$root/tests/validation/tst_visual.qml" \
    Visual test_somethingElse test_andAnother
expect_outcome unmentioned_case accept "$root/CMakeLists.txt" "list 1 function(s)"

# --- Functions spread across two files of one named TestCase -----------------
# The scope is read whole rather than per file, so a case continued in a second
# file cannot hide an unlisted function.
root="$(build_project split_case Scaling::test_one)"
write_case_file "$root/tests/validation/tst_scaling.qml" Scaling test_one
write_case_file "$root/tests/validation/tst_scaling_more.qml" Scaling test_alsoScaling
expect_outcome split_case reject "$root/CMakeLists.txt" \
    'does not list Scaling::test_alsoScaling'

# --- The chain must resolve, and a break in it is not a pass -----------------
root="$(build_project no_binary Scaling::test_one)"
write_case_file "$root/tests/validation/tst_scaling.qml" Scaling test_one
sed -i '/ODYSEA_PRESENTATION_BINARY/d;/^target_compile_definitions(launcher$/d' \
    "$root/CMakeLists.txt"
expect_outcome no_binary reject "$root/CMakeLists.txt" \
    'declares no ODYSEA_PRESENTATION_BINARY'

root="$(build_project no_scope Scaling::test_one)"
write_case_file "$root/tests/validation/tst_scaling.qml" Scaling test_one
sed -i '/QUICK_TEST_SOURCE_DIR/d;/^target_compile_definitions(suite$/d' \
    "$root/CMakeLists.txt"
expect_outcome no_scope reject "$root/CMakeLists.txt" \
    'declares no QUICK_TEST_SOURCE_DIR'

root="$(build_project missing_scope Scaling::test_one)"
rmdir "$root/tests/validation"
expect_outcome missing_scope reject "$root/CMakeLists.txt" \
    'which does not exist'

# --- A scope holding no test function covers nothing -------------------------
root="$(build_project empty_scope Scaling::test_one)"
write_case_file "$root/tests/validation/tst_scaling.qml" Scaling
expect_outcome empty_scope reject "$root/CMakeLists.txt" \
    'defines no test function, so the filter covers nothing'

# --- A build file with no filtered entry must not report success -------------
# The project has one. A run that finds none has failed to parse rather than
# found a build file that stopped filtering, and the two must not read alike.
unfiltered_root="$workspace/unfiltered"
mkdir -p "$unfiltered_root"
printf 'add_test(NAME plain COMMAND suite)\n' >"$unfiltered_root/CMakeLists.txt"
expect_outcome unfiltered reject "$unfiltered_root/CMakeLists.txt" \
    'no add_test carries TestCase::function arguments'

# --- A build file that does not exist ----------------------------------------
expect_outcome absent_build_file reject "$workspace/nowhere/CMakeLists.txt" \
    'does not exist'

if ((status != 0)); then
    exit "$status"
fi

printf 'rhi_function_filter_self_test: %d filter states are enforced\n' "$checked"

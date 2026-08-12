#!/usr/bin/env bash

# Keeps a launcher's function filter in step with the TestCase it filters.
#
# Two GPU-path entries re-exec a Qt Quick Test suite with the OpenGL RHI
# backend forced. One of them does not run the whole suite: it passes a list of
# `TestCase::function` arguments straight through to the suite, so only the
# named functions execute on that path. Nothing kept the list and the TestCase
# in step, and both directions of the drift are silent.
#
#   A function added and not listed runs nowhere on the GPU path. The entry
#   keeps passing, in the same time it took before, because it never ran the
#   new function. Where the same function also runs under the offscreen
#   entries this is survivable — they carry no filter, so they would catch a
#   plain failure. It is NOT survivable for a test that is only meaningful on a
#   real GL path: such a test either passes vacuously offscreen or guards
#   itself away there, so if it is also missing from this list it runs nowhere
#   at all and no entry turns red. That shape is not hypothetical; it is the
#   shape of the test that exposed this hole.
#
#   A listed function that does not exist executes nothing. Qt Quick Test does
#   not object to a filter that matches no function, so a renamed or deleted
#   function leaves an argument behind that quietly covers nothing, and the
#   entry still reports success.
#
# So this gate resolves each filtered entry to the QML it actually runs and
# requires exact agreement, in both directions, for every TestCase the filter
# mentions.
#
# THE RESOLUTION IS THREE HOPS, all read from the build file rather than
# assumed:
#
#   add_test(NAME <entry> COMMAND <launcher> Case::function ...)
#   target_compile_definitions(<launcher> ... ODYSEA_PRESENTATION_BINARY=
#       "$<TARGET_FILE:<suite>>")
#   target_compile_definitions(<suite> ... QUICK_TEST_SOURCE_DIR=
#       "${CMAKE_CURRENT_SOURCE_DIR}/<scope>")
#
# A break anywhere along that chain fails by name. A gate that could not follow
# the chain has not checked the entry, and reporting success there would be the
# same vacuous pass this exists to reject.
#
# THE COMPARISON IS PER TESTCASE, NOT PER DIRECTORY, and the distinction is
# deliberate. A scope may hold several TestCases and a filtered entry may
# legitimately want only some of them — the validation scope holds both the
# device-pixel cases and the broader visual suite, and the GPU entry names only
# the first. Requiring every function in the scope would demand the others be
# added to a list they do not belong on. So selection at TestCase granularity
# is treated as intentional, and completeness is required within each TestCase
# the filter names.
#
# WHAT THIS CANNOT CATCH, stated rather than left to be discovered:
#   * A whole TestCase that no filtered entry mentions. Omitting one is
#     indistinguishable from choosing not to run it, which is a decision this
#     file cannot read. Only functions inside a TestCase already named are
#     bounded.
#   * An entry with no filter at all. It runs its whole suite, so there is
#     nothing to keep in step.
#   * Whether a listed function is meaningful on the path it is listed for. The
#     suite's own vacuity sentinels answer that; this gate answers only whether
#     what is listed and what exists are the same set.
#
# Usage:
#   check_rhi_function_filter.sh [build-file]

set -euo pipefail

# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"

if [[ $# -ge 1 ]]; then
    readonly build_file="$1"
else
    guard_corpus_init rhi_function_filter
    readonly build_file="$guard_corpus_root/app/CMakeLists.txt"
fi

if [[ ! -f "$build_file" ]]; then
    printf 'rhi_function_filter: %s does not exist\n' "$build_file" >&2
    exit 1
fi

source_directory="$(cd "$(dirname "$build_file")" && pwd)"
readonly source_directory

status=0

fail() {
    printf 'rhi_function_filter: %s\n' "$1" >&2
    status=1
}

# Every CMake command on one line, so a command split across lines parses the
# same as one written inline. Parentheses are balanced rather than counted per
# line, and `#` comments are dropped first so a commented parenthesis cannot
# unbalance the join.
logical_commands() {
    awk '
        {
            line = $0
            sub(/#.*$/, "", line)
            joined = joined " " line
            n = gsub(/\(/, "(", line)
            m = gsub(/\)/, ")", line)
            depth += n - m
            if (depth <= 0 && joined ~ /[^ \t]/) {
                gsub(/[ \t]+/, " ", joined)
                sub(/^ /, "", joined)
                print joined
                joined = ""
                depth = 0
            }
        }
    ' "$build_file"
}

commands="$(logical_commands)"

# --- Hop 2 and 3: the compile definitions each target carries ---------------
definition_for() {
    local target="$1" definition="$2"
    printf '%s\n' "$commands" |
        sed -n "s|^target_compile_definitions( *${target} .*${definition}=\"\([^\"]*\)\".*|\1|p" |
        head -n1
}

# --- Hop 1: entries whose COMMAND carries Case::function arguments ----------
filtered_entries="$(printf '%s\n' "$commands" |
    sed -n 's|^add_test( *NAME \([^ ]*\) COMMAND \([^ ]*\) \(.*[A-Za-z0-9_]::[A-Za-z0-9_].*\))$|\1 \2 \3|p')"

filtered_count="$(printf '%s' "$filtered_entries" | grep -c . || true)"

# A parse that finds nothing has examined nothing. The project has at least one
# filtered entry; a run that reports otherwise is a broken parse, not a build
# file that stopped filtering, and it must not read as a pass.
if ((filtered_count == 0)); then
    printf 'rhi_function_filter: no add_test carries TestCase::function arguments in %s\n' \
        "$build_file" >&2
    printf '  Either the parse is broken or the filtered entry was removed; neither is a pass.\n' >&2
    exit 1
fi

# --- The TestCases and functions a scope actually defines -------------------
# Emits "TestCase<TAB>function" for every `function test_*` in the scope,
# attributed to the nearest enclosing block that carries a `name:` property.
#
# The line is walked character by character rather than pattern-matched whole,
# because a brace inside a string is not a block boundary and a block may open
# on the same line as the name it carries. Tracking quoting while walking gets
# both right without a second pass.
scope_functions() {
    local scope="$1"
    find "$scope" -name 'tst_*.qml' -type f -print0 |
        sort -z |
        xargs -0 -r awk '
            function attribute(text,   depth_index, name) {
                if (match(text, /function[ \t]+test_[A-Za-z0-9_]*[ \t]*\(/)) {
                    name = substr(text, RSTART, RLENGTH)
                    sub(/^function[ \t]+/, "", name)
                    sub(/[ \t]*\($/, "", name)
                    for (depth_index = depth; depth_index >= 0; depth_index--) {
                        if (block_name[depth_index] != "") {
                            printf "%s\t%s\n", block_name[depth_index], name
                            return
                        }
                    }
                    printf "<unnamed>\t%s\n", name
                }
            }
            function record_name(text,   value) {
                if (match(text, /name:[ \t]*"[^"]*"/)) {
                    value = substr(text, RSTART, RLENGTH)
                    sub(/^name:[ \t]*"/, "", value)
                    sub(/"$/, "", value)
                    if (block_name[depth] == "") block_name[depth] = value
                }
            }
            FNR == 1 { depth = 0; delete block_name }
            {
                line = $0
                sub(/\/\/.*$/, "", line)
                segment = ""
                in_string = 0
                for (position = 1; position <= length(line); position++) {
                    character = substr(line, position, 1)
                    if (character == "\"") in_string = !in_string
                    if (!in_string && (character == "{" || character == "}")) {
                        record_name(segment)
                        attribute(segment)
                        segment = ""
                        if (character == "{") {
                            depth++
                            block_name[depth] = ""
                        } else if (depth > 0) {
                            block_name[depth] = ""
                            depth--
                        }
                        continue
                    }
                    segment = segment character
                }
                record_name(segment)
                attribute(segment)
            }
        '
}

checked_entries=0
checked_functions=0

while IFS= read -r entry_line; do
    [[ -n "$entry_line" ]] || continue

    entry_name="${entry_line%% *}"
    remainder="${entry_line#* }"
    launcher="${remainder%% *}"
    arguments="${remainder#* }"

    suite_reference="$(definition_for "$launcher" ODYSEA_PRESENTATION_BINARY)"
    if [[ -z "$suite_reference" ]]; then
        fail "$entry_name filters $launcher, which declares no ODYSEA_PRESENTATION_BINARY, so the suite it runs cannot be resolved"
        continue
    fi

    suite_target="$(printf '%s' "$suite_reference" |
        sed -n 's|^[$]<TARGET_FILE:\(.*\)>$|\1|p')"
    if [[ -z "$suite_target" ]]; then
        fail "$entry_name resolves to $suite_reference, which is not a TARGET_FILE reference"
        continue
    fi

    scope_relative="$(definition_for "$suite_target" QUICK_TEST_SOURCE_DIR)"
    scope_relative="${scope_relative#\$\{CMAKE_CURRENT_SOURCE_DIR\}/}"
    if [[ -z "$scope_relative" ]]; then
        fail "$entry_name runs $suite_target, which declares no QUICK_TEST_SOURCE_DIR, so its cases cannot be located"
        continue
    fi

    scope_absolute="$source_directory/$scope_relative"
    if [[ ! -d "$scope_absolute" ]]; then
        fail "$entry_name runs cases from $scope_relative, which does not exist"
        continue
    fi

    defined="$(scope_functions "$scope_absolute")"
    if [[ -z "$defined" ]]; then
        fail "$entry_name filters $scope_relative, which defines no test function, so the filter covers nothing"
        continue
    fi

    listed="$(printf '%s\n' "$arguments" | tr ' ' '\n' |
        grep -E '^[A-Za-z0-9_]+::[A-Za-z0-9_]+$' | sort -u || true)"
    if [[ -z "$listed" ]]; then
        fail "$entry_name was selected as filtered but no argument parses as TestCase::function"
        continue
    fi

    checked_entries=$((checked_entries + 1))

    # Every listed function must exist, in the TestCase it is listed under.
    while IFS= read -r pair; do
        [[ -n "$pair" ]] || continue
        case_name="${pair%%::*}"
        function_name="${pair##*::}"
        checked_functions=$((checked_functions + 1))
        if ! printf '%s\n' "$defined" |
            grep -Fxq -- "$(printf '%s\t%s' "$case_name" "$function_name")"; then
            fail "$entry_name lists $pair, which $scope_relative does not define; Qt Quick Test runs nothing for an unmatched filter"
        fi
    done <<<"$listed"

    # And every function of a named TestCase must be listed. Only the
    # TestCases the filter already mentions are bounded; see the note above.
    named_cases="$(printf '%s\n' "$listed" | sed 's|::.*||' | sort -u)"
    while IFS= read -r case_name; do
        [[ -n "$case_name" ]] || continue
        while IFS= read -r function_name; do
            [[ -n "$function_name" ]] || continue
            if ! printf '%s\n' "$listed" |
                grep -Fxq -- "$case_name::$function_name"; then
                fail "$entry_name runs $case_name but does not list $case_name::$function_name, so that function runs nowhere on this path"
            fi
        done < <(printf '%s\n' "$defined" |
            awk -F'\t' -v wanted="$case_name" '$1 == wanted { print $2 }')
    done <<<"$named_cases"
done <<<"$filtered_entries"

if ((status != 0)); then
    exit "$status"
fi

printf 'rhi_function_filter: %d filtered entr(ies) list %d function(s), each defined by the TestCase it names\n' \
    "$checked_entries" "$checked_functions"

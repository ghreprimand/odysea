#!/usr/bin/env bash

# Verifies that every QML test runner owns a distinct leaf directory.
#
# Qt Quick Test scans its source directory recursively and offers no exclusion.
# A runner aimed at a parent of another runner's directory therefore adopts that
# runner's cases: they execute twice, and a failure is attributed to the wrong
# test entry, which sends a diagnosing reader to a suite that does not contain
# the fault. The reverse mistake is quieter still — a runner aimed at a
# directory holding no `tst_*.qml` prints nothing and exits successfully, so the
# entry stays green while covering nothing.
#
# Both faults are invisible in a passing run, so they are checked statically
# here instead of being left to inspection.

set -euo pipefail

# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"

# The brace and dollar characters are written as bracket expressions because a
# backslash-escaped brace opens an interval in a basic regular expression.
readonly scope_pattern='QUICK_TEST_SOURCE_DIR="[$][{]CMAKE_CURRENT_SOURCE_DIR[}]/'

if [[ $# -ge 1 ]]; then
    readonly build_file="$1"
else
    guard_corpus_init qml_test_scopes
    readonly build_file="$guard_corpus_root/app/CMakeLists.txt"
fi

if [[ ! -f "$build_file" ]]; then
    printf 'qml_test_scopes: %s does not exist\n' "$build_file" >&2
    exit 1
fi

source_directory="$(cd "$(dirname "$build_file")" && pwd)"
readonly source_directory

status=0
declare -a scopes=()

# Collects the directory each runner scans. The relative form is kept for
# messages; the absolute form is what the comparisons below use.
while IFS= read -r relative; do
    [[ -n "$relative" ]] || continue
    scopes+=("$relative")
done < <(sed -n "s|.*${scope_pattern}\([^\"]*\)\".*|\1|p" "$build_file")

# A parse that finds nothing would report success over an unexamined file, which
# is the same vacuous pass this gate exists to reject.
if ((${#scopes[@]} == 0)); then
    printf 'qml_test_scopes: no QML test scopes found in %s\n' "$build_file" >&2
    exit 1
fi

# Every scope must exist and must actually hold cases for its runner to execute.
for relative in "${scopes[@]}"; do
    absolute="$source_directory/$relative"
    if [[ ! -d "$absolute" ]]; then
        printf 'qml_test_scopes: %s scans %s, which does not exist\n' \
            "$(basename "$build_file")" "$relative" >&2
        status=1
        continue
    fi
    if [[ -z "$(find "$absolute" -name 'tst_*.qml' -print -quit)" ]]; then
        printf 'qml_test_scopes: %s holds no tst_*.qml, so its runner passes vacuously\n' \
            "$relative" >&2
        status=1
    fi
done

# No scope may contain another. Comparing with a trailing separator keeps a
# sibling whose name merely starts with another's from matching.
scope_count="${#scopes[@]}"
for ((outer = 0; outer < scope_count; outer++)); do
    for ((inner = 0; inner < scope_count; inner++)); do
        ((outer != inner)) || continue

        container="${scopes[outer]}"
        contained="${scopes[inner]}"

        if [[ "$container" == "$contained" ]]; then
            # Report the duplicated pair once rather than from both sides.
            if ((outer < inner)); then
                printf 'qml_test_scopes: two runners both scan %s, so its cases run twice\n' \
                    "$container" >&2
                status=1
            fi
            continue
        fi

        if [[ "$contained/" == "$container/"* ]]; then
            printf 'qml_test_scopes: %s is scanned by the runner for %s, so its cases run twice and report under the wrong entry\n' \
                "$contained" "$container" >&2
            status=1
        fi
    done
done

if ((status != 0)); then
    exit "$status"
fi

printf 'qml_test_scopes: %d QML test runners own distinct populated directories\n' \
    "$scope_count"

#!/usr/bin/env bash

# Proves the QML test-scope gate rejects the layouts it exists to catch and
# accepts a correct one.
#
# The faults this gate covers are both invisible in a passing test run: nested
# scopes duplicate cases under the wrong entry, and an empty scope reports
# success while executing nothing. A gate for silent faults is itself easy to
# leave silently broken, so each scenario builds a throwaway project and
# requires a specific outcome rather than merely a non-zero exit.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly guard="$tools_directory/check_qml_test_scopes.sh"
if [[ ! -f "$guard" ]]; then
    printf 'qml_test_scopes_self_test: the gate is missing\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

status=0
checked=0

# Builds a throwaway project whose build file declares one runner per named
# scope, then creates each scope directory unless it is marked otherwise.
#
# A scope may be suffixed with `:empty` to create the directory without a case,
# or `:absent` to leave it out entirely.
build_project() {
    local name="$1"
    shift

    local root="$workspace/$name"
    mkdir -p "$root"

    local build_file="$root/CMakeLists.txt"
    : >"$build_file"

    local entry relative marker
    for entry in "$@"; do
        relative="${entry%%:*}"
        marker=""
        [[ "$entry" == *:* ]] && marker="${entry##*:}"

        printf 'target_compile_definitions(runner\n    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/%s")\n' \
            "$relative" >>"$build_file"

        case "$marker" in
            absent) ;;
            empty) mkdir -p "$root/$relative" ;;
            *)
                mkdir -p "$root/$relative"
                printf 'import QtTest\nTestCase {}\n' \
                    >"$root/$relative/tst_case.qml"
                ;;
        esac
    done

    printf '%s' "$build_file"
}

# Runs the gate against one throwaway project and requires acceptance, or
# rejection whose reason names an expected fragment.
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
                printf 'qml_test_scopes_self_test: %s should be accepted, but the gate said: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            elif [[ "$output" != *"own distinct populated directories"* ]]; then
                printf 'qml_test_scopes_self_test: %s did not reach the end of the checks\n' \
                    "$scenario" >&2
                status=1
            fi
            ;;
        reject)
            if ((exit_status != 1)); then
                printf 'qml_test_scopes_self_test: %s should be rejected\n' \
                    "$scenario" >&2
                status=1
            elif [[ -n "$fragment" && "$output" != *"$fragment"* ]]; then
                printf 'qml_test_scopes_self_test: %s should report %s, but said: %s\n' \
                    "$scenario" "$fragment" "$output" >&2
                status=1
            fi
            ;;
    esac
}

# Sibling leaf directories are the layout the gate is protecting.
expect_outcome siblings accept \
    "$(build_project siblings tests/shell tests/presentation tests/software)"

# A runner aimed at the parent of another runner's directory adopts its cases.
expect_outcome nested reject \
    "$(build_project nested tests tests/presentation)" \
    'run twice and report under the wrong entry'

# The same fault survives extra depth between the two scopes.
expect_outcome deeply_nested reject \
    "$(build_project deep tests tests/a/b/presentation)" \
    'run twice and report under the wrong entry'

# Two runners scanning one directory duplicate every case in it.
expect_outcome duplicate reject \
    "$(build_project duplicate tests/shell tests/shell)" \
    'both scan'

# A directory holding no case leaves its runner green over nothing.
expect_outcome empty_scope reject \
    "$(build_project empty tests/shell tests/presentation:empty)" \
    'passes vacuously'

# A scope that does not exist is the same vacuous pass one step earlier.
expect_outcome absent_scope reject \
    "$(build_project absent tests/shell tests/presentation:absent)" \
    'does not exist'

# A sibling whose name merely begins with another's is not nested inside it.
expect_outcome shared_prefix accept \
    "$(build_project prefix tests/shell tests/shell-fallback)"

# A build file declaring no runner at all must not report success: the gate
# would otherwise pass over a file whose declarations it failed to parse.
no_scopes_root="$workspace/none"
mkdir -p "$no_scopes_root"
printf 'add_executable(runner main.cpp)\n' >"$no_scopes_root/CMakeLists.txt"
expect_outcome no_scopes reject "$no_scopes_root/CMakeLists.txt" \
    'no QML test scopes found'

if ((status != 0)); then
    exit "$status"
fi

printf 'qml_test_scopes_self_test: %d scope layouts are enforced\n' "$checked"

#!/usr/bin/env bash

# Proves the module manifest gate rejects the failures it exists to catch.
#
# The gate compares a tracked scene corpus against a built module manifest. A
# gate that compared nothing would look identical to a passing one, so each
# scenario below builds a throwaway repository and a throwaway manifest, then
# requires a specific outcome. An always-failing gate fails the accepting
# scenario, and an always-passing gate fails every rejecting one.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly guard="$tools_directory/check_qml_module.sh"
if [[ ! -f "$guard" ]]; then
    printf 'qml_module_guard_self_test: the gate is missing\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

status=0
checked=0

# Builds a repository holding the named scenes and a manifest holding the named
# component lines, then requires the gate to accept or reject it.
expect_verdict() {
    local scenario="$1"
    local expectation="$2"
    local scenes="$3"
    local components="$4"

    local sandbox="$workspace/$scenario"
    mkdir -p "$sandbox/app/qml" "$sandbox/module"
    git -C "$sandbox" init -q
    git -C "$sandbox" config core.hooksPath /dev/null

    local scene
    while IFS= read -r scene; do
        if [[ -z "$scene" ]]; then
            continue
        fi
        printf 'import QtQuick\n\nItem {\n}\n' >"$sandbox/app/qml/$scene"
        git -C "$sandbox" add -f "app/qml/$scene"
    done <<<"$scenes"

    if [[ "$components" != "none" ]]; then
        {
            printf 'module OdySea\n'
            printf 'typeinfo odysea.qmltypes\n'
            local component
            while IFS= read -r component; do
                if [[ -n "$component" ]]; then
                    printf '%s\n' "$component"
                fi
            done <<<"$components"
            printf 'depends QtQuick\n'
        } >"$sandbox/module/qmldir"
    fi

    local accepted=0
    if (cd "$sandbox" && bash "$guard" "$sandbox/module" >/dev/null 2>&1); then
        accepted=1
    fi

    checked=$((checked + 1))
    if [[ "$expectation" == "accept" && "$accepted" -ne 1 ]]; then
        printf 'qml_module_guard_self_test: %s should be accepted\n' \
            "$scenario" >&2
        status=1
    fi
    if [[ "$expectation" == "reject" && "$accepted" -ne 0 ]]; then
        printf 'qml_module_guard_self_test: %s should be rejected\n' \
            "$scenario" >&2
        status=1
    fi
}

expect_verdict complete accept 'Main.qml
Grid.qml' 'Main 1.0 qml/Main.qml
Grid 1.0 qml/Grid.qml'

expect_verdict omitted_scene reject 'Main.qml
Grid.qml' 'Main 1.0 qml/Main.qml'

expect_verdict renamed_entry reject 'Main.qml
Grid.qml' 'Main 1.0 qml/Main.qml
Tiles 1.0 qml/Tiles.qml'

expect_verdict renamed_file reject 'Main.qml
Grid.qml' 'Main 1.0 qml/Main.qml
Grid 1.0 qml/Tiles.qml'

expect_verdict stale_entry reject 'Main.qml' 'Main 1.0 qml/Main.qml
Grid 1.0 qml/Grid.qml'

expect_verdict absent_manifest reject 'Main.qml' none

expect_verdict empty_corpus reject '' 'Main 1.0 qml/Main.qml'

expect_verdict lowercase_scene reject 'Main.qml
palette.qml' 'Main 1.0 qml/Main.qml
palette 1.0 qml/palette.qml'

if ((status != 0)); then
    exit "$status"
fi

printf 'qml_module_guard_self_test: %d manifest scenarios are enforced\n' \
    "$checked"

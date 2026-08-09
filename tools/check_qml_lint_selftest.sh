#!/usr/bin/env bash

# Proves the QML lint gate still rejects a scene qmllint complains about, and
# refuses a corpus it found no scene in.
#
# The gate's build-ordering check has a self-test of its own; the linting pass
# itself had none. That pass is a loop with a counter, and a loop that runs
# zero times reaches the same success line as one that linted the whole shell -
# which is exactly how the empty-corpus hole in this gate's siblings survived.
#
# Each scenario builds a throwaway repository holding the named scenes, runs
# the real gate inside it against an empty import root, and requires both a
# specific exit status and a specific message.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly guard="$tools_directory/check_qml.sh"

if [[ ! -f "$guard" ]]; then
    printf 'qml_lint_guard_self_test: the gate is missing\n' >&2
    exit 1
fi

# No skip when qmllint is absent. The gate this exercises fails in that case
# rather than skipping, so a self-test that skipped would be reporting a
# healthier tree than the battery it belongs to.
if ! qmllint --version >/dev/null 2>&1; then
    printf 'qml_lint_guard_self_test: qmllint is required, as it is by the gate\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

readonly import_root="$workspace/imports"
mkdir -p "$import_root"

status=0
checked=0

readonly clean_scene='import QtQuick

Item {
    property int measure: 1
}
'

# An unqualified access: qmllint reports it, and it is the smallest fault that
# does not also fail to parse, so the scenario proves the linter ran rather
# than that the file was unreadable.
readonly faulty_scene='import QtQuick

Item {
    property int measure: undefinedOutsideThisFile
}
'

report() {
    local outcome="$1" description="$2"
    if [[ "$outcome" == "pass" ]]; then
        printf '  ok      %s\n' "$description"
    else
        printf '  FAILED  %s\n' "$description" >&2
        status=1
    fi
}

# Builds a repository holding the named scenes, then requires the gate to
# accept or reject it with the stated message. Each argument after the message
# is "RelativePath=clean" or "RelativePath=faulty".
expect_verdict() {
    local description="$1"
    local expectation="$2"
    local fragment="$3"
    shift 3

    checked=$((checked + 1))

    local sandbox="$workspace/sandbox-$checked"
    mkdir -p "$sandbox/app/qml"
    git -C "$sandbox" init -q
    git -C "$sandbox" config core.hooksPath /dev/null

    local specification path kind
    for specification in "$@"; do
        path="${specification%%=*}"
        kind="${specification##*=}"
        mkdir -p "$sandbox/$(dirname "$path")"
        if [[ "$kind" == "faulty" ]]; then
            printf '%s' "$faulty_scene" >"$sandbox/$path"
        else
            printf '%s' "$clean_scene" >"$sandbox/$path"
        fi
    done
    git -C "$sandbox" add -A

    local output=""
    local exit_status=0
    output="$( (cd "$sandbox" && bash "$guard" lint "$import_root") 2>&1 )" ||
        exit_status=$?

    case "$expectation" in
        accept)
            if ((exit_status != 0)); then
                report fail "$description (exit ${exit_status}: ${output})"
                return
            fi
            ;;
        reject)
            if ((exit_status == 0)); then
                report fail "$description (accepted: ${output})"
                return
            fi
            ;;
    esac

    if [[ "$output" != *"$fragment"* ]]; then
        report fail "$description (message did not state ${fragment}: ${output})"
        return
    fi

    report pass "$description"
}

expect_verdict "a scene qmllint accepts passes the gate" \
    accept "1 QML files passed linting" \
    "app/qml/Clean.qml=clean"

expect_verdict "a scene with an unqualified access is rejected" \
    reject "Faulty.qml" \
    "app/qml/Faulty.qml=faulty"

expect_verdict "one faulty scene beside a clean one is still rejected" \
    reject "Faulty.qml" \
    "app/qml/Clean.qml=clean" "app/qml/Faulty.qml=faulty"

# The count in the success line is the corpus, not a constant.
expect_verdict "the success line counts the scenes it linted" \
    accept "2 QML files passed linting" \
    "app/qml/First.qml=clean" "app/qml/Second.qml=clean"

# The floor. Every scene in an empty corpus lints cleanly.
expect_verdict "a corpus holding no scene is refused rather than passed" \
    reject "no tracked QML files found"

if ((status != 0)); then
    printf 'qml_lint_guard_self_test: failed\n' >&2
    exit 1
fi

printf 'qml_lint_guard_self_test: %d scenarios passed\n' "$checked"

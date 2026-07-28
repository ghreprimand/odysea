#!/usr/bin/env bash

set -euo pipefail

readonly mode="${1:-all}"
case "$mode" in
    all | format | lint) ;;
    *)
        printf 'qml_quality_guard: expected all, format, or lint; found %s\n' \
            "$mode" >&2
        exit 2
        ;;
esac

if ! repository_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf 'qml_quality_guard: SKIP (Git metadata unavailable)\n'
    exit 77
fi
cd "$repository_root"

require_qt_tool() {
    local tool_name="$1"
    local tool_version

    if ! tool_version="$("$tool_name" --version 2>/dev/null)"; then
        printf 'qml_quality_guard: %s 6.10 is required\n' "$tool_name" >&2
        exit 1
    fi

    case "$tool_version" in
        "$tool_name 6.10."*) ;;
        *)
            printf 'qml_quality_guard: %s 6.10 is required; found %s\n' \
                "$tool_name" "$tool_version" >&2
            exit 1
            ;;
    esac
}

if [[ "$mode" == "all" || "$mode" == "format" ]]; then
    require_qt_tool qmlformat

    readonly temporary_directory="$(mktemp -d)"
    trap 'rm -rf -- "$temporary_directory"' EXIT

    format_failed=0
    formatted_count=0
    while IFS= read -r -d '' source_file; do
        formatted_file="$temporary_directory/$formatted_count.qml"
        qmlformat --settings "$repository_root/.qmlformat.ini" \
            "$source_file" >"$formatted_file"
        if ! cmp -s "$source_file" "$formatted_file"; then
            printf 'qml_quality_guard: %s is not canonically formatted\n' \
                "$source_file" >&2
            format_failed=1
        fi
        formatted_count=$((formatted_count + 1))
    done < <(git ls-files -z -- '*.qml')

    if ((format_failed != 0)); then
        printf 'qml_quality_guard: run qmlformat -i with .qmlformat.ini\n' >&2
        exit 1
    fi

    if ((formatted_count == 0)); then
        printf 'qml_quality_guard: no tracked QML files found\n' >&2
        exit 1
    fi

    printf 'qml_quality_guard: %d tracked QML files are formatted\n' \
        "$formatted_count"
fi

if [[ "$mode" == "all" || "$mode" == "lint" ]]; then
    require_qt_tool qmllint

    linted_count=0
    while IFS= read -r -d '' source_file; do
        qmllint --ignore-settings --max-warnings 0 "$source_file"
        linted_count=$((linted_count + 1))
    done < <(git ls-files -z -- '*.qml')

    if ((linted_count == 0)); then
        printf 'qml_quality_guard: no tracked QML files found\n' >&2
        exit 1
    fi

    printf 'qml_quality_guard: %d tracked QML files passed linting\n' \
        "$linted_count"
fi

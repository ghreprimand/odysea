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
shift || true

# Remaining arguments are QML import roots. A scene that imports the shell
# module only resolves when the built module directory is on the import path,
# so lint runs against the same module the application links rather than
# against loose source files.
#
# Pathname expansion is disabled for the whole script and roots containing
# whitespace are refused, so the collected flags stay a safe unquoted word list.
set -f
import_flags=""
while (($# > 0)); do
    import_root="$1"
    shift
    case "$import_root" in
        *[[:space:]]*)
            printf 'qml_quality_guard: import path may not contain whitespace\n' \
                >&2
            exit 1
            ;;
    esac
    if [[ ! -d "$import_root" ]]; then
        printf 'qml_quality_guard: import path %s does not exist\n' \
            "$import_root" >&2
        printf 'qml_quality_guard: build the shell module before linting\n' >&2
        exit 1
    fi

    # A module manifest names its type description file. Linting a scene that
    # imports the module fails on the missing description with wording that
    # points at a build path and says nothing about ordering, so the gate states
    # the requirement itself: the module has to be built before it can be linted.
    #
    # A manifest that declares no type descriptions at all — a plugin-only module,
    # or one built with NO_GENERATE_QMLTYPES — passes this check and is left to
    # qmllint, which is the only tool that can judge whether such a module
    # resolves. The check is about ordering, not about module completeness.
    #
    # The scan is not depth-limited. No layout rule fixes how deeply a manifest
    # sits below an import root: a module directory may carry nested submodule or
    # resource manifests of its own, and the shell module already produces a
    # second manifest one level below its own. A depth cap skips those silently,
    # leaving missing type descriptions to surface as a qmllint import warning
    # that names neither the manifest nor the ordering. An import root is a build
    # output directory, so an uncapped scan stays bounded by the build.
    while IFS= read -r manifest; do
        module_directory="${manifest%/qmldir}"
        # The `|| [[ -n ... ]]` continuation reads a final line that carries no
        # trailing newline, which a hand-written or generated manifest may.
        while read -r keyword value remainder || [[ -n "$keyword" ]]; do
            if [[ "$keyword" != "typeinfo" || -n "$remainder" || -z "$value" ]]; then
                continue
            fi
            if [[ ! -f "$module_directory/$value" ]]; then
                printf 'qml_quality_guard: %s declares missing type descriptions %s\n' \
                    "$manifest" "$value" >&2
                printf 'qml_quality_guard: build the shell module before linting\n' >&2
                exit 1
            fi
        done <"$manifest"
    done < <(find "$import_root" -name qmldir -type f)

    import_flags="$import_flags -I $import_root"
done

# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"
guard_corpus_init qml_quality_guard
readonly repository_root="$guard_corpus_root"

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
    done < <(guard_corpus_list '*.qml')

    if ((format_failed != 0)); then
        printf 'qml_quality_guard: run qmlformat -i with .qmlformat.ini\n' >&2
        exit 1
    fi

    if ((formatted_count == 0)); then
        printf 'qml_quality_guard: no tracked QML files found\n' >&2
        exit 1
    fi

    printf 'qml_quality_guard: %d QML files are formatted\n' \
        "$formatted_count"
fi

if [[ "$mode" == "all" || "$mode" == "lint" ]]; then
    require_qt_tool qmllint

    linted_count=0
    while IFS= read -r -d '' source_file; do
        # shellcheck disable=SC2086 # deliberate word split of the import flags
        qmllint --ignore-settings --max-warnings 0 \
            $import_flags "$source_file"
        linted_count=$((linted_count + 1))
    done < <(guard_corpus_list '*.qml')

    if ((linted_count == 0)); then
        printf 'qml_quality_guard: no tracked QML files found\n' >&2
        exit 1
    fi

    printf 'qml_quality_guard: %d QML files passed linting\n' \
        "$linted_count"
fi

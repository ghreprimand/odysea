#!/usr/bin/env bash

# Holds the built QML module to the tracked scene corpus.
#
# The application loads its scenes through the OdySea module, so a scene that
# exists in the working tree but is missing from the module's file list cannot
# be loaded at all. Nothing in a compiler or a QML lint pass notices that: the
# file is still well-formed, it is simply unreachable.
#
# The inputs are therefore derived from the tracked corpus on one side and from
# the module the build actually produced on the other, and the two sets must
# agree exactly. Omitting a scene from the module fails here, and so does a
# module entry that no longer names a tracked scene.

set -euo pipefail

readonly module_directory="${1:-}"
if [[ -z "$module_directory" ]]; then
    printf 'qml_module_guard: expected the built module directory\n' >&2
    exit 2
fi

if ! repository_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf 'qml_module_guard: SKIP (Git metadata unavailable)\n'
    exit 77
fi
cd "$repository_root"

readonly manifest="$module_directory/qmldir"
if [[ ! -f "$manifest" ]]; then
    printf 'qml_module_guard: no module manifest at %s\n' "$manifest" >&2
    printf 'qml_module_guard: build the shell module before running the gate\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

readonly tracked_expectations="$workspace/tracked"
readonly module_entries="$workspace/module"

# Each side is reduced to "TypeName FileName" lines. A scene's type name is its
# file stem, which is how Qt names the type the application instantiates.
status=0
tracked_count=0
: >"$tracked_expectations"
while IFS= read -r scene_path; do
    scene_file="${scene_path##*/}"
    scene_stem="${scene_file%.qml}"
    case "$scene_stem" in
        [A-Z]*) ;;
        *)
            printf 'qml_module_guard: %s cannot be a module type; scenes start with a capital\n' \
                "$scene_path" >&2
            status=1
            continue
            ;;
    esac
    printf '%s %s\n' "$scene_stem" "$scene_file" >>"$tracked_expectations"
    tracked_count=$((tracked_count + 1))
done < <(git ls-files -- 'app/qml/*.qml')

if ((tracked_count == 0)); then
    printf 'qml_module_guard: no tracked scenes found under app/qml\n' >&2
    exit 1
fi

# A manifest component line is "TypeName Version RelativePath"; everything else
# in a qmldir (module, depends, prefer, typeinfo) is not a scene.
: >"$module_entries"
while read -r first second third remainder; do
    if [[ -n "$remainder" || -z "$third" ]]; then
        continue
    fi
    case "$second" in
        [0-9]*.[0-9]*) ;;
        *) continue ;;
    esac
    case "$third" in
        *.qml) ;;
        *) continue ;;
    esac
    printf '%s %s\n' "$first" "${third##*/}" >>"$module_entries"
done <"$manifest"

sort -o "$tracked_expectations" "$tracked_expectations"
sort -o "$module_entries" "$module_entries"

missing="$(comm -23 "$tracked_expectations" "$module_entries")"
if [[ -n "$missing" ]]; then
    printf 'qml_module_guard: tracked scenes are absent from the module\n%s\n' \
        "$missing" >&2
    status=1
fi

unexpected="$(comm -13 "$tracked_expectations" "$module_entries")"
if [[ -n "$unexpected" ]]; then
    printf 'qml_module_guard: module entries name no tracked scene\n%s\n' \
        "$unexpected" >&2
    status=1
fi

if ((status != 0)); then
    printf 'qml_module_guard: update the QML_FILES list in app/CMakeLists.txt\n' >&2
    exit 1
fi

printf 'qml_module_guard: %d tracked scenes are exported by the module\n' \
    "$tracked_count"

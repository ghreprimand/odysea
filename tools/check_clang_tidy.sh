#!/usr/bin/env bash

set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    printf 'usage: %s <build-directory>\n' "$0" >&2
    exit 2
fi

if ! repository_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf 'static_analysis: SKIP (Git metadata unavailable)\n'
    exit 77
fi
cd "$repository_root"

build_directory="$1"
if [[ "$build_directory" != /* ]]; then
    build_directory="$repository_root/$build_directory"
fi
if [[ ! -f "$build_directory/compile_commands.json" ]]; then
    printf 'static_analysis: compilation database missing at %s\n' \
        "$build_directory/compile_commands.json" >&2
    exit 1
fi

tidy_binary="${ODYSEA_CLANG_TIDY:-}"
if [[ -z "$tidy_binary" ]]; then
    tidy_binary="$(command -v clang-tidy || true)"
fi
if [[ -z "$tidy_binary" ]]; then
    common_git_directory="$(git rev-parse --path-format=absolute --git-common-dir)"
    shared_repository_root="$(dirname "$common_git_directory")"
    local_tidy="$shared_repository_root/.archon/tools/clang-tidy-22/bin/clang-tidy"
    if [[ -x "$local_tidy" ]]; then
        tidy_binary="$local_tidy"
    fi
fi
if [[ -z "$tidy_binary" || ! -x "$tidy_binary" ]]; then
    printf 'static_analysis: clang-tidy 22 is required\n' >&2
    exit 1
fi

tidy_version="$("$tidy_binary" --version)"
case "$tidy_version" in
    *"LLVM version 22."*) ;;
    *)
        printf 'static_analysis: clang-tidy 22 is required; found %s\n' \
            "$tidy_version" >&2
        exit 1
        ;;
esac

escaped_repository_root="$(
    printf '%s' "$repository_root" |
        sed 's/[][\\.^$*+?(){}|]/\\&/g'
)"
header_filter="^${escaped_repository_root}/(app|core|tests)/"

checked=0
while IFS= read -r -d '' source_file; do
    "$tidy_binary" -p "$build_directory" --quiet \
        --header-filter="$header_filter" "$source_file"
    checked=$((checked + 1))
done < <(git ls-files -z -- '*.cpp')

printf 'static_analysis: %d tracked translation units passed\n' "$checked"

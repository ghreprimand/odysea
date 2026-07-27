#!/usr/bin/env bash

set -euo pipefail

if ! repository_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf 'formatting_guard: SKIP (Git metadata unavailable)\n'
    exit 77
fi
cd "$repository_root"

if ! format_version="$(clang-format --version 2>/dev/null)"; then
    printf 'formatting_guard: clang-format 22 is required\n' >&2
    exit 1
fi
case "$format_version" in
    *"version 22."*) ;;
    *)
        printf 'formatting_guard: clang-format 22 is required; found %s\n' \
            "$format_version" >&2
        exit 1
        ;;
esac

checked=0
while IFS= read -r -d '' source_file; do
    clang-format --dry-run --Werror "$source_file"
    checked=$((checked + 1))
done < <(git ls-files -z -- '*.cpp' '*.hpp')

printf 'formatting_guard: %d tracked C++ files passed\n' "$checked"

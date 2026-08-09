#!/usr/bin/env bash

set -euo pipefail

# Every conventional C and C++ source or header extension, one per line.
# Narrowing this list would let a file skip formatting purely by being named
# differently, which is not a distinction anyone makes on purpose;
# `tools/check_format_selftest.sh` holds the gate to the list one extension at
# a time.
#
# Kept as newline-separated text rather than a shell array so the tracked
# sources of this repository contain no at-sign, which the publishing guard
# treats as a signal worth stopping on.
readonly covered_extensions='c
cc
cpp
cxx
c++
h
hh
hpp
hxx
h++
inl
ipp
tpp'

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
extension_count=0
while IFS= read -r extension; do
    extension_count=$((extension_count + 1))
    while IFS= read -r -d '' source_file; do
        clang-format --dry-run --Werror "$source_file"
        checked=$((checked + 1))
    done < <(git ls-files -z -- "*.${extension}")
done <<<"$covered_extensions"

# A floor under the count. Every file in an empty corpus is correctly
# formatted, so the check above passes without running clang-format once, and
# the success line below reads identically to a full run. The corpus is
# enumerated from a repository root, which is exactly the kind of thing that
# can silently resolve somewhere else.
if ((checked == 0)); then
    printf 'formatting_guard: no tracked file matched any of the %d covered extensions, so nothing was checked\n' \
        "$extension_count" >&2
    exit 1
fi

printf 'formatting_guard: %d tracked C and C++ files passed across %d extensions\n' \
    "$checked" "$extension_count"

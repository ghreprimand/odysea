#!/usr/bin/env bash

# Proves the formatting gate actually rejects unformatted code, one file
# extension at a time.
#
# A gate that silently checks nothing looks exactly like a gate that passes, so
# this builds a throwaway repository per required extension, puts a single
# deliberately unformatted file in it, and requires the gate to fail. It then
# formats that same file and requires the gate to pass, so the check cannot be
# satisfied by a gate that simply always fails.
#
# Everything happens in a temporary directory that is removed afterwards. No
# unformatted fixture is ever tracked here, which would otherwise leave the gate
# failing against its own test data.

set -euo pipefail

# Stated independently of the gate on purpose. Reading the list out of the gate
# would make this test agree with whatever the gate happens to say, including a
# gate that quietly stopped covering something. This is the requirement; the
# gate has to meet it.
readonly required_extensions='c
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
    printf 'formatting_guard_self_test: SKIP (Git metadata unavailable)\n'
    exit 77
fi

readonly guard="$repository_root/tools/check_format.sh"
readonly style="$repository_root/.clang-format"

if [[ ! -f "$guard" || ! -f "$style" ]]; then
    printf 'formatting_guard_self_test: the gate or its style file is missing\n' >&2
    exit 1
fi

if ! clang-format --version >/dev/null 2>&1; then
    printf 'formatting_guard_self_test: SKIP (clang-format unavailable)\n'
    exit 77
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

readonly unformatted='int  main( ){return 0 ;}'

status=0
checked=0
while IFS= read -r extension; do
    checked=$((checked + 1))
    sandbox="$workspace/$extension"
    mkdir -p "$sandbox"
    cp "$style" "$sandbox/.clang-format"
    git -C "$sandbox" init -q
    git -C "$sandbox" config core.hooksPath /dev/null

    sample="$sandbox/sample.$extension"
    printf '%s\n' "$unformatted" >"$sample"
    git -C "$sandbox" add -f ".clang-format" "sample.$extension"

    if (cd "$sandbox" && bash "$guard" >/dev/null 2>&1); then
        printf 'formatting_guard_self_test: unformatted .%s is not rejected\n' \
            "$extension" >&2
        status=1
        continue
    fi

    clang-format -i "$sample"
    git -C "$sandbox" add "sample.$extension"
    if ! (cd "$sandbox" && bash "$guard" >/dev/null 2>&1); then
        printf 'formatting_guard_self_test: formatted .%s is not accepted\n' \
            "$extension" >&2
        status=1
    fi
done <<<"$required_extensions"

if ((status != 0)); then
    exit "$status"
fi

printf 'formatting_guard_self_test: %d extensions are checked and enforced\n' \
    "$checked"

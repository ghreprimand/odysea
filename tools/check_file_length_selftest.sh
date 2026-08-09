#!/usr/bin/env bash

# Proves the file-length ceiling still rejects what it exists to reject.
#
# This gate had no self-test, and it was one of exactly two that reported
# success over an empty corpus. The two facts are related: a gate whose
# behaviour nothing exercises is a gate whose behaviour nobody notices
# changing. Each scenario below builds a throwaway repository, runs the real
# gate inside it, and requires both a specific exit status and a specific
# message, so an always-failing gate fails the accepting scenarios and a gate
# that fails for an unrelated reason fails the message check.
#
# The boundary cases are exact on purpose. A ceiling that is one line out is
# indistinguishable from a correct one on every file that is not sitting on it.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly guard="$tools_directory/check_file_length.sh"
readonly max_lines=2000

if [[ ! -f "$guard" ]]; then
    printf 'file_length_guard_self_test: the gate is missing\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

status=0
checked=0

# Creates an empty throwaway repository and prints its path.
new_repository() {
    local name="$1"
    local sandbox="$workspace/$name"

    mkdir -p "$sandbox"
    git -C "$sandbox" init -q
    git -C "$sandbox" config core.hooksPath /dev/null
    printf '%s' "$sandbox"
}

# Writes a file of exactly the requested number of lines.
write_lines() {
    local path="$1"
    local count="$2"

    : >"$path"
    if ((count > 0)); then
        seq "$count" >"$path"
    fi
}

report() {
    local outcome="$1" description="$2"
    if [[ "$outcome" == "pass" ]]; then
        printf '  ok      %s\n' "$description"
    else
        printf '  FAILED  %s\n' "$description" >&2
        status=1
    fi
}

# Runs the gate in the given repository and requires the stated outcome and the
# stated message. Keying on the exit status rather than on the presence of a
# complaint keeps a crash from reading as a rejection.
expect_verdict() {
    local description="$1"
    local expectation="$2"
    local fragment="$3"
    local sandbox="$4"

    checked=$((checked + 1))

    local output=""
    local exit_status=0
    output="$( (cd "$sandbox" && bash "$guard") 2>&1 )" || exit_status=$?

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

# --- The boundary, from both sides -----------------------------------------
sandbox="$(new_repository at-the-ceiling)"
write_lines "$sandbox/at-the-ceiling.txt" "$max_lines"
git -C "$sandbox" add -A
expect_verdict "a file of exactly ${max_lines} lines is accepted" \
    accept "passed the ${max_lines}-line ceiling" "$sandbox"

sandbox="$(new_repository over-the-ceiling)"
write_lines "$sandbox/over-the-ceiling.txt" "$((max_lines + 1))"
git -C "$sandbox" add -A
expect_verdict "a file of ${max_lines} plus one lines is rejected" \
    reject "has $((max_lines + 1)) lines" "$sandbox"

# --- Each side of the index is measured separately ---------------------------
# A file can be over the ceiling in the working tree and under it in the index,
# or the reverse. Only one of the two messages may appear in each case, so the
# scenarios discriminate which half of the gate did the work.
sandbox="$(new_repository over-in-the-working-tree)"
write_lines "$sandbox/subject.txt" 10
git -C "$sandbox" add -A
write_lines "$sandbox/subject.txt" "$((max_lines + 1))"
expect_verdict "an oversized working-tree file is rejected while the index is clean" \
    reject "working-tree file" "$sandbox"

sandbox="$(new_repository over-in-the-index)"
write_lines "$sandbox/subject.txt" "$((max_lines + 1))"
git -C "$sandbox" add -A
write_lines "$sandbox/subject.txt" 10
expect_verdict "an oversized indexed file is rejected while the working tree is clean" \
    reject "indexed file" "$sandbox"

# --- Floors: a gate that measured nothing has established nothing ------------
sandbox="$(new_repository empty-corpus)"
expect_verdict "an empty corpus is refused rather than passed" \
    reject "the tracked corpus is empty" "$sandbox"

sandbox="$(new_repository binary-corpus)"
printf '\000\001\002\003' >"$sandbox/asset.bin"
git -C "$sandbox" add -A
expect_verdict "a corpus holding no text file is refused rather than passed" \
    reject "held no text file" "$sandbox"

# --- The success line states what was measured -------------------------------
# Two repositories differing only in size must not print the same success line,
# or the line carries no information about whether the gate ran.
one_file_repository="$(new_repository one-tracked-file)"
write_lines "$one_file_repository/only.txt" 3
git -C "$one_file_repository" add -A

two_file_repository="$(new_repository two-tracked-files)"
write_lines "$two_file_repository/first.txt" 3
write_lines "$two_file_repository/second.txt" 3
git -C "$two_file_repository" add -A

checked=$((checked + 1))
one_file_line="$( (cd "$one_file_repository" && bash "$guard") 2>&1 )"
two_file_line="$( (cd "$two_file_repository" && bash "$guard") 2>&1 )"
if [[ "$one_file_line" == "$two_file_line" ]]; then
    report fail "the success line distinguishes a one-file corpus from a two-file one"
else
    report pass "the success line distinguishes a one-file corpus from a two-file one"
fi

if ((status != 0)); then
    printf 'file_length_guard_self_test: failed\n' >&2
    exit 1
fi

printf 'file_length_guard_self_test: %d scenarios passed\n' "$checked"

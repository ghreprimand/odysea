#!/usr/bin/env bash

# Proves the public-repository guard still rejects an address in a shell file
# after shell expansion syntax was carved out of the at-sign ban.
#
# A carve-out with no negative test is a hole with a comment on it: the guard
# would keep reporting success, and nothing would reveal that the exception had
# swallowed the rule. Each scenario below builds a throwaway repository, stages
# a file, and runs the real guard against it, so the check measures the guard's
# behaviour rather than the text of its patterns.
#
# The planted at-sign byte is composed at run time rather than written out.
# This file is scanned by the guard like any other tracked shell source, and an
# address written literally here would have to be excused by exactly the kind
# of file-level exclusion the carve-out exists to avoid. The legitimate
# expansion forms below are written literally, because the carve-out permits
# them: this file's own tracked form is part of what it demonstrates.

set -euo pipefail

if ! repository_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf 'public_repository_guard_self_test: SKIP (Git metadata unavailable)\n'
    exit 77
fi

readonly guard="$repository_root/tools/check_public_repo.sh"
if [[ ! -f "$guard" ]]; then
    printf 'public_repository_guard_self_test: the gate is missing\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

readonly at="$(printf '\100')"
readonly owner_identity="1+owner${at}users.noreply.github.com"
readonly planted_address="admin${at}host.example"

readonly address_message='email-like or user-at-host text is tracked in a shell file'
readonly success_message='tracked content passed'

status=0
checked=0

# Builds a throwaway repository holding one shell file with the given body,
# committed under an identity the guard accepts, so that a failure can only
# come from the corpus scan under test.
build_repository() {
    local name="$1"
    local body="$2"

    local root="$workspace/$name"
    mkdir -p "$root"
    git init -q "$root"
    git -C "$root" config user.name owner
    git -C "$root" config user.email "$owner_identity"
    git -C "$root" config commit.gpgsign false

    printf '%s\n' "$body" >"$root/script.sh"
    git -C "$root" add script.sh
    git -C "$root" -c core.hooksPath=/dev/null commit -q -m 'Add a script'

    printf '%s' "$root"
}

# Runs the guard inside one throwaway repository and requires it to accept the
# corpus or to reject it for the address reason specifically. Requiring the
# reason keeps a scenario from passing because some unrelated check fired.
expect_outcome() {
    local scenario="$1"
    local expectation="$2"
    local root="$3"

    local output=""
    local exit_status=0
    output="$(cd "$root" && bash "$guard" 2>&1)" || exit_status=$?

    checked=$((checked + 1))
    case "$expectation" in
        accept)
            if ((exit_status != 0)) || [[ "$output" != *"$success_message"* ]]; then
                printf 'public_repository_guard_self_test: %s should be accepted, but the guard said: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            fi
            ;;
        reject)
            if ((exit_status == 0)); then
                printf 'public_repository_guard_self_test: %s should be rejected\n' \
                    "$scenario" >&2
                status=1
            elif [[ "$output" != *"$address_message"* ]]; then
                printf 'public_repository_guard_self_test: %s should be rejected for the address, but the guard said: %s\n' \
                    "$scenario" "$output" >&2
                status=1
            fi
            ;;
    esac
}

# The permitted forms, one scenario each. These are the idioms the ban had
# driven contributors away from.
expect_outcome array_at_subscript accept \
    "$(build_repository array_at 'printf "%s\n" "${entries[@]}"')"

expect_outcome array_length accept \
    "$(build_repository array_length 'printf "%d\n" "${#entries[@]}"')"

expect_outcome quoted_positional accept \
    "$(build_repository quoted_positional 'printf "%s\n" "$@"')"

expect_outcome unquoted_positional accept \
    "$(build_repository unquoted_positional 'set -- a b; printf "%s\n" $@')"

expect_outcome braced_positional accept \
    "$(build_repository braced_positional 'printf "%s\n" "${@}"')"

expect_outcome braced_positional_slice accept \
    "$(build_repository braced_slice 'printf "%s\n" "${@:2}"')"

# The negative direction. An address in a shell file must still be rejected,
# and the carve-out must not have turned the ban into a formality.
expect_outcome planted_address reject \
    "$(build_repository planted "# contact ${planted_address}")"

# The case the carve-out could most plausibly have broken: an address sharing a
# line with a legitimate expansion. A scan that stopped at the first permitted
# form, or that excused the whole line or the whole file, would miss this.
expect_outcome address_beside_expansion reject \
    "$(build_repository beside "printf '%s\n' \"\${entries[@]}\" # ${planted_address}")"

# The same pairing across separate lines of one file: the permitted forms are
# accepted and the address is still reported, in a single run over a single
# corpus. A carve-out that pardoned a file once any permitted form appeared in
# it would pass this file while the address sat two lines below.
expect_outcome address_and_expansion_in_one_file reject \
    "$(build_repository one_file "printf '%s\n' \"\${entries[@]}\"
printf '%s\n' \"\$@\"
# contact ${planted_address}")"

# An address in a non-shell file is unaffected by the carve-out and stays
# banned by the blanket rule.
non_shell_root="$workspace/non_shell"
mkdir -p "$non_shell_root"
git init -q "$non_shell_root"
git -C "$non_shell_root" config user.name owner
git -C "$non_shell_root" config user.email "$owner_identity"
git -C "$non_shell_root" config commit.gpgsign false
printf 'contact %s\n' "$planted_address" >"$non_shell_root/NOTES.md"
git -C "$non_shell_root" add NOTES.md
git -C "$non_shell_root" -c core.hooksPath=/dev/null commit -q -m 'Add notes'

non_shell_output=""
non_shell_status=0
non_shell_output="$(cd "$non_shell_root" && bash "$guard" 2>&1)" || non_shell_status=$?
checked=$((checked + 1))
if ((non_shell_status == 0)) ||
    [[ "$non_shell_output" != *"email-like or user-at-host text is tracked"* ]]; then
    printf 'public_repository_guard_self_test: an address in a non-shell file should still be rejected\n' >&2
    status=1
fi

if ((status != 0)); then
    exit "$status"
fi

printf 'public_repository_guard_self_test: %d at-sign scenarios are enforced\n' \
    "$checked"

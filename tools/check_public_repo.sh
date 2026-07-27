#!/usr/bin/env bash

set -euo pipefail

repository_root="$(git rev-parse --show-toplevel)"
cd "$repository_root"

failed=0

report_matches() {
    local label="$1"
    shift

    local matches
    if matches="$("$@" 2>/dev/null)"; then
        printf 'public_repository_guard: %s\n%s\n' "$label" "$matches" >&2
        failed=1
    fi
}

sensitive_paths="$(
    git ls-files --cached |
        grep -E '(^|/)(\.env($|\.)|\.aws/|\.credentials/|\.gnupg/|\.netrc$|\.npmrc$|\.pypirc$|\.secrets/|\.ssh/|credentials([./]|$)|secrets([./]|$)|id_(dsa|ecdsa|ed25519|rsa)|[^/]+\.(age|enc|gpg|key|kdbx|p12|pem|pfx|ppk)$)' ||
        true
)"
if [[ -n "$sensitive_paths" ]]; then
    printf 'public_repository_guard: sensitive path names are tracked\n%s\n' \
        "$sensitive_paths" >&2
    failed=1
fi

public_pathspec=(
    .
    ':(exclude)tools/check_public_repo.sh'
)

report_matches "email-like or user-at-host text is tracked" \
    git grep --cached -nI -E '@' -- "${public_pathspec[@]}"

report_matches "personal home-directory path is tracked" \
    git grep --cached -nI -E '(/home/[^/[:space:]]+|/Users/[^/[:space:]]+|[A-Za-z]:\\Users\\[^\\[:space:]]+)' \
    -- "${public_pathspec[@]}"

report_matches "private-key or common access-token signature is tracked" \
    git grep --cached -nI -E \
    '(-----BEGIN ([A-Z0-9]+ )?PRIVATE KEY-----|AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9]{20,}|xox[baprs]-[A-Za-z0-9-]{10,})' \
    -- "${public_pathspec[@]}"

if ((failed != 0)); then
    exit 1
fi

printf 'public_repository_guard: tracked content passed\n'

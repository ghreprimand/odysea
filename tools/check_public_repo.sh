#!/usr/bin/env bash

set -euo pipefail

if ! repository_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf 'public_repository_guard: SKIP (Git metadata unavailable)\n'
    exit 77
fi
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

self_excluding_pathspec=(
    .
    ':(exclude)tools/check_public_repo.sh'
)

report_matches "email-like or user-at-host text is tracked" \
    git grep --cached -nI -E '@' -- "${self_excluding_pathspec[@]}"

report_matches "personal home-directory path is tracked" \
    git grep --cached -nI -E '(/home/[^/[:space:]]+|/Users/[^/[:space:]]+|[A-Za-z]:\\Users\\[^\\[:space:]]+)' \
    -- "${self_excluding_pathspec[@]}"

report_matches "private-key or common access-token signature is tracked" \
    git grep --cached -nI -E \
    '(-----BEGIN ([A-Z0-9]+ )?PRIVATE[[:space:]]KEY-----|AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9]{20,}|xox[baprs]-[A-Za-z0-9-]{10,})' \
    -- .

report_matches "private-network address or local-only transport is tracked" \
    git grep --cached -nI -E \
    '(ssh://|git@|https?://[^/[:space:]]+\.local([/:]|$)|(^|[^0-9])10\.([0-9]{1,3}\.){2}[0-9]{1,3}([^0-9]|$)|(^|[^0-9])192\.168\.[0-9]{1,3}\.[0-9]{1,3}([^0-9]|$)|(^|[^0-9])172\.(1[6-9]|2[0-9]|3[01])\.[0-9]{1,3}\.[0-9]{1,3}([^0-9]|$))' \
    -- "${self_excluding_pathspec[@]}" ':(exclude).gitignore'

report_matches "internal workflow narration is tracked" \
    git grep --cached -nI -E \
    '(the operator|the user (asked|requested|wanted)|per operator|Director role|Builder [0-9]|agent workflow|work packet|peer_send|audit[- ]round)' \
    -- "${self_excluding_pathspec[@]}"

unsafe_identity_commits=""
while IFS=$'\t' read -r commit author_email committer_email; do
    case "$author_email" in
        *@users.noreply.github.com | noreply@*) ;;
        *) unsafe_identity_commits+="${commit}"$'\n' ;;
    esac
    case "$committer_email" in
        *@users.noreply.github.com | noreply@*) ;;
        *) unsafe_identity_commits+="${commit}"$'\n' ;;
    esac
done < <(git log --format='%H%x09%ae%x09%ce')
if [[ -n "$unsafe_identity_commits" ]]; then
    printf 'public_repository_guard: commits use non-no-reply attribution\n%s' \
        "$unsafe_identity_commits" >&2
    failed=1
fi

coauthored_commits=""
while IFS= read -r commit; do
    if git show -s --format=%B "$commit" | grep -q '^Co-Authored-By:'; then
        coauthored_commits+="${commit}"$'\n'
    fi
done < <(git log --format=%H)
if [[ -n "$coauthored_commits" ]]; then
    printf 'public_repository_guard: Co-Authored-By trailers are present\n%s' \
        "$coauthored_commits" >&2
    failed=1
fi

if ((failed != 0)); then
    exit 1
fi

printf 'public_repository_guard: tracked content passed\n'

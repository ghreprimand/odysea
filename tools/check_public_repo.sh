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

# The attribution enforcement sources are excluded from the corpus scans below
# for the same reason this script is: their subject matter is the shape of a
# commit identity, so they necessarily contain address syntax and would
# otherwise trip the blanket at-sign rule they exist to support. They are
# reviewed as enforcement code, and no other tracked file may contain an
# at-sign.
self_excluding_pathspec=(
    .
    ':(exclude)tools/check_public_repo.sh'
    ':(exclude)tools/check_hooks_selftest.sh'
    ':(exclude)tools/hooks/*'
)

# Shell expansion syntax requires the at-sign, so the ban on it is lifted for
# those forms alone rather than for the files that contain them. The permitted
# forms are the array-at subscript inside a parameter expansion, the braced
# positional form, and the bare positional form, which covers both the quoted
# and unquoted spellings because the quotes sit outside the token.
#
# A file-level exclusion was rejected. Excluding a shell file wholesale would
# stop the guard reading the rest of it, and the exclusion list would grow one
# entry per script until the ban existed only in the comment describing it. A
# syntax-level carve-out keeps every other at-sign in those files banned: a
# line is scanned again after the permitted forms are removed, so an address
# sitting beside a legitimate expansion on the same line is still caught.
#
# The alternation is written with bracket expressions because a brace carries
# interval meaning in an extended regular expression.
permitted_shell_expansion_re='[$][{]#?[A-Za-z_][A-Za-z0-9_]*[[]@[]]|[$][{]@[^}]*[}]|[$]@'

report_matches "email-like or user-at-host text is tracked" \
    git grep --cached -nI -E '@' -- "${self_excluding_pathspec[@]}" \
    ':(exclude)*.sh'

# Shell sources are scanned with the permitted expansion forms removed first.
# Anything still holding an at-sign afterwards is text, not syntax.
shell_at_sign_matches="$(
    git grep --cached -nI -E '@' -- '*.sh' \
        ':(exclude)tools/check_public_repo.sh' \
        ':(exclude)tools/check_hooks_selftest.sh' 2>/dev/null |
        sed -E "s/${permitted_shell_expansion_re}//g" |
        grep -E '@' || true
)"
if [[ -n "$shell_at_sign_matches" ]]; then
    printf 'public_repository_guard: email-like or user-at-host text is tracked in a shell file\n%s\n' \
        "$shell_at_sign_matches" >&2
    failed=1
fi

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

# Bare process-role vocabulary. Words with legitimate engineering meanings are
# deliberately absent from this list and matched only in the phrase forms
# above: "worker" names thread-pool members throughout core, "builder" names
# the builder pattern, and bare "operator" is the C++ keyword in every
# operator=, operator==, and operator() declaration. "director" does not
# collide with "directory": the word boundary requires the token to end.
# `.gitignore` is excluded because it legitimately names ignored files.
report_matches "process-role vocabulary is tracked" \
    git grep --cached -nI -iE \
    '\b(reviewers?|directors?|verdicts?|packets?|agents?|subagents?|orchestrators?|coordinators?|the assignee|sign[- ]?off)\b' \
    -- "${self_excluding_pathspec[@]}" ':(exclude).gitignore'

report_matches "process-adjacent operator phrasing is tracked" \
    git grep --cached -nI -iE \
    "\\boperator'?s? (approval|approved|asked|decision|instruction|request|review)" \
    -- "${self_excluding_pathspec[@]}"

# Attribution must use the account-scoped GitHub no-reply form
# <numeric-account-id>+<login>@users.noreply.github.com. Requiring the numeric
# account prefix is what makes the check meaningful: a bare local part such as
# a project or role name is still syntactically a no-reply address, but GitHub
# resolves it to whichever unrelated account happens to own that login. Only
# the account-scoped form is guaranteed to map to this repository's owner.
owner_identity_re='^[0-9]+\+[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?@users\.noreply\.github\.com$'

unsafe_identity_commits=""
observed_identities=""
while IFS=$'\t' read -r commit author_email committer_email; do
    if [[ ! "$author_email" =~ $owner_identity_re ]] ||
        [[ ! "$committer_email" =~ $owner_identity_re ]]; then
        unsafe_identity_commits+="${commit}"$'\n'
    fi
    observed_identities+="${author_email}"$'\n'"${committer_email}"$'\n'
done < <(git log --format='%H%x09%ae%x09%ce')
if [[ -n "$unsafe_identity_commits" ]]; then
    printf 'public_repository_guard: commits are not attributed to an account-scoped no-reply identity\n%s' \
        "$unsafe_identity_commits" >&2
    failed=1
fi

# Every commit must carry the same identity. A second well-formed identity is
# still a second published contributor, which is the outcome this guards.
distinct_identities="$(printf '%s' "$observed_identities" | sort -u | grep -c .)"
if ((distinct_identities > 1)); then
    printf 'public_repository_guard: history carries %s distinct commit identities; expected exactly one\n' \
        "$distinct_identities" >&2
    failed=1
fi

# Machine-generated collaboration trailers must never reach published history.
# The match is case-insensitive and covers the trailers emitted by common
# coding agents; any of them would publish a second contributor on the commit.
attribution_trailer_re='^[[:space:]]*(co-authored-by|signed-off-by|assisted-by|generated-by|created-by|authored-by|on-behalf-of):'
trailer_commits=""
while IFS= read -r commit; do
    if git show -s --format=%B "$commit" | grep -qiE "$attribution_trailer_re"; then
        trailer_commits+="${commit}"$'\n'
    fi
done < <(git log --format=%H)
if [[ -n "$trailer_commits" ]]; then
    printf 'public_repository_guard: attribution trailers are present\n%s' \
        "$trailer_commits" >&2
    failed=1
fi

if ((failed != 0)); then
    exit 1
fi

printf 'public_repository_guard: tracked content passed\n'

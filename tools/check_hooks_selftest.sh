#!/usr/bin/env bash
#
# Prove the attribution hooks still reject what they exist to reject.
#
# A hook that silently stops biting is worse than no hook, because it is
# trusted. Each case below is exercised against a real throwaway repository
# using the tracked hooks, so the check measures behaviour rather than text.
set -euo pipefail

repository_root="$(git rev-parse --show-toplevel)"
hooks_directory="${repository_root}/tools/hooks"

owner_identity="1+owner@users.noreply.github.com"
other_identity="2+other@users.noreply.github.com"
unscoped_identity="project@users.noreply.github.com"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

git init -q "$scratch"
git -C "$scratch" config core.hooksPath "$hooks_directory"
git -C "$scratch" config user.name owner
git -C "$scratch" config user.email "$owner_identity"

failures=0

report() {
    local outcome="$1" description="$2"
    if [[ "$outcome" == "pass" ]]; then
        printf '  ok      %s\n' "$description"
    else
        printf '  FAILED  %s\n' "$description" >&2
        failures=1
    fi
}

# A commit that must be accepted, establishing the baseline identity.
echo one >"${scratch}/file.txt"
git -C "$scratch" add -A
if git -C "$scratch" commit -qm "baseline commit" 2>/dev/null; then
    report pass "an owner-attributed commit is accepted"
else
    report fail "an owner-attributed commit is accepted"
fi

# An unscoped no-reply address resolves to an unrelated account.
echo two >"${scratch}/file.txt"
git -C "$scratch" add -A
if git -C "$scratch" -c user.email="$unscoped_identity" commit -qm "unscoped" 2>/dev/null; then
    report fail "an unscoped no-reply identity is rejected"
    git -C "$scratch" reset -q --hard HEAD~1
else
    report pass "an unscoped no-reply identity is rejected"
fi

# A well-formed identity belonging to a different account.
if git -C "$scratch" -c user.email="$other_identity" commit -qm "other owner" 2>/dev/null; then
    report fail "a second account-scoped identity is rejected"
    git -C "$scratch" reset -q --hard HEAD~1
else
    report pass "a second account-scoped identity is rejected"
fi

# Agent trailers, which are the default output of several coding tools.
for trailer in "Co-Authored-By: Agent <agent@example.invalid>" \
    "Assisted-by: Agent" \
    "Generated-by: Agent"; do
    if git -C "$scratch" commit -qm "$(printf 'subject\n\n%s' "$trailer")" 2>/dev/null; then
        report fail "the ${trailer%%:*} trailer is rejected"
        git -C "$scratch" reset -q --hard HEAD~1
    else
        report pass "the ${trailer%%:*} trailer is rejected"
    fi
done

# A trailer named inside a stripped comment line must not trip the check.
if git -C "$scratch" commit -qm "$(printf 'subject\n\n# Co-Authored-By: mentioned in a comment')" 2>/dev/null; then
    report pass "a trailer inside a comment line is ignored"
    git -C "$scratch" reset -q --hard HEAD~1
else
    report fail "a trailer inside a comment line is ignored"
fi

# The configured hook path must resolve from a linked worktree and from a
# subdirectory, not only from the repository root. Git resolves a relative
# core.hooksPath against the current working directory, so a relative value
# leaves every worktree unprotected while still looking configured.
configured_hooks_path="$(git config --get core.hooksPath || true)"
if [[ -z "$configured_hooks_path" ]]; then
    report fail "core.hooksPath is configured (run tools/install_hooks.sh)"
elif [[ "$configured_hooks_path" != /* ]]; then
    report fail "core.hooksPath is absolute so it resolves from any directory"
else
    report pass "core.hooksPath is absolute so it resolves from any directory"

    for required in pre-commit commit-msg pre-push; do
        if [[ -x "${configured_hooks_path}/${required}" ]]; then
            report pass "the configured ${required} hook is present and executable"
        else
            report fail "the configured ${required} hook is present and executable"
        fi
    done

    # Prove enforcement actually reaches a linked worktree.
    worktree="${scratch}-worktree"
    if git -C "$scratch" worktree add -q -b selftest-probe "$worktree" >/dev/null 2>&1; then
        echo probe >"${worktree}/probe.txt"
        git -C "$worktree" add -A
        if git -C "$worktree" -c user.email="$unscoped_identity" \
            commit -qm "unscoped identity in a worktree" 2>/dev/null; then
            report fail "an unscoped identity is rejected inside a linked worktree"
        else
            report pass "an unscoped identity is rejected inside a linked worktree"
        fi
        git -C "$scratch" worktree remove --force "$worktree" >/dev/null 2>&1 || true
        git -C "$scratch" branch -D selftest-probe >/dev/null 2>&1 || true
    else
        report fail "a linked worktree can be created for the enforcement probe"
    fi
fi

if ((failures != 0)); then
    echo "hooks_selftest: failed" >&2
    exit 1
fi

echo "hooks_selftest: passed"

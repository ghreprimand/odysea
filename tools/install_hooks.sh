#!/usr/bin/env bash
#
# Point this checkout's Git hooks at the tracked hook directory.
#
# Hooks live under .git/, which is never cloned, so a fresh checkout starts
# with no enforcement at all. Running this once per clone activates the tracked
# hooks.
#
# The configured path is absolute on purpose. Git resolves a relative
# `core.hooksPath` against the current working directory rather than the
# repository root, so a relative value silently finds nothing whenever work
# happens in a linked worktree or any subdirectory — the hooks appear
# configured while enforcing nothing. Because `core.hooksPath` is stored in the
# shared repository config, one absolute value covers the main checkout and
# every linked worktree.
set -euo pipefail

repository_root="$(git rev-parse --show-toplevel)"
# In a linked worktree, --show-toplevel is that worktree. Resolve the main
# checkout so the hook path never points inside a worktree that will be removed.
common_directory="$(cd "$(git rev-parse --git-common-dir)" && pwd -P)"
main_checkout="$(dirname "$common_directory")"

hooks_directory="${main_checkout}/tools/hooks"

if [[ ! -d "$hooks_directory" ]]; then
    echo "install_hooks: ${hooks_directory} is missing" >&2
    exit 1
fi

chmod +x "${hooks_directory}"/*

git -C "$repository_root" config core.hooksPath "$hooks_directory"

echo "install_hooks: hooks enabled from ${hooks_directory}"
for hook in "${hooks_directory}"/*; do
    echo "  $(basename "$hook")"
done

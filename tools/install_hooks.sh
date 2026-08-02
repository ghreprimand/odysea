#!/usr/bin/env bash
#
# Point this checkout's Git hooks at the tracked hook directory.
#
# Hooks live under .git/, which is never cloned, so a fresh checkout starts
# with no enforcement at all. Running this once per clone activates the tracked
# hooks; `core.hooksPath` is repository-local configuration, so it applies to
# this checkout and every worktree attached to it.
set -euo pipefail

repository_root="$(git rev-parse --show-toplevel)"
hooks_directory="tools/hooks"

if [[ ! -d "${repository_root}/${hooks_directory}" ]]; then
    echo "install_hooks: ${hooks_directory} is missing" >&2
    exit 1
fi

chmod +x "${repository_root}/${hooks_directory}"/*

git -C "$repository_root" config core.hooksPath "$hooks_directory"

echo "install_hooks: hooks enabled from ${hooks_directory}"
for hook in "${repository_root}/${hooks_directory}"/*; do
    echo "  $(basename "$hook")"
done

#!/usr/bin/env bash

# Proves the lint gate's build-ordering check rejects the manifests it exists to
# catch and lets the rest through.
#
# The check reads every module manifest below an import root and fails when a
# manifest declares type descriptions that have not been built yet. Its whole
# value is the wording: without it the same fault surfaces as a qmllint import
# warning that names neither the manifest nor the ordering, so a check that
# silently scanned nothing would be indistinguishable from a passing one.
#
# Each scenario builds a throwaway import root and requires a specific verdict.
# The scenarios run from a directory outside any repository, so a root the check
# accepts stops at the gate's own Git-metadata skip: reaching that skip proves
# the scan ran to completion and passed the root rather than exiting early.

set -euo pipefail

if ! repository_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
    printf 'qml_lint_order_self_test: SKIP (Git metadata unavailable)\n'
    exit 77
fi

readonly guard="$repository_root/tools/check_qml.sh"
if [[ ! -f "$guard" ]]; then
    printf 'qml_lint_order_self_test: the gate is missing\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

if git -C "$workspace" rev-parse --show-toplevel >/dev/null 2>&1; then
    printf 'qml_lint_order_self_test: the scratch directory is inside a repository\n' >&2
    exit 1
fi

readonly ordering_message='declares missing type descriptions'
status=0
checked=0

# Runs the gate against one throwaway import root and requires it to accept the
# root or to reject the named manifest.
expect_verdict() {
    local scenario="$1"
    local expectation="$2"
    local root="$3"
    local manifest="${4:-}"

    local output=""
    local exit_status=0
    output="$(cd "$workspace" && bash "$guard" lint "$root" 2>&1)" || exit_status=$?

    checked=$((checked + 1))
    case "$expectation" in
        accept)
            if [[ "$output" == *"$ordering_message"* ]]; then
                printf 'qml_lint_order_self_test: %s should be accepted\n' \
                    "$scenario" >&2
                status=1
            elif ((exit_status != 77)); then
                printf 'qml_lint_order_self_test: %s did not reach the end of the scan\n' \
                    "$scenario" >&2
                status=1
            fi
            ;;
        reject)
            if ((exit_status != 1)) || [[ "$output" != *"$ordering_message"* ]]; then
                printf 'qml_lint_order_self_test: %s should be rejected\n' \
                    "$scenario" >&2
                status=1
            elif [[ -n "$manifest" && "$output" != *"$manifest"* ]]; then
                printf 'qml_lint_order_self_test: %s should name %s\n' \
                    "$scenario" "$manifest" >&2
                status=1
            fi
            ;;
    esac
}

# Writes a manifest, creating its directory. The final argument selects whether
# the manifest ends in a newline.
write_manifest() {
    local path="$1"
    local body="$2"
    local terminator="$3"

    mkdir -p "${path%/qmldir}"
    if [[ "$terminator" == "terminated" ]]; then
        printf '%s\n' "$body" >"$path"
    else
        printf '%s' "$body" >"$path"
    fi
}

# A manifest whose declared type descriptions exist is the built state the gate
# is waiting for.
built_root="$workspace/built/root"
write_manifest "$built_root/Mod/qmldir" \
    'module Mod
typeinfo mod.qmltypes' terminated
touch "$built_root/Mod/mod.qmltypes"
expect_verdict built accept "$built_root"

# The same manifest without the file it names is the configure-only tree.
unbuilt_root="$workspace/unbuilt/root"
write_manifest "$unbuilt_root/Mod/qmldir" \
    'module Mod
typeinfo mod.qmltypes' terminated
expect_verdict unbuilt reject "$unbuilt_root" "$unbuilt_root/Mod/qmldir"

# A generated or hand-written manifest may end without a trailing newline. The
# declaration on that final line still counts.
unterminated_root="$workspace/unterminated/root"
write_manifest "$unterminated_root/Mod/qmldir" \
    'module Mod
typeinfo mod.qmltypes' unterminated
expect_verdict unterminated reject "$unterminated_root" \
    "$unterminated_root/Mod/qmldir"

# A module that declares no type descriptions has nothing to wait for. Whether
# it resolves is qmllint's judgement, not this check's.
plugin_root="$workspace/plugin/root"
write_manifest "$plugin_root/Mod/qmldir" \
    'module Mod
optional plugin modplugin
classname ModPlugin' terminated
expect_verdict plugin_only accept "$plugin_root"

# A module directory may carry a nested manifest of its own, one level below the
# manifest that names the module.
nested_root="$workspace/nested/root"
write_manifest "$nested_root/Mod/qmldir" 'module Mod' terminated
write_manifest "$nested_root/Mod/qml/qmldir" \
    'module Mod.Inner
typeinfo inner.qmltypes' terminated
expect_verdict nested reject "$nested_root" "$nested_root/Mod/qml/qmldir"

# Nothing fixes how deeply a manifest may sit, so the scan carries no depth cap.
deep_root="$workspace/deep/root"
write_manifest "$deep_root/Mod/qmldir" 'module Mod' terminated
write_manifest "$deep_root/Mod/a/b/c/qmldir" \
    'module Mod.A.B.C
typeinfo deep.qmltypes' terminated
expect_verdict deeply_nested reject "$deep_root" "$deep_root/Mod/a/b/c/qmldir"

# An import root that does not exist at all is the same fault one step earlier:
# the module was never built. It is refused with its own wording.
missing_root="$workspace/missing/root"
missing_output=""
missing_status=0
missing_output="$(cd "$workspace" && bash "$guard" lint "$missing_root" 2>&1)" ||
    missing_status=$?
checked=$((checked + 1))
if ((missing_status != 1)) || [[ "$missing_output" != *"does not exist"* ]]; then
    printf 'qml_lint_order_self_test: an absent import root should be rejected\n' >&2
    status=1
fi

if ((status != 0)); then
    exit "$status"
fi

printf 'qml_lint_order_self_test: %d build-ordering scenarios are enforced\n' \
    "$checked"

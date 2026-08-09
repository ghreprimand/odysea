#!/usr/bin/env bash

# Proves the shared corpus resolution behaves the same with and without Git.
#
# Every guard in this directory now takes its corpus from one library, which
# concentrates a risk as well as removing one: a single wrong answer there is
# wrong in every gate at once. The two enumerations are therefore compared
# directly against each other over the same tree, rather than each being
# checked against a description of what it ought to return.
#
# The scenarios build a throwaway source tree, take a copy of it with the
# repository metadata removed, and require both copies to answer identically.
# They also pin the two properties that are not shared: with Git the search
# reads the index, and without it the enumeration has to exclude build output
# it cannot be told about by an ignore file.

set -euo pipefail

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
readonly library="$tools_directory/guard_corpus.sh"

if [[ ! -f "$library" ]]; then
    printf 'guard_corpus_self_test: the corpus library is missing\n' >&2
    exit 1
fi

if ! command -v git >/dev/null 2>&1; then
    printf 'guard_corpus_self_test: git is required to build the scenario trees and is not installed\n' >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

status=0
checked=0

report() {
    local outcome="$1" description="$2"
    checked=$((checked + 1))
    if [[ "$outcome" == "pass" ]]; then
        printf '  ok      %s\n' "$description"
    else
        printf '  FAILED  %s\n' "$description" >&2
        status=1
    fi
}

# A driver that exercises the library the way a guard does: source it, resolve
# the corpus, then answer one question about it.
readonly probe='#!/usr/bin/env bash
set -euo pipefail
# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"
guard_corpus_init probe >/dev/null
action="$1"
shift
case "$action" in
    mode) printf "%s\n" "$guard_corpus_mode" ;;
    list) guard_corpus_list "$@" | tr "\0" "\n" | sort ;;
    grep) pattern="$1"; shift; guard_corpus_grep -nI -E "$pattern" -- "$@" || true ;;
esac
'

# Builds a source tree with a known shape. The build directory is recognised by
# the cache file inside it and not by its name, so the tree also holds a
# directory called build that is not one.
build_tree() {
    local root="$1"

    mkdir -p "$root/tools" "$root/app/qml" "$root/core/src" "$root/docs" \
        "$root/build" "$root/build-output" "$root/.archon"
    cp "$library" "$root/tools/guard_corpus.sh"
    printf '%s' "$probe" >"$root/tools/probe.sh"
    printf 'project(sandbox)\n' >"$root/CMakeLists.txt"
    printf 'import QtQuick\nItem {}\n' >"$root/app/qml/Main.qml"
    printf 'int main() { return 0; }\n' >"$root/core/src/main.cpp"
    printf 'notes\n' >"$root/docs/notes.md"
    printf 'not a build directory\n' >"$root/build/keep.txt"

    # Build output, and a workflow directory: neither belongs to the corpus.
    printf 'CMAKE_CACHE\n' >"$root/build-output/CMakeCache.txt"
    printf 'int generated() { return 1; }\n' >"$root/build-output/generated.cpp"
    printf 'private\n' >"$root/.archon/private.txt"
}

readonly tracked_paths=(
    CMakeLists.txt
    tools/guard_corpus.sh
    tools/probe.sh
    app/qml/Main.qml
    core/src/main.cpp
    docs/notes.md
    build/keep.txt
)

readonly with_git="$workspace/with-git"
readonly without_git="$workspace/without-git"

build_tree "$with_git"
git -C "$with_git" init -q
git -C "$with_git" config core.hooksPath /dev/null
git -C "$with_git" add -- "${tracked_paths[@]}"

cp -a "$with_git" "$without_git"
rm -rf "$without_git/.git"

probe_in() {
    local root="$1"
    shift
    (cd "$root" && bash "$root/tools/probe.sh" "$@")
}

# --- The modes are the ones they claim to be --------------------------------
if [[ "$(probe_in "$with_git" mode)" == "git" ]]; then
    report pass "a tree with repository metadata resolves the tracked corpus"
else
    report fail "a tree with repository metadata resolves the tracked corpus"
fi

if [[ "$(probe_in "$without_git" mode)" == "filesystem" ]]; then
    report pass "a tree without repository metadata resolves the source tree"
else
    report fail "a tree without repository metadata resolves the source tree"
fi

# --- The two enumerations agree ---------------------------------------------
# Each pattern is answered by both trees and the answers are required to be
# identical. Comparing them against each other rather than against a fixed list
# is what makes this a check of the library rather than of one enumerator.
compare_pattern_sets() {
    local description="$1"
    shift

    local from_git from_filesystem
    from_git="$(probe_in "$with_git" list "$@")"
    from_filesystem="$(probe_in "$without_git" list "$@")"

    if [[ "$from_git" == "$from_filesystem" ]]; then
        report pass "$description"
    else
        report fail "$description (git: ${from_git//$'\n'/,} | tree: ${from_filesystem//$'\n'/,})"
    fi
}

compare_pattern_sets "the whole corpus is the same either way"
compare_pattern_sets "a suffix pattern selects the same files" '*.cpp'
compare_pattern_sets "a directory pattern selects the same files" 'app/qml/*.qml'
compare_pattern_sets "a stem pattern selects the same files" 'core/src/main*'
compare_pattern_sets "an exclusion removes the same file" '.' '!docs/notes.md'
compare_pattern_sets "an unmatched pattern is empty either way" '*.rs'

# --- The corpus is the intended one, not merely a consistent one -------------
# Two enumerators can agree and both be wrong, so the content is pinned once.
expected_corpus="$(printf '%s\n' "${tracked_paths[@]}" | sort)"
if [[ "$(probe_in "$without_git" list)" == "$expected_corpus" ]]; then
    report pass "build output and the workflow directory are left out of the corpus"
else
    report fail "build output and the workflow directory are left out of the corpus: $(probe_in "$without_git" list | tr '\n' ' ')"
fi

if [[ "$(probe_in "$without_git" list '*.cpp')" == "core/src/main.cpp" ]]; then
    report pass "a generated source inside a build tree is not part of the corpus"
else
    report fail "a generated source inside a build tree is not part of the corpus"
fi

if [[ "$(probe_in "$without_git" list 'build/*')" == "build/keep.txt" ]]; then
    report pass "a directory named build without a cache file is still part of the corpus"
else
    report fail "a directory named build without a cache file is still part of the corpus"
fi

# --- Searching reads the index where there is one ----------------------------
# A guard runs before a commit to judge what that commit would publish, so with
# Git present the search has to read staged content and not the working tree.
printf 'const char* marker = "STAGED_MARKER";\n' >"$with_git/core/src/main.cpp"
git -C "$with_git" add core/src/main.cpp
printf 'const char* marker = "WORKING_TREE_MARKER";\n' >"$with_git/core/src/main.cpp"

staged_search="$(probe_in "$with_git" grep 'STAGED_MARKER|WORKING_TREE_MARKER' '*.cpp')"
if [[ "$staged_search" == *"STAGED_MARKER"* &&
    "$staged_search" != *"WORKING_TREE_MARKER"* ]]; then
    report pass "with repository metadata the search reads staged content"
else
    report fail "with repository metadata the search reads staged content: $staged_search"
fi

printf 'const char* marker = "TREE_ONLY_MARKER";\n' >"$without_git/core/src/main.cpp"
if [[ "$(probe_in "$without_git" grep 'TREE_ONLY_MARKER' '*.cpp')" == *"TREE_ONLY_MARKER"* ]]; then
    report pass "without repository metadata the search reads the source tree"
else
    report fail "without repository metadata the search reads the source tree"
fi

# --- An inferred root that is not a source root is refused --------------------
# Without Git the root is inferred from where the library sits. A wrong
# inference would present as an empty corpus, which is the failure this whole
# arrangement exists to prevent, so it is refused by name instead.
readonly stray="$workspace/stray/tools"
mkdir -p "$stray"
cp "$library" "$stray/guard_corpus.sh"
printf '%s' "$probe" >"$stray/probe.sh"

stray_output=""
stray_status=0
stray_output="$( (cd "$workspace/stray" && bash "$stray/probe.sh" mode) 2>&1 )" ||
    stray_status=$?
if ((stray_status != 0)) && [[ "$stray_output" == *"is not a source root"* ]]; then
    report pass "a library sitting outside a source root refuses to guess"
else
    report fail "a library sitting outside a source root refuses to guess (exit ${stray_status}: ${stray_output})"
fi

if ((status != 0)); then
    printf 'guard_corpus_self_test: failed\n' >&2
    exit 1
fi

printf 'guard_corpus_self_test: %d scenarios passed\n' "$checked"

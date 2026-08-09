#!/usr/bin/env bash
set -euo pipefail

# Proves the key-construction guard reaches each of its checks.
#
# Every scenario asserts the SPECIFIC reason the guard reports, not merely
# that it failed. A guard can reject a planted defect for the wrong reason and
# look correct while the check under test is dead, which is exactly how an
# earlier gate in this repository passed with one of its checks unreachable.

readonly tools_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly guard_source="$tools_directory/check_key_construction.sh"
readonly library_source="$tools_directory/guard_corpus.sh"

# Stated as a precondition rather than skipped. The scenarios below need a
# real repository to build, and a self-test that declined to run would report
# nothing while reading as a pass in the summary.
if ! command -v git >/dev/null 2>&1; then
    echo "key_construction_guard_self_test: git is required to build the scenario repositories and is not installed" >&2
    exit 1
fi

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

failures=0

report() {
    printf 'key_construction_guard_self_test: %s\n' "$1" >&2
    failures=$((failures + 1))
}

# Builds a throwaway repository holding the given directory-model sources.
# Each scenario gets its own, because the guard enumerates through
# `git ls-files` and therefore only sees committed paths.
build_repository() {
    local name="$1"
    local root="$workspace/$name"
    rm -rf -- "$root"
    mkdir -p "$root/app/src" "$root/tools"
    cp "$guard_source" "$root/tools/check_key_construction.sh"
    cp "$library_source" "$root/tools/guard_corpus.sh"
    # The scenarios stage rather than commit, so no identity configuration is
    # required and no scenario can be affected by repository hooks.
    git -C "$root" init --quiet
    printf '%s\n' "$root"
}

track_repository() {
    git -C "$1" add --all
}

run_guard() {
    local root="$1"
    local output
    local code
    output="$(cd "$root" && bash tools/check_key_construction.sh 2>&1)" && code=0 || code=$?
    printf '%s\n' "$code"
    printf '%s\n' "$output"
}

expect() {
    local scenario="$1"
    local root="$2"
    local expected_code="$3"
    local expected_text="$4"
    local result
    local code
    local output
    result="$(run_guard "$root")"
    code="$(printf '%s\n' "$result" | head -n 1)"
    output="$(printf '%s\n' "$result" | tail -n +2)"

    if [[ "$code" != "$expected_code" ]]; then
        report "$scenario: expected exit $expected_code, got $code; output: $output"
        return
    fi
    if [[ -n "$expected_text" && "$output" != *"$expected_text"* ]]; then
        report "$scenario: expected the reason to mention '$expected_text'; output: $output"
    fi
}

# The permitted shape: one normalization inside entryKey, one inside
# normalizedFilesystemPath.
compliant_source() {
    cat <<'SOURCE'
#include "directory_list_model.hpp"

QString DirectoryListModel::entryKey(const std::filesystem::path& path) const {
    ++entryKeyBuilds_;
    return QString::fromStdString(path.lexically_normal().string());
}

std::filesystem::path DirectoryListModel::normalizedFilesystemPath(const QString& path) const {
    return std::filesystem::path(normalizedPath(path).toStdString()).lexically_normal();
}

int DirectoryListModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(entries_.size());
}
SOURCE
}

# Scenario 1: compliant sources are accepted.
root="$(build_repository compliant)"
compliant_source >"$root/app/src/directory_list_model.cpp"
track_repository "$root"
expect "compliant sources" "$root" 0 "2 permitted normalizations"

# Scenario 2: the formula spelled out in another member is rejected, naming
# that member. This is the bypass the guard exists to stop.
root="$(build_repository bypass)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::receiveScanBatch(std::uint64_t token) {
    const QString key = QString::fromStdString(entry.path.lexically_normal().string());
    static_cast<void>(key);
}
SOURCE
track_repository "$root"
expect "hand-spelled key in another member" "$root" 1 "in receiveScanBatch"

# Scenario 3: the formula outside any member definition is rejected with its
# own reason, so a free function or file-scope helper cannot host it.
root="$(build_repository free_function)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_thumbnails.cpp"
#include "directory_list_model.hpp"

namespace {
QString stableKey(const std::filesystem::path& source) {
    return QString::fromStdString(source.lexically_normal().string());
}
} // namespace
SOURCE
track_repository "$root"
expect "free function" "$root" 1 "outside any member function"

# Scenario 4: a file the index does not carry is not inspected, which is why
# the guard enumerates through Git rather than the working tree. Recorded so
# the ordering requirement stays visible: stage before gating.
root="$(build_repository untracked)"
compliant_source >"$root/app/src/directory_list_model.cpp"
track_repository "$root"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::receiveScanBatch(std::uint64_t token) {
    const QString key = QString::fromStdString(entry.path.lexically_normal().string());
}
SOURCE
expect "untracked source" "$root" 0 "2 permitted normalizations"

# Scenario 5: no matching sources at all is a failure, not a pass. A guard
# that silently enforces nothing after a rename is worse than no guard.
root="$(build_repository no_sources)"
printf 'placeholder\n' >"$root/app/src/other_model.cpp"
track_repository "$root"
expect "no matching sources" "$root" 1 "no directory-model sources matched"

# Scenario 6: matching sources that contain no permitted normalization is also
# a failure. This is what catches the permitted function being renamed while
# the formula moves somewhere the guard would have to be updated to see.
root="$(build_repository no_permitted)"
cat <<'SOURCE' >"$root/app/src/directory_list_model.cpp"
#include "directory_list_model.hpp"

int DirectoryListModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(entries_.size());
}
SOURCE
track_repository "$root"
expect "no permitted normalization" "$root" 1 "may have been renamed"

# Scenario 7: a renamed permitted function is rejected even though the file
# still normalizes exactly once, which is the half of scenario 6 that a
# sighting count alone would miss.
root="$(build_repository renamed)"
cat <<'SOURCE' >"$root/app/src/directory_list_model.cpp"
#include "directory_list_model.hpp"

QString DirectoryListModel::buildRowKey(const std::filesystem::path& path) const {
    return QString::fromStdString(path.lexically_normal().string());
}
SOURCE
track_repository "$root"
expect "renamed permitted function" "$root" 1 "in buildRowKey"

# Scenario 8: a normalization inside a lambda nested in a permitted function
# is accepted, and one inside a lambda nested in any other member is not.
# Without this the guard would be trivially bypassed by wrapping the formula.
root="$(build_repository nested_lambda)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_operations.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::performCopy(const QString& destination) {
    const auto key = [](const std::filesystem::path& path) {
        return QString::fromStdString(path.lexically_normal().string());
    };
    static_cast<void>(key);
}
SOURCE
track_repository "$root"
expect "lambda inside another member" "$root" 1 "in performCopy"

# Scenario 9: header files matching the glob are inspected too, so the formula
# cannot move into an inline member.
root="$(build_repository header)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_inline.hpp"
#pragma once

QString DirectoryListModel::inlineKey(const std::filesystem::path& path) const {
    return QString::fromStdString(path.lexically_normal().string());
}
SOURCE
track_repository "$root"
expect "inline member in a header" "$root" 1 "in inlineKey"

# Scenario 10: a source whose name matches nothing, but which includes the
# model header, is inspected. Naming the file after the model was never the
# property that mattered; reaching into the model is. Without this a new
# app/src/directory_key_helpers.cpp could hold a hand-spelled key untouched.
root="$(build_repository included_elsewhere)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_key_helpers.cpp"
#include "directory_list_model.hpp"

QString DirectoryListModel::helperKey(const std::filesystem::path& path) const {
    return QString::fromStdString(path.lexically_normal().string());
}
SOURCE
track_repository "$root"
expect "unrelated name including the model header" "$root" 1 "in helperKey"

# Scenario 11: the residual hole, asserted rather than described. A source
# that neither matches the name nor includes the model header is not
# inspected, so a key spelled there is invisible to this guard. It is recorded
# as a passing scenario because that is the honest state: coverage follows the
# include, and a file that reaches the model without including its header does
# not exist today but is not prevented from existing.
root="$(build_repository outside_coverage)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/unrelated_helpers.cpp"
#include <QString>

QString unrelatedKey(const std::filesystem::path& path) {
    return QString::fromStdString(path.lexically_normal().string());
}
SOURCE
track_repository "$root"
expect "source outside the covered set" "$root" 0 "2 permitted normalizations"

if ((failures != 0)); then
    printf 'key_construction_guard_self_test: %d scenario(s) failed\n' "$failures" >&2
    exit 1
fi

echo "key_construction_guard_self_test: all scenarios passed"

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
checks_run=0

# How many expectations this file states. A suite that reports no failures
# after running half its scenarios reads exactly like one that ran them all,
# and a scenario is one careless edit away from being dropped. Compared at the
# end against the number actually executed.
readonly expected_checks=37

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
    checks_run=$((checks_run + 1))
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
# normalizedFilesystemPath, and one entry path spelled as text inside data,
# which is a permitted place for it. Both rules need a permitted sighting
# here, because each fails on its own when it finds none anywhere.
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

QVariant DirectoryListModel::data(const QModelIndex& index, int role) const {
    const odysea::core::Entry& entry = entries_.at(static_cast<std::size_t>(index.row()));
    return QString::fromStdString(entry.path.string());
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
#
# The helper is placed BELOW a member definition, which is the position that
# matters. Above one, the enclosing name is empty because nothing has set it
# yet, and the scenario passes whether or not the guard tracks where a member
# ends. Below one, it passes only because it does: a guard that stops at the
# definition line attributes the helper to the member above it and admits it.
# Every source in this model happens to open with its anonymous namespace, so
# the weaker position was the one the record could see.
root="$(build_repository free_function)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_thumbnails.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::refreshThumbnails() {
    thumbnailGeneration_++;
}

namespace {
QString stableKey(const std::filesystem::path& source) {
    return QString::fromStdString(source.lexically_normal().string());
}
} // namespace
SOURCE
track_repository "$root"
expect "free function below a member definition" "$root" 1 "outside any member function"

# Scenario 3b: the same helper above every member definition. Retained as its
# own scenario so both positions stay pinned - the branch is reached two
# different ways and a fix for one is not a fix for the other.
root="$(build_repository free_function_above)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_thumbnails.cpp"
#include "directory_list_model.hpp"

namespace {
QString stableKey(const std::filesystem::path& source) {
    return QString::fromStdString(source.lexically_normal().string());
}
} // namespace

void DirectoryListModel::refreshThumbnails() {
    thumbnailGeneration_++;
}
SOURCE
track_repository "$root"
expect "free function above every member definition" "$root" 1 "outside any member function"

# Scenario 3c: the attribution itself, asserted on the count rather than only
# on the exit status. A helper below a PERMITTED member does not merely get
# admitted when the end of a member is not tracked - it is counted as a
# permitted sighting, so the bypass raises the number the guard's own vacuity
# floor reads. Rejecting it is not enough; it must not be counted.
root="$(build_repository helper_below_permitted)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

QVariant DirectoryListModel::data(const QModelIndex& index, int role) const {
    return QVariant();
}

namespace {
QString stableKey(const odysea::core::Entry& entry) {
    return QString::fromStdString(entry.path.string());
}
} // namespace
SOURCE
track_repository "$root"
expect "helper below a permitted member" "$root" 1 "outside any member function"

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
# Asserted on the reason belonging to THIS rule, not on the suffix both floors
# share. The fixture holds no entry-path spelling either, so the entry-path
# floor fires with the same trailing words and a scenario asserting only
# "may have been renamed" passes on the other rule's message - which left this
# floor deletable with the suite green.
expect "no permitted normalization" "$root" 1 "no permitted normalization found"

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

# Scenario 12: the documented bypass, planted where it was demonstrated. A
# reconciliation member builds its comparison key by hand rather than through
# the counted builder. Scanned paths are already absolute and normal, so the
# key is byte-identical and the cost gate reads healthy: this scenario is the
# whole reason the second rule exists, and it is asserted by the member it
# names.
root="$(build_repository hand_spelled_entry_path)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::applyPresentationSettings() {
    for (const odysea::core::Entry& entry : presented) {
        presentedKeys.push_back(QString::fromStdString(entry.path.string()));
    }
}
SOURCE
track_repository "$root"
expect "hand-spelled entry path in a reconciliation member" "$root" 1 \
    "takes an entry's path as text or under another name in applyPresentationSettings"

# Scenario 13: a second conversion from the same family is rejected too, so
# the rule cannot be walked past by reaching for native() instead of
# string(). Without this the alternation could lose every branch but one and
# the suite would not notice.
root="$(build_repository entry_path_native)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::receiveScanBatch(std::uint64_t token) {
    const QString key = QString::fromStdString(entry.path.native());
    static_cast<void>(key);
}
SOURCE
track_repository "$root"
expect "entry path spelled through native()" "$root" 1 "in receiveScanBatch"

# Scenario 14: an entry path spelled as text outside any member definition is
# rejected with its own reason, so a file-scope helper cannot host it either.
# Placed below a member definition for the reason scenario 3 gives.
root="$(build_repository entry_path_free_function)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_thumbnails.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::refreshThumbnails() {
    thumbnailGeneration_++;
}

namespace {
QString stableKey(const odysea::core::Entry& entry) {
    return QString::fromStdString(entry.path.generic_string());
}
} // namespace
SOURCE
track_repository "$root"
expect "entry path in a free function" "$root" 1 "outside any member function"

# Scenario 15: sources that hold no permitted entry-path spelling at all fail,
# and with the reason belonging to that rule rather than the other one. This
# is what catches the rule going dead because every permitted function was
# renamed, which a violation count alone reads as perfect compliance.
root="$(build_repository no_permitted_entry_path)"
cat <<'SOURCE' >"$root/app/src/directory_list_model.cpp"
#include "directory_list_model.hpp"

QString DirectoryListModel::entryKey(const std::filesystem::path& path) const {
    ++entryKeyBuilds_;
    return QString::fromStdString(path.lexically_normal().string());
}
SOURCE
track_repository "$root"
expect "no permitted entry-path spelling" "$root" 1 "no permitted entry-path spelling found"

# Scenario 16: a violation of each rule in one tree is reported for both, not
# for whichever is inspected first. The rules run per file and each returns
# its own status, so one rule failing must not stop the other being applied.
root="$(build_repository both_rules)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::receiveScanBatch(std::uint64_t token) {
    const QString spelled = QString::fromStdString(entry.path.string());
    const QString normalized = QString::fromStdString(entry.path.lexically_normal().string());
    static_cast<void>(spelled);
    static_cast<void>(normalized);
}
SOURCE
track_repository "$root"
expect "both rules violated in one member" "$root" 1 "takes an entry's path as text"
expect "both rules violated in one member, normalization half" "$root" 1 "normalizes a path"

# --- Every conversion the standard offers ------------------------------------
# The guard once listed five conversion names and claimed in a comment that a
# conversion outside the list would be reported by this self-test. Nothing
# here enumerated anything, and eight more conversions already existed - so
# the comment told the next reader not to look, which is worse than silence.
#
# The rule now matches by shape, and this is the enumeration the comment
# promised. Every conversion member `std::filesystem::path` offers is planted
# in turn and must be rejected. The generic forms matter as much as the native
# ones: on this platform they are byte-identical, so each produces exactly the
# counted key.
readonly -a path_conversions=(
    "string()"
    "wstring()"
    "u8string()"
    "u16string()"
    "u32string()"
    "generic_string()"
    "generic_wstring()"
    "generic_u8string()"
    "generic_u16string()"
    "generic_u32string()"
    "native()"
    "c_str()"
    "string<char>()"
)

# Compared against the number actually planted below, so a conversion dropped
# from the list above shrinks the enumeration loudly rather than quietly.
readonly expected_conversions=13
conversions_planted=0

for conversion in "${path_conversions[@]}"; do
    root="$(build_repository "conversion_${conversions_planted}")"
    compliant_source >"$root/app/src/directory_list_model.cpp"
    {
        printf '#include "directory_list_model.hpp"\n\n'
        printf 'void DirectoryListModel::receiveScanBatch(std::uint64_t token) {\n'
        printf '    const auto key = entry.path.%s;\n' "$conversion"
        printf '    static_cast<void>(key);\n}\n'
    } >"$root/app/src/directory_list_model_async.cpp"
    track_repository "$root"
    expect "entry path spelled through $conversion" "$root" 1 "in receiveScanBatch"
    conversions_planted=$((conversions_planted + 1))
done

if ((conversions_planted != expected_conversions)); then
    printf 'key_construction_guard_self_test: planted %d conversions, %d are declared\n' \
        "$conversions_planted" "$expected_conversions" >&2
    failures=$((failures + 1))
fi

# --- The alias branch --------------------------------------------------------
# The conversion branch needs `.path.` adjacent. One idiomatic line separates
# them and leaves the same key free to be built from the alias, at less cost
# than the residual the guard already declares.
root="$(build_repository alias_reference)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::receiveScanBatch(std::uint64_t token) {
    const std::filesystem::path& aliased = entry.path;
    const QString key = QString::fromStdString(aliased.string());
    static_cast<void>(key);
}
SOURCE
track_repository "$root"
expect "entry path bound to a filesystem-path reference" "$root" 1 "in receiveScanBatch"

root="$(build_repository alias_auto)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::receiveScanBatch(std::uint64_t token) {
    const auto& aliased = entry.path;
    const QString key = QString::fromStdString(aliased.string());
    static_cast<void>(key);
}
SOURCE
track_repository "$root"
expect "entry path bound through auto" "$root" 1 "in receiveScanBatch"

# The other direction, and the reason the alias branch is typed rather than
# general. The tab state in these files carries its own `path` member of
# interface string type; binding it is ordinary code with nothing to do with a
# row key, and a rule matching any `= ....path;` would reject the shipped
# sources on the day it landed.
root="$(build_repository alias_interface_string)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

QString DirectoryListModel::tabLabel(int tabIndex) const {
    const QString tabPath = panes_[0].tabs[static_cast<std::size_t>(tabIndex)].path;
    return tabPath;
}
SOURCE
track_repository "$root"
expect "an interface-string tab path bound to a name" "$root" 0 "2 permitted normalizations"

# --- Declared residuals ------------------------------------------------------
# Each of these is something the guard does NOT catch, asserted as accepted so
# the residual list in the guard cannot quietly stop describing the guard. A
# residual that is written down is an instrument; one that is only believed is
# a hole.

# A path compares against another path with no string built anywhere. Named in
# the guard's residual list, and held by the shape of the update rather than
# by any spelling rule.
root="$(build_repository residual_path_comparison)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::receiveScanBatch(std::uint64_t token) {
    if (arriving.path == presented.path) {
        return;
    }
}
SOURCE
track_repository "$root"
expect "a path compared against a path" "$root" 0 "2 permitted normalizations"

# The implicit conversion. `std::filesystem::path` converts to its native
# string type with no conversion member named, so the counted key is produced
# by a line that mentions no conversion at all. Catching it needs the
# argument's type, which is not on the line.
root="$(build_repository residual_implicit_conversion)"
compliant_source >"$root/app/src/directory_list_model.cpp"
cat <<'SOURCE' >"$root/app/src/directory_list_model_async.cpp"
#include "directory_list_model.hpp"

void DirectoryListModel::receiveScanBatch(std::uint64_t token) {
    const QString key = QString::fromStdString(entry.path);
    static_cast<void>(key);
}
SOURCE
track_repository "$root"
expect "an entry path converted implicitly" "$root" 0 "2 permitted normalizations"

if ((checks_run != expected_checks)); then
    printf 'key_construction_guard_self_test: ran %d expectations, %d are declared\n' \
        "$checks_run" "$expected_checks" >&2
    exit 1
fi

if ((failures != 0)); then
    printf 'key_construction_guard_self_test: %d scenario(s) failed\n' "$failures" >&2
    exit 1
fi

printf 'key_construction_guard_self_test: all %d expectations passed\n' "$checks_run"

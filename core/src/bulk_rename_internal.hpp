// Internal seam for bulk rename. Not part of the public API and not
// installed: only bulk_rename.cpp, operation_journal.cpp, and the headless
// tests include it.
//
// Two of the four things a plan must catch depend on what the filesystem
// underneath happens to be, and one of them cannot be provoked at all on the
// filesystems these tests run on. A name that differs from an existing entry
// only by case is a collision on a filesystem that folds case and an ordinary
// free name on one that does not; the Linux filesystems available here do not
// fold, so a test that waited for a real fold would never exercise the check
// and would report a pass for a check that had never run.
//
// Routing the two filesystem questions through injectable steps makes both
// answers deterministic. A test supplies a directory reader and a name probe
// that fold case, and the case-only collision is measured on any filesystem.
// The production steps are the plain readers, so there is one planner rather
// than one for tests and one for use.
#pragma once

#include "odysea/core/bulk_rename.hpp"
#include "odysea/core/directory_model.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace odysea::core::detail {

/// The literal names a directory holds.
struct DirectoryNames {
    /// False when the directory could not be read at all, which makes every
    /// proposed name in it unverifiable and therefore refused.
    bool readable = false;
    /// Names exactly as the directory reports them, with no filtering: a
    /// hidden entry occupies its name the same as any other.
    std::vector<std::string> names;
};

/// What the filesystem resolves a path to, without following a symbolic link.
struct ProbedName {
    bool exists = false;
    /// Identity of whatever the path resolved to. Unknown when the entry
    /// exists but could not be examined, which is treated as occupied.
    EntryIdentity identity;
};

/// Read the literal names in `directory`.
[[nodiscard]] DirectoryNames read_directory_names(const std::filesystem::path& directory);

/// Resolve `path` to an entry, or report that nothing is there.
[[nodiscard]] ProbedName probe_name(const std::filesystem::path& path);

/// The longest single name `directory` accepts, in bytes. Falls back to a
/// conservative 255 when the filesystem does not say.
[[nodiscard]] std::size_t name_limit_for(const std::filesystem::path& directory);

/// The filesystem questions a plan asks, so a test can answer them itself.
struct PlanProbes {
    std::function<DirectoryNames(const std::filesystem::path&)> read_directory =
        &read_directory_names;
    std::function<ProbedName(const std::filesystem::path&)> probe = &probe_name;
    std::function<std::size_t(const std::filesystem::path&)> name_limit = &name_limit_for;
};

/// plan_bulk_rename with the filesystem questions supplied by the caller.
[[nodiscard]] RenamePlan plan_bulk_rename_using(const std::vector<std::filesystem::path>& entries,
                                                const RenameRule& rule, const PlanProbes& probes);

/// Perform one rename of a batch: `from` and `to` always share a directory.
///
/// The journalled and unjournalled applications differ only in this step, so
/// the sequencing that keeps a batch from writing over an entry that has not
/// left yet has one implementation.
using BatchRenameStep = std::function<void(
    const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error)>;

/// The production step: rename_entry, which refuses an occupied name.
void rename_with_filesystem_step(const std::filesystem::path& from, const std::filesystem::path& to,
                                 std::error_code& error);

/// apply_bulk_rename with the rename step and the plan recheck supplied.
///
/// `on_applied` is called after each rename completes, with the path the entry
/// started the batch at and the path it now holds. The journal records through
/// it, which is why a record is only ever made for a rename that has already
/// happened.
[[nodiscard]] RenameApplication
apply_bulk_rename_using(const RenamePlan& plan, const PlanProbes& probes,
                        const BatchRenameStep& rename_step,
                        const std::function<void(const AppliedRename&)>& on_applied);

} // namespace odysea::core::detail

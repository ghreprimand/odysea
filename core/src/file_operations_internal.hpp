// Internal seam for the mutation primitives. Not part of the public API and
// not installed: only file_operations.cpp and the headless tests include it.
//
// Replacing an existing entry is a sequence of renames rather than a single
// one: the entry that is going to take the destination is prepared first, the
// occupant is moved aside, the prepared entry is installed, and the occupant is
// only discarded once the install has succeeded. Every one of those renames can
// fail on a real filesystem, and most of the failures cannot be provoked from a
// test without privileged control of the mount. Routing them through an
// injectable step makes each failure reachable, so the recovery behaviour can
// be asserted rather than assumed.
#pragma once

#include "odysea/core/file_operations.hpp"
#include "odysea/core/transfer.hpp"

#include <filesystem>
#include <functional>
#include <system_error>

namespace odysea::core::detail {

/// Which step of a replacement a rename is performing.
///
/// The distinction exists so a test can fail exactly one step. It also records
/// the one asymmetry that matters: only Relocate moves an entry that the caller
/// supplied, so only Relocate can report std::errc::cross_device_link. Every
/// other step renames within a single directory.
enum class RenameKind {
    /// Move the source entry out of where the caller left it.
    Relocate,
    /// Move an existing destination aside so the prepared entry can take its
    /// place.
    Backup,
    /// Put the prepared entry at the destination.
    Install,
    /// Put a moved-aside destination back after a failed install.
    Restore,
    /// Put a relocated source back after a failed install.
    Unwind,
};

/// Relocates `from` to `to`, reporting failure through `error`.
using RenameStep = std::function<void(RenameKind kind, const std::filesystem::path& from,
                                      const std::filesystem::path& to, std::error_code& error)>;

/// Reserve an unused working name in `directory`.
///
/// Working entries always live in the directory the operation is working in,
/// so installing one is a rename within a single directory and can never cross
/// a filesystem boundary.
///
/// The serial advances globally rather than per call, so a candidate that is
/// already taken — by another thread, another process, or an entry left behind
/// by an earlier interrupted run — is never retried by a caller that would pick
/// the same name again.
///
/// Shared with bulk rename, which needs a name to break a cycle through. One
/// reservation scheme means one set of names that classify_working_entry can
/// explain to a listing; a second scheme would produce entries nothing could
/// account for.
[[nodiscard]] std::filesystem::path reserve_working_path(const std::filesystem::path& directory,
                                                         WorkingEntryRole role,
                                                         std::error_code& error);

/// The production step: std::filesystem::rename, whatever the kind.
void rename_with_filesystem(RenameKind kind, const std::filesystem::path& from,
                            const std::filesystem::path& to, std::error_code& error);

/// copy_into with the rename step supplied by the caller.
[[nodiscard]] OperationOutcome copy_into_using(const std::filesystem::path& source,
                                               const std::filesystem::path& destination_directory,
                                               const OperationOptions& options,
                                               const RenameStep& rename_step);

/// move_into with the rename step supplied by the caller.
[[nodiscard]] OperationOutcome move_into_using(const std::filesystem::path& source,
                                               const std::filesystem::path& destination_directory,
                                               const OperationOptions& options,
                                               const RenameStep& rename_step);

/// The same two, with reporting and control supplied as well.
///
/// The plain forms above are these with an empty observer and no control, so
/// there is one implementation of the recovery behaviour rather than two that
/// have to be kept saying the same thing.
[[nodiscard]] OperationOutcome copy_into_using(const std::filesystem::path& source,
                                               const std::filesystem::path& destination_directory,
                                               const TransferOptions& transfer,
                                               const RenameStep& rename_step);

[[nodiscard]] OperationOutcome move_into_using(const std::filesystem::path& source,
                                               const std::filesystem::path& destination_directory,
                                               const TransferOptions& transfer,
                                               const RenameStep& rename_step);

/// rename_entry with the rename step supplied by the caller.
[[nodiscard]] OperationOutcome rename_entry_using(const std::filesystem::path& source,
                                                  std::string_view new_name,
                                                  const OperationOptions& options,
                                                  const RenameStep& rename_step);

} // namespace odysea::core::detail

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

/// rename_entry with the rename step supplied by the caller.
[[nodiscard]] OperationOutcome rename_entry_using(const std::filesystem::path& source,
                                                  std::string_view new_name,
                                                  const OperationOptions& options,
                                                  const RenameStep& rename_step);

/// Name prefix of the entry a copy is assembled under before it is installed.
inline constexpr std::string_view staging_prefix = ".odysea-staging-";

/// Name prefix of the entry a replaced destination is held under until the
/// replacement is in place.
inline constexpr std::string_view backup_prefix = ".odysea-replaced-";

} // namespace odysea::core::detail

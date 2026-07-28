// OdySea core: filesystem mutation primitives.
//
// Toolkit-agnostic. No Qt, no GUI types. Copy, move, and rename are expressed
// as explicit requests that never throw: every failure is reported through an
// outcome so the presentation layer can decide how to surface it.
//
// Replacing an existing entry cannot always be done in one step, so these
// operations prepare the replacement under a temporary name in the destination
// directory and move the existing entry aside while the swap happens. Both
// temporary names start with a dot and are removed once the operation settles.
// In the rare case where recovery from a failed step itself fails, the data is
// left under its temporary name rather than removed: an entry under an
// unexpected name can be recovered, a deleted one cannot.
#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

namespace odysea::core {

/// How an operation behaves when the destination name is already taken.
enum class ConflictPolicy {
    /// Change nothing and report std::errc::file_exists.
    Fail,
    /// Replace the existing destination. Replacement, never a merge: a
    /// destination directory is removed rather than written into.
    Overwrite,
    /// Pick the next free "name (2)" style variant next to the original.
    AutoRename,
};

/// Parameters shared by the mutation operations.
struct OperationOptions {
    ConflictPolicy conflict = ConflictPolicy::Fail;
};

/// The result of a single mutation.
///
/// On success `destination` holds the final path, which differs from the
/// requested path when ConflictPolicy::AutoRename resolved a collision. On
/// failure `destination` is empty and `error` describes the cause.
struct OperationOutcome {
    std::filesystem::path destination;
    std::error_code error;

    [[nodiscard]] bool succeeded() const noexcept { return !error; }
};

/// Resolve the path a new entry named `name` would occupy in `directory`.
///
/// Applies the conflict policy without touching the filesystem beyond
/// existence checks. Exposed because callers frequently need to preview the
/// final name before committing to an operation.
[[nodiscard]] OperationOutcome resolve_destination(const std::filesystem::path& directory,
                                                   std::string_view name,
                                                   const OperationOptions& options);

/// Copy `source` into `destination_directory`, keeping the source file name.
///
/// Directories are copied recursively and symlinks are copied as symlinks
/// rather than followed. Copying a directory into itself or into one of its own
/// descendants is rejected with std::errc::invalid_argument.
///
/// When the resolved destination is the source itself the copy is a no-op that
/// reports success, so an entry can never be destroyed by being copied over
/// itself.
///
/// Otherwise the copy is assembled beside the destination and installed once it
/// is complete. An existing destination is moved aside rather than removed, and
/// is discarded only after the replacement is in place, so no failure at any
/// point costs either the source or the destination. A failed copy leaves no
/// partial entry behind.
[[nodiscard]] OperationOutcome copy_into(const std::filesystem::path& source,
                                         const std::filesystem::path& destination_directory,
                                         const OperationOptions& options);

/// Move `source` into `destination_directory`, keeping the source file name.
///
/// Uses a rename when both paths share a filesystem and falls back to a
/// recursive copy followed by removal of the source across filesystems. Moving
/// a directory into itself or into one of its own descendants is rejected with
/// std::errc::invalid_argument.
///
/// When the resolved destination is the source itself the move is a no-op that
/// reports success. Moving into a free name, and replacing one non-directory
/// with another, are left to the rename, which does either in a single atomic
/// step.
///
/// Every other case — a replacement a rename cannot perform, or a move across
/// filesystems — assembles the moved entry beside the destination and swaps it
/// into place. The existing destination is moved aside rather than removed and
/// is discarded only after the replacement is in place, so a failure at any
/// point leaves both the source and the destination intact.
[[nodiscard]] OperationOutcome move_into(const std::filesystem::path& source,
                                         const std::filesystem::path& destination_directory,
                                         const OperationOptions& options);

/// Rename `source` in place to `new_name`.
///
/// `new_name` must be a bare file name: empty names, names containing a path
/// separator, and the "." and ".." specials are rejected with
/// std::errc::invalid_argument.
///
/// Renaming an entry to a name that already resolves to that same entry is a
/// no-op that reports success, whether it matches by spelling or by identity.
///
/// A free name, and one non-directory replacing another, are handled by a
/// single atomic rename. Replacing a directory moves the existing destination
/// aside and discards it only after the replacement is in place, so a failure
/// leaves both entries intact.
[[nodiscard]] OperationOutcome rename_entry(const std::filesystem::path& source,
                                            std::string_view new_name,
                                            const OperationOptions& options);

} // namespace odysea::core

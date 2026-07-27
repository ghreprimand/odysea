// OdySea core: filesystem mutation primitives.
//
// Toolkit-agnostic. No Qt, no GUI types. Copy, move, and rename are expressed
// as explicit requests that never throw: every failure is reported through an
// outcome so the presentation layer can decide how to surface it.
#pragma once

#include <filesystem>
#include <string_view>
#include <system_error>

namespace odysea::core {

/// How an operation behaves when the destination name is already taken.
enum class ConflictPolicy {
    /// Change nothing and report std::errc::file_exists.
    Fail,
    /// Replace the existing destination.
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
[[nodiscard]] OperationOutcome copy_into(const std::filesystem::path& source,
                                         const std::filesystem::path& destination_directory,
                                         const OperationOptions& options);

/// Move `source` into `destination_directory`, keeping the source file name.
///
/// Uses a rename when both paths share a filesystem and falls back to a
/// recursive copy followed by removal of the source across filesystems. Moving
/// a directory into itself or into one of its own descendants is rejected with
/// std::errc::invalid_argument.
[[nodiscard]] OperationOutcome move_into(const std::filesystem::path& source,
                                         const std::filesystem::path& destination_directory,
                                         const OperationOptions& options);

/// Rename `source` in place to `new_name`.
///
/// `new_name` must be a bare file name: empty names, names containing a path
/// separator, and the "." and ".." specials are rejected with
/// std::errc::invalid_argument.
[[nodiscard]] OperationOutcome rename_entry(const std::filesystem::path& source,
                                            std::string_view new_name,
                                            const OperationOptions& options);

} // namespace odysea::core

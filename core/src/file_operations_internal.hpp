// Internal seam for the mutation primitives. Not part of the public API and
// not installed: only file_operations.cpp and the headless tests include it.
//
// Moving an entry between filesystems takes a different route from moving it
// within one, and that route is where a destination can be lost if it is
// cleared before the replacement exists. The route is chosen by whether the
// rename step reports std::errc::cross_device_link, so the step is injectable
// and the fallback can be exercised without a second mount point.
#pragma once

#include "odysea/core/file_operations.hpp"

#include <filesystem>
#include <system_error>

namespace odysea::core::detail {

/// Relocates `from` to `to`, reporting failure through `error`.
///
/// Only ever used for the steps that relocate the *source* entry. Installing a
/// staged replacement is always a rename within one directory, so it cannot
/// cross a filesystem boundary and never goes through this seam.
using RenameStep = void (*)(const std::filesystem::path& from, const std::filesystem::path& to,
                            std::error_code& error);

/// The production step: std::filesystem::rename.
void rename_with_filesystem(const std::filesystem::path& from, const std::filesystem::path& to,
                            std::error_code& error);

/// move_into with the rename step supplied by the caller.
///
/// Behaviour matches odysea::core::move_into, which forwards to this with
/// rename_with_filesystem.
[[nodiscard]] OperationOutcome move_into_using(const std::filesystem::path& source,
                                               const std::filesystem::path& destination_directory,
                                               const OperationOptions& options,
                                               RenameStep rename_step);

} // namespace odysea::core::detail

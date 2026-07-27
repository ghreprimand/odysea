// OdySea core: freedesktop.org trash support.
//
// Toolkit-agnostic. Deleting from the shell means moving an entry into the
// desktop trash with a recoverable record, not destroying it. The
// implementation follows the freedesktop.org Trash specification: a `files`
// directory holding the entry and an `info` directory holding a `.trashinfo`
// record of where it came from and when it was removed.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace odysea::core {

/// The result of moving one entry to the trash.
struct TrashOutcome {
    /// Final location inside the trash `files` directory. Empty on failure.
    std::filesystem::path trashed_path;
    /// The matching `.trashinfo` record. Empty on failure.
    std::filesystem::path info_path;
    std::error_code error;

    [[nodiscard]] bool succeeded() const noexcept { return !error; }
};

/// Move `source` into the trash that serves the filesystem it lives on.
///
/// Entries on the same filesystem as the home trash go to the home trash.
/// Entries elsewhere go to a top-level trash directory on their own
/// filesystem, so the operation stays a rename and never silently rewrites a
/// large tree. The trash record is claimed before the entry is moved, so a
/// concurrent deletion of the same name cannot collide.
[[nodiscard]] TrashOutcome move_to_trash(const std::filesystem::path& source);

/// The home trash directory: `XDG_DATA_HOME/Trash`, or `HOME/.local/share/Trash`
/// when that variable is unset. Reports std::errc::invalid_argument when
/// neither variable provides an absolute path. Does not create anything.
[[nodiscard]] std::filesystem::path home_trash_directory(std::error_code& error);

/// The trash directory that serves `path`, creating its layout when needed.
///
/// Returns the home trash for entries on the same filesystem, otherwise a
/// top-level `.Trash/<uid>` or `.Trash-<uid>` directory on the filesystem that
/// holds `path`.
[[nodiscard]] std::filesystem::path trash_directory_for(const std::filesystem::path& path,
                                                        std::error_code& error);

/// Percent-encode a path for the `Path` field of a `.trashinfo` record.
///
/// Unreserved characters and the path separator are kept literal; everything
/// else is escaped, as the specification requires.
[[nodiscard]] std::string encode_trash_location(std::string_view path);

} // namespace odysea::core

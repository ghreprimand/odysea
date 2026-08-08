// OdySea core: one place that reads an entry's own metadata.
//
// Internal seam. Not installed, not part of the public API, and free of Qt.
//
// Listing and usage accounting both need the same facts about an entry — its
// identity, its type, its apparent and allocated size, its link count — and
// both must read them without following a symbolic link. Reading them twice
// in two places would mean two chances to reassemble the device number
// wrongly, and a wrong reassembly is invisible: it stays consistent with
// itself for every entry, so nothing observable fails. There is one
// implementation and one test that pins it against what `lstat` reports.
#pragma once

#include "odysea/core/directory_model.hpp"

#include <cstdint>
#include <filesystem>
#include <sys/stat.h>

namespace odysea::core::detail {

/// An entry's own metadata: never a symbolic link target's.
struct EntryMetadata {
    /// False when the entry could not be examined at all. Every other field
    /// is then left at its default.
    bool known = false;
    EntryIdentity identity;
    /// Bytes the entry claims. A sparse or compressed file claims more than
    /// it occupies; a small file usually occupies more than it claims.
    std::uintmax_t apparent_bytes = 0;
    /// Bytes the filesystem has actually allocated, derived from the reported
    /// 512-byte block count.
    std::uintmax_t allocated_bytes = 0;
    /// Whole seconds of the last content modification, as a Unix timestamp.
    std::int64_t modified_seconds = 0;
    /// Hard links to this inode. Greater than one means the same data is
    /// reachable under more than one name.
    std::uint64_t link_count = 0;
    /// Mode bits, including the file-type bits.
    std::uint32_t mode = 0;
    /// The `errno` value that made the read fail, zero when `known` is set.
    int error_number = 0;
};

/// Read `path`'s own metadata without following a symbolic link.
///
/// `statx` is preferred over `lstat` for one reason: it can report the
/// creation time, which is what separates a recycled device and inode pair
/// from the entry that previously held it. It costs the same single syscall,
/// so the identity strengthening is free per entry.
///
/// A kernel or sandbox without `statx` falls back to `lstat` and leaves the
/// creation time unknown, which degrades identity to the device and inode
/// pair rather than losing it.
///
/// That fallback has no automated coverage: `statx` has been available since
/// Linux 4.11 and cannot be made to fail from a test without a seccomp filter
/// or an older kernel. The tests do pin the part of the preferred path that
/// could fail silently — `statx` splits the device into major and minor
/// numbers, and the value reassembled from them is checked against the one
/// `lstat` reports for the same entry, so a wrong reassembly cannot pass by
/// being consistently wrong everywhere.
///
/// A filesystem that records no creation time is a separate and expected case
/// from a kernel without `statx`, and it is covered: the tests ask the
/// filesystem directly and require identity to carry a creation time exactly
/// when one is reported, so the field cannot quietly stop being read.
[[nodiscard]] EntryMetadata read_entry_metadata(const std::filesystem::path& path);

/// Read the metadata of whatever `path` resolves to, following a symbolic
/// link. Used only where a consumer has decided to treat a link as its target,
/// such as a usage walk configured to descend through directory symlinks;
/// everything that describes the entry itself uses `read_entry_metadata`.
[[nodiscard]] EntryMetadata read_target_metadata(const std::filesystem::path& path);

/// Whether the mode bits describe a directory, ignoring any link target.
[[nodiscard]] inline bool mode_is_directory(std::uint32_t mode) noexcept {
    return S_ISDIR(mode);
}

/// Whether the mode bits describe a symbolic link.
[[nodiscard]] inline bool mode_is_symlink(std::uint32_t mode) noexcept {
    return S_ISLNK(mode);
}

/// Whether the mode bits describe a regular file.
[[nodiscard]] inline bool mode_is_regular(std::uint32_t mode) noexcept {
    return S_ISREG(mode);
}

} // namespace odysea::core::detail

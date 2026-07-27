// OdySea core: filesystem model.
//
// Toolkit-agnostic. No Qt, no GUI types. Provides the directory-listing
// primitives the rest of the application builds on, so the core can be
// exercised and tested without a display server.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace odysea::core {

/// The kind of a directory entry.
enum class EntryKind { Directory, File, Symlink, Other };

/// A single entry in a directory listing.
struct Entry {
    std::string name;
    std::filesystem::path path;
    EntryKind kind = EntryKind::Other;
    std::uintmax_t size = 0;

    [[nodiscard]] bool is_directory() const noexcept { return kind == EntryKind::Directory; }
};

/// Options controlling how a directory is read.
struct ListOptions {
    /// Include entries whose names begin with a dot.
    bool show_hidden = false;
};

/// Read a directory into a sorted list of entries.
///
/// Ordering is directories-first, then case-insensitively by name, matching the
/// default presentation of most desktop file managers. Errors on individual
/// entries are skipped rather than aborting the whole listing; a failure to open
/// the directory itself is reported through `error`.
[[nodiscard]] std::vector<Entry> read_directory(const std::filesystem::path& path,
                                                 const ListOptions& options,
                                                 std::error_code& error);

} // namespace odysea::core

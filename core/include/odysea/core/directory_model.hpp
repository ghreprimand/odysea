// OdySea core: filesystem model.
//
// Toolkit-agnostic. No Qt, no GUI types. Provides the directory-listing
// primitives the rest of the application builds on, so the core can be
// exercised and tested without a display server.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
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
    /// Stable filesystem identity for preserving selection across rename,
    /// sorting, filtering, and refresh. Zero when metadata lookup failed.
    std::uint64_t device = 0;
    std::uint64_t inode = 0;

    [[nodiscard]] bool is_directory() const noexcept { return kind == EntryKind::Directory; }
};

/// Options controlling how a directory is read.
struct ListOptions {
    /// Include entries whose names begin with a dot.
    bool show_hidden = false;
};

/// Whether a name is hidden by desktop convention: it begins with a dot.
[[nodiscard]] bool is_hidden_name(std::string_view name);

/// Describe one element of a directory iteration.
///
/// Metadata failures on the element degrade to EntryKind::Other and a zero
/// size rather than aborting an entire listing.
[[nodiscard]] Entry make_entry(const std::filesystem::directory_entry& element);

/// Whether `first` sorts before `second` in the default presentation order:
/// directories first, then case-insensitively by name.
[[nodiscard]] bool entry_orders_before(const Entry& first, const Entry& second);

/// Sort entries in place into the default presentation order. Exposed so a
/// consumer receiving incremental batches can order them the same way a
/// complete listing is ordered.
void sort_entries(std::vector<Entry>& entries);

/// Read a directory into a sorted list of entries.
///
/// Ordering is directories-first, then case-insensitively by name, matching the
/// default presentation of most desktop file managers. Errors on individual
/// entries are skipped rather than aborting the whole listing.
///
/// Never throws. A failure to open the directory reports through `error` and
/// yields no entries. A failure part-way through iteration reports the first
/// such failure through `error` and returns the entries read before it, so a
/// caller can present a partial listing alongside the error rather than losing
/// both.
[[nodiscard]] std::vector<Entry> read_directory(const std::filesystem::path& path,
                                                const ListOptions& options, std::error_code& error);

} // namespace odysea::core

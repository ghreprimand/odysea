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

/// Identity of a filesystem entry, for following that entry across rename,
/// sorting, filtering, and refresh within a single session.
///
/// The device and inode numbers alone are not sufficient. On Btrfs the device
/// half of a subvolume root is an anonymous device number: the kernel hands it
/// out at runtime and returns it to a pool when the subvolume goes away, and
/// every subvolume root carries inode number 256. A subvolume removed and
/// another created afterwards can therefore present exactly the pair the first
/// one had, which makes two unrelated directories indistinguishable.
///
/// The creation time closes that gap where the filesystem reports one. It is
/// the one timestamp that neither rename nor a content write disturbs, so it
/// separates a recycled identifier from the entry that previously held it
/// without disturbing the identity of an entry that merely moved or changed.
///
/// Identity is meaningful only within one run against one live mount. It must
/// never be written to disk or compared across a remount, because the device
/// half is not a durable property of the filesystem.
struct EntryIdentity {
    /// Device and inode numbers as reported for the entry itself. Both zero
    /// when metadata lookup failed.
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    /// Creation time, as a Unix timestamp, when `birth_known` is set. Not
    /// every filesystem records one, so identity degrades to the device and
    /// inode pair rather than failing when it is absent.
    std::int64_t birth_seconds = 0;
    std::uint32_t birth_nanoseconds = 0;
    bool birth_known = false;

    /// Whether metadata lookup produced usable identifiers. An unknown
    /// identity never matches another entry, including another unknown one.
    [[nodiscard]] bool known() const noexcept { return device != 0 && inode != 0; }

    [[nodiscard]] friend bool operator==(const EntryIdentity&, const EntryIdentity&) = default;
};

/// A single entry in a directory listing.
struct Entry {
    std::string name;
    std::filesystem::path path;
    EntryKind kind = EntryKind::Other;
    std::uintmax_t size = 0;
    /// Identity for preserving selection across rename, sorting, filtering,
    /// and refresh. Unknown when metadata lookup failed.
    EntryIdentity identity;
    /// Whole seconds of the last content modification, as a Unix timestamp.
    /// Zero when metadata lookup failed.
    ///
    /// Like `size` and `identity`, this describes the entry itself and never
    /// the target of a symbolic link, because selection identity has to tell a
    /// link apart from what it points at. A consumer that needs the target's
    /// metadata, such as a cache keyed on file contents, resolves it
    /// separately instead of reinterpreting this field.
    std::int64_t modified_seconds = 0;
    /// Whether a symbolic link resolves to a directory. The entry kind and
    /// identity still describe the link itself, while navigation consumers can
    /// treat a directory target as a directory.
    bool target_is_directory = false;

    [[nodiscard]] bool is_directory() const noexcept {
        return kind == EntryKind::Directory || target_is_directory;
    }
};

/// Options controlling how a directory is read.
struct ListOptions {
    /// Include entries whose names begin with a dot.
    bool show_hidden = false;
};

/// Whether a name is hidden by desktop convention: it begins with a dot.
[[nodiscard]] bool is_hidden_name(std::string_view name);

/// Whether `left` and `right` describe the same filesystem entry.
///
/// False whenever either identity is unknown. Equality alone would report two
/// entries whose metadata lookup failed as the same entry, because both would
/// hold the same zeroed fields; a failed lookup has to mean "cannot say"
/// rather than "matches everything that also failed".
[[nodiscard]] bool same_identity(const EntryIdentity& left, const EntryIdentity& right);

/// How many entries in `entries` carry `identity`. Zero for an unknown
/// identity. A consumer that follows an entry across a refresh uses this to
/// require an unambiguous match before it moves selection or focus.
[[nodiscard]] std::size_t count_identity(const std::vector<Entry>& entries,
                                         const EntryIdentity& identity);

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
/// Ordering is directories-first, then case-insensitively by name, which is the
/// default presentation this application ships. Errors on individual entries
/// are skipped rather than aborting the whole listing.
///
/// Never throws. A failure to open the directory reports through `error` and
/// yields no entries. A failure part-way through iteration reports the first
/// such failure through `error` and returns the entries read before it, so a
/// caller can present a partial listing alongside the error rather than losing
/// both.
[[nodiscard]] std::vector<Entry> read_directory(const std::filesystem::path& path,
                                                const ListOptions& options, std::error_code& error);

} // namespace odysea::core

#include "odysea/core/directory_model.hpp"

#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

/// Record the entry's own metadata, never a symlink target's.
///
/// `statx` is preferred over `lstat` for one reason: it can report the
/// creation time, which is what separates a recycled device and inode pair
/// from the entry that previously held it. It costs the same single syscall,
/// so the identity strengthening is free per entry.
///
/// A kernel or sandbox without `statx` falls back to `lstat` and leaves the
/// creation time unknown, which degrades identity to the device and inode pair
/// rather than losing it.
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
void read_metadata(const fs::path& path, Entry& entry) {
    struct ::statx details{};
    const int flags = AT_SYMLINK_NOFOLLOW | AT_STATX_SYNC_AS_STAT;
    if (::statx(AT_FDCWD, path.c_str(), flags, STATX_INO | STATX_MTIME | STATX_BTIME, &details) ==
        0) {
        entry.identity.device =
            static_cast<std::uint64_t>(::makedev(details.stx_dev_major, details.stx_dev_minor));
        entry.identity.inode = static_cast<std::uint64_t>(details.stx_ino);
        entry.modified_seconds = static_cast<std::int64_t>(details.stx_mtime.tv_sec);
        if ((details.stx_mask & STATX_BTIME) != 0) {
            entry.identity.birth_known = true;
            entry.identity.birth_seconds = static_cast<std::int64_t>(details.stx_btime.tv_sec);
            entry.identity.birth_nanoseconds = details.stx_btime.tv_nsec;
        }
        return;
    }

    struct ::stat metadata{};
    if (::lstat(path.c_str(), &metadata) == 0) {
        entry.identity.device = static_cast<std::uint64_t>(metadata.st_dev);
        entry.identity.inode = static_cast<std::uint64_t>(metadata.st_ino);
        entry.modified_seconds = static_cast<std::int64_t>(metadata.st_mtim.tv_sec);
    }
}

std::string to_lower(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

EntryKind classify(const fs::directory_entry& entry, std::error_code& ec) {
    if (entry.is_symlink(ec)) {
        return EntryKind::Symlink;
    }
    if (entry.is_directory(ec)) {
        return EntryKind::Directory;
    }
    if (entry.is_regular_file(ec)) {
        return EntryKind::File;
    }
    return EntryKind::Other;
}

} // namespace

bool is_hidden_name(std::string_view name) {
    return !name.empty() && name.front() == '.';
}

Entry make_entry(const fs::directory_entry& element) {
    std::error_code ec;
    Entry entry;
    entry.name = element.path().filename().string();
    entry.path = element.path();
    entry.kind = classify(element, ec);
    if (entry.kind == EntryKind::Symlink) {
        std::error_code target_ec;
        entry.target_is_directory = element.is_directory(target_ec) && !target_ec;
    }
    entry.size = (entry.kind == EntryKind::File) ? element.file_size(ec) : 0;
    read_metadata(entry.path, entry);
    return entry;
}

bool same_identity(const EntryIdentity& left, const EntryIdentity& right) {
    return left.known() && right.known() && left == right;
}

std::size_t count_identity(const std::vector<Entry>& entries, const EntryIdentity& identity) {
    if (!identity.known()) {
        return 0;
    }
    return static_cast<std::size_t>(
        std::ranges::count_if(entries, [&identity](const Entry& candidate) {
            return same_identity(candidate.identity, identity);
        }));
}

bool entry_orders_before(const Entry& first, const Entry& second) {
    if (first.is_directory() != second.is_directory()) {
        return first.is_directory();
    }
    return to_lower(first.name) < to_lower(second.name);
}

void sort_entries(std::vector<Entry>& entries) {
    std::ranges::sort(entries, entry_orders_before);
}

std::vector<Entry> read_directory(const fs::path& path, const ListOptions& options,
                                  std::error_code& error) {
    std::vector<Entry> entries;

    fs::directory_iterator element(path, fs::directory_options::skip_permission_denied, error);
    if (error) {
        return entries;
    }

    // Advance explicitly rather than with a range-for: the range-for uses the
    // throwing increment, which would let a mid-iteration failure escape a
    // function whose contract reports errors through `error`. Whatever was read
    // before the failure is kept, so a caller can still show a partial listing
    // alongside the reported error.
    //
    // This branch has no automated coverage. Linux keeps a directory handle
    // usable after the directory is removed or its permissions change, so a
    // mid-iteration failure cannot be provoked from a test without privileged
    // filesystem control. The tests cover every failure that can be provoked.
    const fs::directory_iterator end;
    while (element != end) {
        const std::string name = element->path().filename().string();
        if (options.show_hidden || !is_hidden_name(name)) {
            entries.push_back(make_entry(*element));
        }

        std::error_code step_error;
        element.increment(step_error);
        if (step_error) {
            error = step_error;
            break;
        }
    }

    sort_entries(entries);
    return entries;
}

} // namespace odysea::core

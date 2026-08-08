#include "entry_metadata.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace odysea::core::detail {
namespace {

/// A reported block count is in 512-byte units, the same unit `st_blocks`
/// uses. Named so the conversion cannot be read as an arbitrary shift.
constexpr std::uintmax_t block_bytes = 512;

EntryMetadata read_metadata(const std::filesystem::path& path, int flags) {
    EntryMetadata metadata;

    struct ::statx details{};
    const unsigned int wanted = STATX_INO | STATX_MTIME | STATX_BTIME | STATX_SIZE | STATX_BLOCKS |
                                STATX_NLINK | STATX_MODE;
    if (::statx(AT_FDCWD, path.c_str(), flags, wanted, &details) == 0) {
        metadata.known = true;
        metadata.identity.device =
            static_cast<std::uint64_t>(::makedev(details.stx_dev_major, details.stx_dev_minor));
        metadata.identity.inode = static_cast<std::uint64_t>(details.stx_ino);
        metadata.modified_seconds = static_cast<std::int64_t>(details.stx_mtime.tv_sec);
        metadata.apparent_bytes = static_cast<std::uintmax_t>(details.stx_size);
        metadata.allocated_bytes = static_cast<std::uintmax_t>(details.stx_blocks) * block_bytes;
        metadata.link_count = static_cast<std::uint64_t>(details.stx_nlink);
        metadata.mode = details.stx_mode;
        if ((details.stx_mask & STATX_BTIME) != 0) {
            metadata.identity.birth_known = true;
            metadata.identity.birth_seconds = static_cast<std::int64_t>(details.stx_btime.tv_sec);
            metadata.identity.birth_nanoseconds = details.stx_btime.tv_nsec;
        }
        return metadata;
    }

    struct ::stat fallback{};
    const bool follow = (flags & AT_SYMLINK_NOFOLLOW) == 0;
    const int fallback_result =
        follow ? ::stat(path.c_str(), &fallback) : ::lstat(path.c_str(), &fallback);
    if (fallback_result == 0) {
        metadata.known = true;
        metadata.identity.device = static_cast<std::uint64_t>(fallback.st_dev);
        metadata.identity.inode = static_cast<std::uint64_t>(fallback.st_ino);
        metadata.modified_seconds = static_cast<std::int64_t>(fallback.st_mtim.tv_sec);
        metadata.apparent_bytes = static_cast<std::uintmax_t>(fallback.st_size);
        metadata.allocated_bytes = static_cast<std::uintmax_t>(fallback.st_blocks) * block_bytes;
        metadata.link_count = static_cast<std::uint64_t>(fallback.st_nlink);
        metadata.mode = fallback.st_mode;
        return metadata;
    }

    metadata.error_number = errno;
    return metadata;
}

} // namespace

EntryMetadata read_entry_metadata(const std::filesystem::path& path) {
    return read_metadata(path, AT_SYMLINK_NOFOLLOW | AT_STATX_SYNC_AS_STAT);
}

EntryMetadata read_target_metadata(const std::filesystem::path& path) {
    return read_metadata(path, AT_STATX_SYNC_AS_STAT);
}

} // namespace odysea::core::detail

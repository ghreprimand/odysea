#include "odysea/core/directory_watcher.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

/// Large enough that a busy directory drains in one read.
constexpr std::size_t read_buffer_bytes = 64U * 1024U;

constexpr std::uint32_t watch_mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM |
                                     IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_EXCL_UNLINK;

std::error_code errno_error() {
    return std::error_code(errno, std::generic_category());
}

std::optional<ChangeKind> classify(std::uint32_t mask) {
    if ((mask & IN_CREATE) != 0U) {
        return ChangeKind::Created;
    }
    if ((mask & IN_DELETE) != 0U) {
        return ChangeKind::Deleted;
    }
    if ((mask & IN_MODIFY) != 0U) {
        return ChangeKind::Modified;
    }
    if ((mask & IN_ATTRIB) != 0U) {
        return ChangeKind::AttributesChanged;
    }
    if ((mask & IN_MOVED_FROM) != 0U) {
        return ChangeKind::MovedFrom;
    }
    if ((mask & IN_MOVED_TO) != 0U) {
        return ChangeKind::MovedTo;
    }
    if ((mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0U) {
        return ChangeKind::WatchRemoved;
    }
    return std::nullopt;
}

/// Milliseconds left before `deadline`, clamped for poll.
int remaining_milliseconds(std::chrono::steady_clock::time_point deadline, bool infinite) {
    if (infinite) {
        return -1;
    }
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (left.count() <= 0) {
        return 0;
    }
    return static_cast<int>(left.count());
}

} // namespace

DirectoryWatcher::DirectoryWatcher(Descriptor inotify, Descriptor wake)
    : inotify_(std::move(inotify)), wake_(std::move(wake)), buffer_(read_buffer_bytes) {}

std::optional<DirectoryWatcher> DirectoryWatcher::create(std::error_code& error) {
    Descriptor inotify(::inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
    if (!inotify.valid()) {
        error = errno_error();
        return std::nullopt;
    }

    Descriptor wake(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    if (!wake.valid()) {
        error = errno_error();
        return std::nullopt;
    }

    error.clear();
    return DirectoryWatcher(std::move(inotify), std::move(wake));
}

bool DirectoryWatcher::add(const fs::path& directory, std::error_code& error) {
    const std::string key = directory.string();
    const int watch = ::inotify_add_watch(inotify_.get(), key.c_str(), watch_mask);
    if (watch < 0) {
        error = errno_error();
        return false;
    }

    // Re-adding an existing path returns the same watch identifier.
    path_by_watch_[watch] = directory;
    watch_by_path_[key] = watch;
    error.clear();
    return true;
}

void DirectoryWatcher::remove(const fs::path& directory) {
    const auto found = watch_by_path_.find(directory.string());
    if (found == watch_by_path_.end()) {
        return;
    }
    ::inotify_rm_watch(inotify_.get(), found->second);
    path_by_watch_.erase(found->second);
    watch_by_path_.erase(found);
}

std::vector<fs::path> DirectoryWatcher::watched() const {
    std::vector<fs::path> directories;
    directories.reserve(path_by_watch_.size());
    for (const auto& [watch, directory] : path_by_watch_) {
        directories.push_back(directory);
    }
    return directories;
}

void DirectoryWatcher::interrupt() {
    const std::uint64_t token = 1;
    // A short write cannot happen on an eventfd; a failure means the reader is
    // already gone, which needs no wake-up.
    const ::ssize_t written = ::write(wake_.get(), &token, sizeof(token));
    static_cast<void>(written);
}

std::vector<DirectoryChange> DirectoryWatcher::wait(std::chrono::milliseconds timeout,
                                                    std::error_code& error) {
    error.clear();
    std::vector<DirectoryChange> changes;

    const bool infinite = timeout.count() < 0;
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    std::array<::pollfd, 2> descriptors{};
    descriptors[0].fd = inotify_.get();
    descriptors[0].events = POLLIN;
    descriptors[1].fd = wake_.get();
    descriptors[1].events = POLLIN;

    const int ready =
        ::poll(descriptors.data(), descriptors.size(), remaining_milliseconds(deadline, infinite));
    if (ready < 0) {
        if (errno != EINTR) {
            error = errno_error();
        }
        return changes;
    }
    if (ready == 0) {
        return changes;
    }

    if ((descriptors[1].revents & POLLIN) != 0) {
        std::uint64_t token = 0;
        const ::ssize_t consumed = ::read(wake_.get(), &token, sizeof(token));
        static_cast<void>(consumed);
    }

    if ((descriptors[0].revents & POLLIN) == 0) {
        return changes;
    }

    // Drain everything the kernel has queued so one burst is one refresh.
    while (true) {
        const ::ssize_t bytes = ::read(inotify_.get(), buffer_.data(), buffer_.size());
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                error = errno_error();
            }
            break;
        }
        if (bytes == 0) {
            break;
        }

        std::size_t offset = 0;
        const auto available = static_cast<std::size_t>(bytes);
        while (offset + sizeof(::inotify_event) <= available) {
            ::inotify_event header{};
            std::memcpy(&header, buffer_.data() + offset, sizeof(header));

            const std::size_t name_offset = offset + sizeof(::inotify_event);
            std::string name;
            if (header.len > 0 && name_offset + header.len <= available) {
                const char* raw = buffer_.data() + name_offset;
                name.assign(raw, ::strnlen(raw, header.len));
            }
            offset = name_offset + header.len;

            if ((header.mask & IN_Q_OVERFLOW) != 0U) {
                changes.push_back(DirectoryChange{.kind = ChangeKind::Overflow,
                                                  .directory = {},
                                                  .name = {},
                                                  .rename_cookie = 0,
                                                  .is_directory = false});
                continue;
            }

            const auto watched_path = path_by_watch_.find(header.wd);
            const fs::path directory =
                watched_path == path_by_watch_.end() ? fs::path{} : watched_path->second;

            if ((header.mask & IN_IGNORED) != 0U) {
                // The kernel dropped the watch: forget it without reporting a
                // second event, WatchRemoved already covered the cause.
                if (watched_path != path_by_watch_.end()) {
                    watch_by_path_.erase(watched_path->second.string());
                    path_by_watch_.erase(watched_path);
                }
                continue;
            }

            const std::optional<ChangeKind> kind = classify(header.mask);
            if (!kind.has_value()) {
                continue;
            }

            changes.push_back(DirectoryChange{.kind = *kind,
                                              .directory = directory,
                                              .name = std::move(name),
                                              .rename_cookie = header.cookie,
                                              .is_directory = (header.mask & IN_ISDIR) != 0U});
        }

        if (available < buffer_.size()) {
            break;
        }
    }

    return changes;
}

} // namespace odysea::core

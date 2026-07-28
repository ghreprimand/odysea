// OdySea core: incremental directory watching.
//
// Toolkit-agnostic. Wraps Linux inotify so a view can refresh the entries that
// actually changed instead of rescanning a directory on a timer. Each wait
// returns the whole batch of events currently queued, which keeps a burst of
// filesystem activity to a single refresh.
#pragma once

#include "odysea/core/descriptor.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace odysea::core {

/// What happened to an entry in a watched directory.
enum class ChangeKind {
    /// A new entry appeared.
    Created,
    /// An entry was removed.
    Deleted,
    /// An entry's contents changed.
    Modified,
    /// An entry's metadata changed: permissions, ownership, timestamps.
    AttributesChanged,
    /// An entry was renamed away. Pairs with MovedTo through the cookie when
    /// the destination is also watched.
    MovedFrom,
    /// An entry was renamed into the directory.
    MovedTo,
    /// The watched directory itself was deleted or moved away.
    WatchRemoved,
    /// The kernel queue overflowed and events were lost: rescan the directory.
    Overflow,
};

/// One filesystem change inside a watched directory.
struct DirectoryChange {
    ChangeKind kind = ChangeKind::Overflow;
    /// The watched directory the change belongs to. Empty for Overflow.
    std::filesystem::path directory;
    /// The affected entry name, empty for directory-level changes.
    std::string name;
    /// Non-zero for MovedFrom and MovedTo; equal values describe one rename.
    std::uint32_t rename_cookie = 0;
    /// True when the affected entry is itself a directory.
    bool is_directory = false;
};

/// A set of watched directories and the changes happening inside them.
///
/// Move-only and not internally synchronized: one thread owns the instance.
/// `interrupt` is the exception and may be called from any thread to release a
/// blocked `wait`, which is how a worker thread is shut down promptly.
class DirectoryWatcher {
  public:
    /// Passed to `wait` to block until something happens.
    static constexpr std::chrono::milliseconds wait_forever{-1};

    /// Create a watcher, or report why the kernel refused.
    [[nodiscard]] static std::optional<DirectoryWatcher> create(std::error_code& error);

    DirectoryWatcher(const DirectoryWatcher&) = delete;
    DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;
    DirectoryWatcher(DirectoryWatcher&&) noexcept = default;
    DirectoryWatcher& operator=(DirectoryWatcher&&) noexcept = default;
    ~DirectoryWatcher() = default;

    /// Start watching a directory. Watching the same directory twice is a
    /// no-op that succeeds.
    [[nodiscard]] bool add(const std::filesystem::path& directory, std::error_code& error);

    /// Stop watching a directory. Unknown directories are ignored.
    void remove(const std::filesystem::path& directory);

    /// The directories currently watched, in unspecified order.
    [[nodiscard]] std::vector<std::filesystem::path> watched() const;

    /// Collect every change currently queued, waiting up to `timeout`.
    ///
    /// Returns an empty batch when the timeout expires or `interrupt` fires.
    /// Pass `wait_forever` to block indefinitely.
    [[nodiscard]] std::vector<DirectoryChange> wait(std::chrono::milliseconds timeout,
                                                    std::error_code& error);

    /// Release a blocked `wait`. Safe to call from another thread.
    void interrupt();

    /// The inotify descriptor, for callers driving their own event loop.
    /// The watcher retains ownership.
    [[nodiscard]] int descriptor() const noexcept { return inotify_.get(); }

  private:
    DirectoryWatcher(Descriptor inotify, Descriptor wake);

    Descriptor inotify_;
    Descriptor wake_;
    std::unordered_map<int, std::filesystem::path> path_by_watch_;
    std::unordered_map<std::string, int> watch_by_path_;
    std::vector<char> buffer_;
};

} // namespace odysea::core

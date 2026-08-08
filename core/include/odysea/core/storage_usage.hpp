// OdySea core: cancellable recursive storage-usage accounting.
//
// Toolkit-agnostic. Answers "what is taking up the space here" for a subtree,
// as running totals per immediate child, so a storage map or an equivalent
// list can render partial results while the walk is still going and can
// abandon it the moment the user looks elsewhere.
//
// Counting policy, stated once here and repeated in the design document
// because a usage figure that does not say what it counts is not a
// measurement:
//
//   * Two sizes are reported side by side, never blended. The apparent size
//     is what the entries claim; the allocated size is what the filesystem
//     has actually reserved, derived from the reported block count. A sparse
//     file claims far more than it occupies, a compressed one occupies less,
//     and a small file usually occupies a whole block more than it claims,
//     so a single number would be wrong for one question or the other.
//   * Every entry is counted, hidden ones included. A dotfile occupies real
//     space; presentation filtering is the caller's business and must not
//     silently change a measurement.
//   * Directories count their own metadata size as well as their contents.
//   * A non-directory entry counts as a file, including symbolic links and
//     special files. A symbolic link is counted at its own size, not its
//     target's, unless the walk is configured to follow directory links.
//   * The same inode reached twice is counted once, whatever brought the walk
//     back to it. The first reach owns the bytes, and later reaches are
//     counted as deduplicated instead. This is what stops a set of hard links
//     from inflating a subtree, and it is why a child's total is "the space
//     that would be freed by removing this child" only when nothing outside
//     the subtree also links to its files. The guarantee does not depend on
//     an entry's link count: a bind mount presents one inode at a second path
//     with a single link at both, so link count says nothing about how often
//     an inode can be reached. Holding it costs one identity per counted
//     entry, on the order of sixty bytes each, which is the deliberate price
//     of a figure that does not inflate silently.
//   * Crossing a filesystem boundary is a caller's decision, never implicit.
#pragma once

#include "odysea/core/directory_model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace odysea::core {

/// Accumulated accounting for one subtree.
struct UsageTotals {
    /// Bytes the counted entries claim.
    std::uintmax_t apparent_bytes = 0;
    /// Bytes the filesystem has allocated for the counted entries.
    std::uintmax_t allocated_bytes = 0;
    /// Non-directory entries counted, symbolic and special files included.
    std::uint64_t file_count = 0;
    /// Directories counted, the subtree's own root included.
    std::uint64_t directory_count = 0;
    /// Directories whose contents could not be listed. Their own metadata is
    /// still counted; whatever they contain is missing from these totals, so
    /// a non-zero value means the figures below are a floor and not a
    /// measurement.
    std::uint64_t unreadable_directories = 0;
    /// Entries whose inode had already been counted elsewhere in this walk.
    std::uint64_t deduplicated_entries = 0;
    /// Directories not descended into because they lie on another
    /// filesystem and the walk was told to stay on one. Deliberate, unlike
    /// `unreadable_directories`, but it still means space is unaccounted.
    std::uint64_t skipped_boundaries = 0;
};

/// One immediate child of the scanned root, carrying its subtree's totals.
struct UsageChild {
    std::string name;
    std::filesystem::path path;
    /// The child's own kind. As everywhere else in the core, a symbolic link
    /// is a symbolic link whatever it points at.
    EntryKind kind = EntryKind::Other;
    EntryIdentity identity;
    UsageTotals totals;
    /// True once the walk has finished with this child. Totals for a child
    /// that is not yet finished are a running subtotal and will grow.
    bool finished = false;
};

/// Caller-controlled walk policy.
struct UsageOptions {
    /// Whether to descend into a directory that lives on a different
    /// filesystem from the scanned root.
    ///
    /// Off by default, because a walk from a system root would otherwise
    /// wander into pseudo-filesystems, removable media, and network mounts,
    /// and answer a question nobody asked. Two consequences are worth stating
    /// plainly. A Btrfs subvolume carries its own anonymous device number, so
    /// with this off a subvolume nested inside the scanned tree is reported
    /// as a skipped boundary rather than measured. And a bind mount of the
    /// same filesystem shares its device number, so it is not a boundary at
    /// all and this setting does not govern it; what keeps it from being
    /// counted twice is that its directories present inodes already visited.
    bool cross_filesystem_boundaries = false;

    /// Whether to descend through a symbolic link that resolves to a
    /// directory, attributing the target's contents to the subtree holding
    /// the link.
    ///
    /// Off by default: following links makes a usage figure depend on where
    /// the link happens to point, which is rarely what "how big is this
    /// directory" means. When it is on, a directory already visited in this
    /// walk is never entered again, so a link that closes a cycle terminates
    /// the walk instead of running forever.
    bool follow_directory_symlinks = false;

    /// Entries examined between progress reports. A report is also delivered
    /// whenever an immediate child is finished, so a caller sees a child
    /// settle even in a tree too small to reach the interval. Zero is treated
    /// as one.
    std::size_t progress_interval = 512;
};

/// A running view of a walk in flight.
struct UsageProgress {
    std::filesystem::path root;
    /// The token returned by the `start` call this report belongs to.
    std::uint64_t token = 0;
    /// A complete snapshot of the immediate children known so far, in
    /// discovery order. A snapshot rather than a delta: the caller replaces
    /// its view instead of merging, so a dropped or reordered report cannot
    /// leave the rendered totals quietly wrong.
    std::vector<UsageChild> children;
    /// Running totals for the whole scanned subtree.
    UsageTotals totals;
    /// Entries examined so far, whether counted or deduplicated.
    std::uint64_t entries_visited = 0;
};

/// How a walk ended.
struct UsageSummary {
    std::filesystem::path root;
    /// The token returned by the `start` call this summary answers.
    std::uint64_t token = 0;
    /// The immediate children, in discovery order. Use `sort_usage_children`
    /// for presentation order.
    std::vector<UsageChild> children;
    UsageTotals totals;
    std::uint64_t entries_visited = 0;
    /// Set only when the root itself could not be examined or listed, in
    /// which case there are no totals to report. A failure deeper in the tree
    /// is a partial result, not an error.
    std::error_code error;
    /// True when a newer request or an explicit cancel ended the walk early.
    bool cancelled = false;

    /// Whether anything the walk was asked to measure is missing from these
    /// totals. A boundary the caller told the walk not to cross is not
    /// counted as partial: the caller already knows it asked for that.
    [[nodiscard]] bool partial() const noexcept {
        return cancelled || static_cast<bool>(error) || totals.unreadable_directories > 0;
    }
};

/// Order children for presentation: largest allocated size first, then
/// largest apparent size, then by name, so the ordering is total even when
/// sizes tie and does not shuffle between two reports of the same tree.
void sort_usage_children(std::vector<UsageChild>& children);

/// A single-worker recursive usage scanner where the newest request wins.
///
/// Shares the directory scanner's cancellation contract rather than restating
/// it: callbacks arrive on the scanner's worker thread and never on the
/// caller's, starting a request cancels everything issued before it, every
/// request receives exactly one completion callback, and no callback is
/// delivered once the scanner is being destroyed. Cancellation is polled per
/// entry, so it is prompt at any depth rather than only between directories.
/// The scanner is neither copyable nor movable so callbacks can safely
/// capture its address.
class UsageScanner {
  public:
    using ProgressHandler = std::function<void(std::uint64_t token, UsageProgress progress)>;
    using CompletionHandler = std::function<void(UsageSummary summary)>;

    struct Request {
        std::filesystem::path root;
        UsageOptions options;
        ProgressHandler on_progress;
        CompletionHandler on_complete;
    };

    UsageScanner();

    UsageScanner(const UsageScanner&) = delete;
    UsageScanner& operator=(const UsageScanner&) = delete;
    UsageScanner(UsageScanner&&) = delete;
    UsageScanner& operator=(UsageScanner&&) = delete;

    /// Cancels any walk in flight and joins the worker. Requests that have
    /// not completed receive no further callbacks.
    ~UsageScanner();

    /// Queue a walk and return immediately with its token.
    std::uint64_t start(Request request);

    /// Cancel the walk in flight and anything queued behind it.
    void cancel();

    /// Block until no walk is running or queued. Intended for shutdown and
    /// for tests; ordinary callers rely on the completion callback.
    void wait_idle();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace odysea::core

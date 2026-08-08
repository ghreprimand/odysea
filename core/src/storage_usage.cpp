#include "odysea/core/storage_usage.hpp"

#include "entry_metadata.hpp"
#include "scan_worker.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

/// Hash an identity by mixing every field that participates in equality.
///
/// Inodes are dense and small, so hashing the inode alone would put a whole
/// filesystem in one bucket per device. The mixing constant is the widely
/// used 64-bit golden-ratio odd constant; it needs no cryptographic property
/// here, only that neighbouring inodes land far apart.
struct IdentityHash {
    [[nodiscard]] std::size_t operator()(const EntryIdentity& identity) const noexcept {
        constexpr std::uint64_t mix = 0x9e3779b97f4a7c15ULL;
        std::uint64_t hash = identity.device;
        auto fold = [&hash](std::uint64_t value) {
            hash ^= value + mix + (hash << 6U) + (hash >> 2U);
        };
        fold(identity.inode);
        fold(static_cast<std::uint64_t>(identity.birth_seconds));
        fold(identity.birth_nanoseconds);
        fold(identity.birth_known ? 1U : 0U);
        return static_cast<std::size_t>(hash);
    }
};

using IdentitySet = std::unordered_set<EntryIdentity, IdentityHash>;

/// Everything a single walk carries from entry to entry.
struct WalkContext {
    UsageOptions options;
    /// The device the scanned root lives on, for the boundary decision.
    std::uint64_t root_device = 0;
    /// Directories already entered.
    ///
    /// Always maintained, not only when links are followed. A directory can
    /// be reached twice without any symbolic link and without leaving the
    /// filesystem: a bind mount of part of the same filesystem shares its
    /// device number, so it is not a boundary, and its contents present the
    /// inodes they already had elsewhere in the tree. Tracking only some of
    /// the time would count such a subtree twice by default, which is the
    /// quiet kind of wrong a usage map cannot afford. The cost is bounded by
    /// the number of directories, far below the number of files.
    IdentitySet visited_directories;
    /// Inodes already counted for entries with more than one link. Entries
    /// with a single link cannot be reached twice, so they stay out of the
    /// set and a walk over ordinary files costs no extra memory.
    IdentitySet counted_files;
};

void account(UsageTotals& totals, const detail::EntryMetadata& metadata, bool is_directory) {
    totals.apparent_bytes += metadata.apparent_bytes;
    totals.allocated_bytes += metadata.allocated_bytes;
    if (is_directory) {
        ++totals.directory_count;
    } else {
        ++totals.file_count;
    }
}

void note_duplicate(UsageTotals& child, UsageTotals& root) {
    ++child.deduplicated_entries;
    ++root.deduplicated_entries;
}

void note_boundary(UsageTotals& child, UsageTotals& root) {
    ++child.skipped_boundaries;
    ++root.skipped_boundaries;
}

void note_unreadable(UsageTotals& child, UsageTotals& root) {
    ++child.unreadable_directories;
    ++root.unreadable_directories;
}

/// Whether the walk must stop at `identity` because it lives on a different
/// filesystem from the scanned root and the caller asked to stay on one.
///
/// One function rather than a repeated condition at each descent site: the
/// decision is the same for a directory reached directly and for one reached
/// through a followed symbolic link, and only the second of those can be set
/// up without privileges. Sharing the decision means the covered case and the
/// uncovered one cannot drift apart.
[[nodiscard]] bool blocked_by_boundary(const WalkContext& context, const EntryIdentity& identity) {
    return !context.options.cross_filesystem_boundaries && identity.device != context.root_device;
}

/// Map an entry's mode bits onto the listing vocabulary. As everywhere else
/// in the core, a symbolic link is classified as a symbolic link whatever it
/// resolves to, so the link is tested before the directory.
[[nodiscard]] EntryKind kind_from_mode(std::uint32_t mode) {
    if (detail::mode_is_symlink(mode)) {
        return EntryKind::Symlink;
    }
    if (detail::mode_is_directory(mode)) {
        return EntryKind::Directory;
    }
    if (detail::mode_is_regular(mode)) {
        return EntryKind::File;
    }
    return EntryKind::Other;
}

/// Whether `identity` is the first sighting of a directory in this walk.
/// An unknown identity is never recorded, and never treated as a repeat.
[[nodiscard]] bool first_visit(WalkContext& context, const EntryIdentity& identity) {
    if (!identity.known()) {
        return true;
    }
    return context.visited_directories.insert(identity).second;
}

/// Count one entry into the child subtree it belongs to and into the root
/// totals, and report the directory to descend into next, if any.
///
/// Returns no path when the entry is not a directory, when it has already
/// been counted, when it lies across a boundary the walk was told not to
/// cross, or when it cannot be examined at all.
[[nodiscard]] std::optional<fs::path> visit_entry(WalkContext& context, const fs::path& path,
                                                  UsageTotals& child, UsageTotals& root) {
    const detail::EntryMetadata metadata = detail::read_entry_metadata(path);
    if (!metadata.known) {
        // The entry was listed but has since gone, or cannot be examined.
        // Nothing can be said about its size, so nothing is counted for it.
        return std::nullopt;
    }

    const bool is_directory = detail::mode_is_directory(metadata.mode);
    if (is_directory) {
        if (!first_visit(context, metadata.identity)) {
            note_duplicate(child, root);
            return std::nullopt;
        }
    } else if (metadata.link_count > 1 && metadata.identity.known() &&
               !context.counted_files.insert(metadata.identity).second) {
        note_duplicate(child, root);
        return std::nullopt;
    }

    account(child, metadata, is_directory);
    account(root, metadata, is_directory);

    if (is_directory) {
        if (blocked_by_boundary(context, metadata.identity)) {
            note_boundary(child, root);
            return std::nullopt;
        }
        return path;
    }

    if (!detail::mode_is_symlink(metadata.mode) || !context.options.follow_directory_symlinks) {
        return std::nullopt;
    }

    // Following a link means treating the target directory as part of this
    // subtree, so the target's own metadata is counted alongside the link's.
    const detail::EntryMetadata target = detail::read_target_metadata(path);
    if (!target.known || !detail::mode_is_directory(target.mode) || !target.identity.known()) {
        return std::nullopt;
    }
    if (blocked_by_boundary(context, target.identity)) {
        note_boundary(child, root);
        return std::nullopt;
    }
    if (!first_visit(context, target.identity)) {
        // The cycle guard: a directory already entered is never entered
        // again, so a link that closes a loop ends the walk instead of
        // running forever.
        note_duplicate(child, root);
        return std::nullopt;
    }

    account(child, target, true);
    account(root, target, true);
    return path;
}

} // namespace

void sort_usage_children(std::vector<UsageChild>& children) {
    std::ranges::sort(children, [](const UsageChild& first, const UsageChild& second) {
        if (first.totals.allocated_bytes != second.totals.allocated_bytes) {
            return first.totals.allocated_bytes > second.totals.allocated_bytes;
        }
        if (first.totals.apparent_bytes != second.totals.apparent_bytes) {
            return first.totals.apparent_bytes > second.totals.apparent_bytes;
        }
        return first.name < second.name;
    });
}

struct UsageScanner::State {
    using Worker = detail::ScanWorker<Request>;

    /// Everything one walk carries while it runs. Held together so the walk
    /// can be written as a few short steps rather than one long function.
    struct Run {
        Request request;
        std::uint64_t token = 0;
        /// Whether callbacks were being delivered when the walk began.
        bool deliver = true;
        std::size_t interval = 1;
        std::size_t since_report = 0;
        WalkContext context;
        UsageSummary summary;
    };

    /// An immediate child whose subtree still has to be walked.
    struct Subtree {
        std::size_t index = 0;
        std::filesystem::path start;
    };

    void execute(Request request, std::uint64_t token) const;
    void report(Run& run) const;
    void counted(Run& run) const;
    void finish(Run& run) const;
    /// Enumerate the immediate children. False when the walk stopped.
    [[nodiscard]] bool list_children(Run& run, std::vector<Subtree>& subtrees) const;
    /// Walk one child's subtree to the end. False when the walk stopped.
    [[nodiscard]] bool walk_subtree(Run& run, std::size_t index,
                                    const std::filesystem::path& start) const;

    // Declared last: reverse member destruction joins the worker before
    // anything a callback reaches is destroyed.
    Worker worker;

    State()
        : worker(Worker::Execute{[this](Request request, std::uint64_t token) {
                     execute(std::move(request), token);
                 }},
                 Worker::Abandon{[](const Request& request, std::uint64_t token) {
                     if (request.on_complete) {
                         UsageSummary summary;
                         summary.root = request.root;
                         summary.token = token;
                         summary.cancelled = true;
                         request.on_complete(std::move(summary));
                     }
                 }}) {}
};

void UsageScanner::State::report(Run& run) const {
    run.since_report = 0;
    if (!run.request.on_progress || !worker.delivering()) {
        return;
    }
    UsageProgress progress;
    progress.root = run.summary.root;
    progress.token = run.token;
    progress.children = run.summary.children;
    progress.totals = run.summary.totals;
    progress.entries_visited = run.summary.entries_visited;
    run.request.on_progress(run.token, std::move(progress));
}

void UsageScanner::State::counted(Run& run) const {
    ++run.summary.entries_visited;
    if (++run.since_report >= run.interval) {
        report(run);
    }
}

void UsageScanner::State::finish(Run& run) const {
    if (worker.cancelled(run.token)) {
        run.summary.cancelled = true;
    }
    if (run.request.on_complete && run.deliver && worker.delivering()) {
        run.request.on_complete(std::move(run.summary));
    }
}

bool UsageScanner::State::list_children(Run& run, std::vector<Subtree>& subtrees) const {
    std::error_code list_error;
    fs::directory_iterator iterator(run.summary.root, list_error);
    if (list_error) {
        run.summary.error = list_error;
        return false;
    }

    const fs::directory_iterator end;
    std::error_code step_error;
    while (iterator != end) {
        if (worker.cancelled(run.token)) {
            return false;
        }

        UsageChild child;
        child.path = iterator->path();
        child.name = child.path.filename().string();
        const detail::EntryMetadata metadata = detail::read_entry_metadata(child.path);
        child.identity = metadata.identity;
        child.kind = kind_from_mode(metadata.mode);

        std::optional<fs::path> descend =
            visit_entry(run.context, child.path, child.totals, run.summary.totals);
        if (descend.has_value()) {
            subtrees.push_back(
                Subtree{.index = run.summary.children.size(), .start = std::move(*descend)});
        } else {
            child.finished = true;
        }
        run.summary.children.push_back(std::move(child));
        counted(run);

        iterator.increment(step_error);
        if (step_error) {
            // The root listing failed part-way through. There is no child to
            // attribute it to, so it is recorded once against the whole walk.
            ++run.summary.totals.unreadable_directories;
            break;
        }
    }
    return true;
}

bool UsageScanner::State::walk_subtree(Run& run, std::size_t index, const fs::path& start) const {
    UsageChild& child = run.summary.children.at(index);
    std::vector<fs::path> queue;
    queue.push_back(start);

    const fs::directory_iterator end;
    while (!queue.empty()) {
        if (worker.cancelled(run.token)) {
            return false;
        }

        const fs::path directory = std::move(queue.back());
        queue.pop_back();

        std::error_code open_error;
        // Permission-denied is deliberately not skipped by the iterator: a
        // subtree the walk cannot read has to be reported as missing from the
        // totals, not silently dropped as if it were empty.
        fs::directory_iterator entries(directory, open_error);
        if (open_error) {
            note_unreadable(child.totals, run.summary.totals);
            continue;
        }

        std::error_code step_error;
        while (entries != end) {
            if (worker.cancelled(run.token)) {
                return false;
            }

            std::optional<fs::path> next =
                visit_entry(run.context, entries->path(), child.totals, run.summary.totals);
            if (next.has_value()) {
                queue.push_back(std::move(*next));
            }
            counted(run);

            entries.increment(step_error);
            if (step_error) {
                note_unreadable(child.totals, run.summary.totals);
                break;
            }
        }
    }

    child.finished = true;
    report(run);
    return true;
}

void UsageScanner::State::execute(Request request, std::uint64_t token) const {
    Run run;
    run.request = std::move(request);
    run.token = token;
    run.deliver = worker.delivering();
    run.interval =
        run.request.options.progress_interval == 0 ? 1 : run.request.options.progress_interval;
    run.context.options = run.request.options;
    run.summary.root = run.request.root;
    run.summary.token = token;

    const detail::EntryMetadata root_metadata = detail::read_entry_metadata(run.summary.root);
    if (!root_metadata.known) {
        run.summary.error = std::error_code(root_metadata.error_number, std::generic_category());
        finish(run);
        return;
    }
    if (!detail::mode_is_directory(root_metadata.mode)) {
        run.summary.error = std::make_error_code(std::errc::not_a_directory);
        finish(run);
        return;
    }

    run.context.root_device = root_metadata.identity.device;
    static_cast<void>(first_visit(run.context, root_metadata.identity));
    account(run.summary.totals, root_metadata, true);
    ++run.summary.entries_visited;

    // The immediate children are enumerated first so a caller can lay out the
    // whole map before any one slice is final, then each subtree is walked to
    // completion in turn so slices settle one after another instead of all at
    // the end.
    std::vector<Subtree> subtrees;
    if (list_children(run, subtrees)) {
        for (const Subtree& subtree : subtrees) {
            if (!walk_subtree(run, subtree.index, subtree.start)) {
                break;
            }
        }
    }
    finish(run);
}

UsageScanner::UsageScanner() : state_(std::make_unique<State>()) {}

UsageScanner::~UsageScanner() = default;

std::uint64_t UsageScanner::start(Request request) {
    return state_->worker.start(std::move(request));
}

void UsageScanner::cancel() {
    state_->worker.cancel();
}

void UsageScanner::wait_idle() {
    state_->worker.wait_idle();
}

} // namespace odysea::core

// Headless tests for cancellable recursive storage-usage accounting.
//
// Expected sizes are derived independently of the code under test: the
// fixtures are measured with a plain `lstat` here, while the scanner reads
// its metadata through `statx`. Aggregation is what these tests pin, so the
// per-entry figures they compare against must not come from the same place
// the scanner got them.
#include "odysea/core/storage_usage.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <sched.h>
#include <string>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;
using namespace std::chrono_literals;

namespace {

/// Sizes read straight from the kernel, without going through the core.
struct RawSize {
    std::uintmax_t apparent = 0;
    std::uintmax_t allocated = 0;
};

RawSize raw_size(const fs::path& path) {
    struct ::stat info{};
    if (::lstat(path.c_str(), &info) != 0) {
        return {};
    }
    return RawSize{.apparent = static_cast<std::uintmax_t>(info.st_size),
                   .allocated = static_cast<std::uintmax_t>(info.st_blocks) * 512};
}

RawSize raw_total(std::initializer_list<fs::path> paths) {
    RawSize total;
    for (const fs::path& path : paths) {
        const RawSize one = raw_size(path);
        total.apparent += one.apparent;
        total.allocated += one.allocated;
    }
    return total;
}

/// The device a path lives on, for the filesystem-boundary case.
std::uint64_t device_of(const fs::path& path) {
    struct ::stat info{};
    if (::lstat(path.c_str(), &info) != 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(info.st_dev);
}

std::string payload(std::size_t bytes) {
    return std::string(bytes, 'x');
}

/// Thread-safe collector for what a walk delivers.
class UsageRecorder {
  public:
    void record_progress(UsageProgress progress) {
        const std::lock_guard<std::mutex> guard(mutex_);
        reports_.push_back(std::move(progress));
    }

    void record_completion(UsageSummary summary) {
        {
            const std::lock_guard<std::mutex> guard(mutex_);
            summary_ = std::move(summary);
        }
        finished_.notify_all();
    }

    bool wait_for_completion() {
        std::unique_lock<std::mutex> guard(mutex_);
        return finished_.wait_for(guard, 30s, [this] { return summary_.has_value(); });
    }

    [[nodiscard]] UsageSummary summary() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return summary_.value_or(UsageSummary{});
    }

    [[nodiscard]] std::vector<UsageProgress> reports() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return reports_;
    }

  private:
    std::mutex mutex_;
    std::condition_variable finished_;
    std::vector<UsageProgress> reports_;
    std::optional<UsageSummary> summary_;
};

UsageScanner::Request request_for(const fs::path& root, UsageRecorder& recorder,
                                  const UsageOptions& options = {}) {
    UsageScanner::Request request;
    request.root = root;
    request.options = options;
    request.on_progress = [&recorder](std::uint64_t, UsageProgress progress) {
        recorder.record_progress(std::move(progress));
    };
    request.on_complete = [&recorder](UsageSummary summary) {
        recorder.record_completion(std::move(summary));
    };
    return request;
}

/// Run one walk to completion and return its summary.
UsageSummary walk(const fs::path& root, const UsageOptions& options = {}) {
    UsageRecorder recorder;
    UsageScanner scanner;
    static_cast<void>(scanner.start(request_for(root, recorder, options)));
    if (!recorder.wait_for_completion()) {
        check(false, "every walk reports completion");
        return {};
    }
    return recorder.summary();
}

const UsageChild* child_named(const UsageSummary& summary, std::string_view name) {
    const auto found = std::ranges::find(summary.children, name, &UsageChild::name);
    return found == summary.children.end() ? nullptr : &*found;
}

void test_totals_match_a_tree_of_known_sizes() {
    const odysea::test::TemporaryTree tree("usage_totals");
    const fs::path alpha = tree.directory("alpha");
    const fs::path first = tree.file("alpha/a.bin", payload(3000));
    const fs::path second = tree.file("alpha/b.bin", payload(1000));
    const fs::path deep = tree.directory("alpha/deep");
    const fs::path third = tree.file("alpha/deep/c.bin", payload(2000));
    const fs::path beta = tree.directory("beta");
    const fs::path fourth = tree.file("beta/d.bin", payload(5000));
    const fs::path loose = tree.file("loose.bin", payload(700));

    const UsageSummary summary = walk(tree.root());
    check(!summary.error, "a readable tree reports no error");
    check(!summary.cancelled, "an undisturbed walk is not cancelled");
    check(!summary.partial(), "a fully readable tree is not a partial result");
    check(summary.children.size() == 3, "every immediate child is reported");
    check(summary.entries_visited == 9, "every entry is examined exactly once");

    const UsageChild* alpha_child = child_named(summary, "alpha");
    const UsageChild* beta_child = child_named(summary, "beta");
    const UsageChild* loose_child = child_named(summary, "loose.bin");
    if (alpha_child == nullptr || beta_child == nullptr || loose_child == nullptr) {
        check(false, "children are reported under their own names");
        return;
    }

    const RawSize alpha_expected = raw_total({alpha, first, second, deep, third});
    check(alpha_child->totals.apparent_bytes == alpha_expected.apparent,
          "a child's apparent size is the sum of its whole subtree");
    check(alpha_child->totals.allocated_bytes == alpha_expected.allocated,
          "a child's allocated size is the sum of its whole subtree");
    check(alpha_child->totals.file_count == 3 && alpha_child->totals.directory_count == 2,
          "a child counts the directories and files beneath it, itself included");
    check(alpha_child->kind == EntryKind::Directory, "a directory child is reported as one");
    check(alpha_child->finished, "a fully walked child is reported as finished");
    check(alpha_child->identity.known(), "a child carries the identity of the entry it describes");

    const RawSize beta_expected = raw_total({beta, fourth});
    check(beta_child->totals.apparent_bytes == beta_expected.apparent,
          "each child is measured independently of its siblings");
    check(loose_child->totals.apparent_bytes == raw_size(loose).apparent,
          "a file child is measured at its own size");
    check(loose_child->totals.file_count == 1 && loose_child->totals.directory_count == 0,
          "a file child counts as one file and no directory");
    check(loose_child->kind == EntryKind::File, "a regular file child is reported as one");

    const RawSize root_expected =
        raw_total({tree.root(), alpha, first, second, deep, third, beta, fourth, loose});
    check(summary.totals.apparent_bytes == root_expected.apparent,
          "the root total covers the whole tree, its own directory included");
    check(summary.totals.allocated_bytes == root_expected.allocated,
          "the root allocated total covers the whole tree");
    check(summary.totals.file_count == 5 && summary.totals.directory_count == 4,
          "the root counts every file and directory in the tree");
    check(summary.totals.deduplicated_entries == 0 && summary.totals.skipped_boundaries == 0 &&
              summary.totals.unreadable_directories == 0,
          "an ordinary tree needs no deduplication, skips no boundary, and reads completely");
}

void test_apparent_and_allocated_sizes_stay_apart() {
    const odysea::test::TemporaryTree tree("usage_sparse");
    const fs::path directory = tree.directory("sparse");
    const fs::path file = tree.file("sparse/hole.bin", "");
    std::error_code resize_error;
    constexpr std::uintmax_t claimed_bytes = 1U << 20U;
    fs::resize_file(file, claimed_bytes, resize_error);
    check(!resize_error, "the fixture can claim a size it does not occupy");

    const RawSize measured = raw_size(file);
    check(measured.apparent == claimed_bytes, "the fixture claims the size it was given");
    if (measured.allocated >= measured.apparent) {
        std::puts("storage_usage: skipping the sparse case, this filesystem allocates in full");
        return;
    }

    const UsageSummary summary = walk(tree.root());
    const UsageChild* child = child_named(summary, "sparse");
    if (child == nullptr) {
        check(false, "the sparse fixture is reported");
        return;
    }

    const RawSize expected = raw_total({directory, file});
    check(child->totals.apparent_bytes == expected.apparent,
          "the apparent total reports what the entries claim");
    check(child->totals.allocated_bytes == expected.allocated,
          "the allocated total reports what the filesystem reserved");
    check(child->totals.allocated_bytes < child->totals.apparent_bytes,
          "a sparse file keeps the two sizes apart instead of blending them");
}

void test_a_file_reached_twice_is_counted_once() {
    const odysea::test::TemporaryTree tree("usage_hardlink");
    const fs::path one = tree.directory("one");
    const fs::path two = tree.directory("two");
    const fs::path original = tree.file("one/data.bin", payload(4000));
    std::error_code link_error;
    fs::create_hard_link(original, two / "same.bin", link_error);
    check(!link_error, "the fixture can create a hard link");
    if (link_error) {
        return;
    }

    const UsageSummary summary = walk(tree.root());
    const RawSize expected = raw_total({tree.root(), one, two, original});
    check(summary.totals.apparent_bytes == expected.apparent,
          "a file reached through two links is counted once");
    check(summary.totals.file_count == 1, "the second link is not counted as a second file");
    check(summary.totals.deduplicated_entries == 1,
          "the repeat is reported rather than silently dropped");

    const UsageChild* first = child_named(summary, "one");
    const UsageChild* second = child_named(summary, "two");
    if (first == nullptr || second == nullptr) {
        check(false, "both link holders are reported as children");
        return;
    }
    const std::uint64_t counted = first->totals.file_count + second->totals.file_count;
    const std::uint64_t deduplicated =
        first->totals.deduplicated_entries + second->totals.deduplicated_entries;
    check(counted == 1 && deduplicated == 1,
          "exactly one of the two subtrees owns the bytes and the other records the repeat");
}

/// How the forked child below reported on the bind-mount fixture. Distinct
/// values rather than a bare pass or fail, so a machine that cannot build the
/// fixture is told apart from one where the walk got the wrong answer.
/// The two ends of the bind mount, as distinct types. Both are paths and they
/// sit next to each other, so transposing them would compile and then produce
/// a fixture that still looks plausible: binding the covered file over the
/// shared one leaves two ordinary files and proves nothing.
struct BindSource {
    fs::path path;
};

struct BindTarget {
    fs::path path;
};

enum class BindOutcome : std::uint8_t {
    CountedOnce = 0,
    CountedTwice = 1,
    NotReproduced = 2,
    ScanFailed = 3,
    Unavailable = 4,
};

/// Bind `source` over `target` inside a private namespace and report whether
/// the walk counted the shared inode once.
///
/// Runs in a forked child because both the user namespace and the mount are
/// process-wide: doing this in the test process would change its identity for
/// every later case and leave a mount behind if the walk aborted. The child's
/// namespace dies with it, so nothing outside this function can observe the
/// mount, and nothing is left to clean up.
BindOutcome bind_mount_walk_outcome(const fs::path& root, const BindSource& source,
                                    const BindTarget& target) {
    // A new user namespace grants a full capability set inside itself, which
    // is what makes the mount below permissible. No identifier mapping is
    // written: mapping is what translates owners for display, and this walk
    // reads sizes and inode numbers, neither of which the mapping touches.
    if (::unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
        return BindOutcome::Unavailable;
    }
    // Detach this namespace's mounts from the host's, so the bind below
    // cannot propagate out of it.
    if (::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
        return BindOutcome::Unavailable;
    }
    if (::mount(source.path.c_str(), target.path.c_str(), nullptr, MS_BIND, nullptr) != 0) {
        return BindOutcome::Unavailable;
    }

    // Asserted, not assumed. If the mount did not actually put one inode at
    // both paths with a single link at each, the walk below would agree with
    // a broken implementation for the wrong reason.
    struct ::stat source_info{};
    struct ::stat target_info{};
    if (::lstat(source.path.c_str(), &source_info) != 0 ||
        ::lstat(target.path.c_str(), &target_info) != 0) {
        return BindOutcome::NotReproduced;
    }
    if (source_info.st_dev != target_info.st_dev || source_info.st_ino != target_info.st_ino ||
        source_info.st_nlink != 1 || target_info.st_nlink != 1) {
        return BindOutcome::NotReproduced;
    }

    const UsageSummary summary = walk(root);
    if (summary.error || summary.cancelled) {
        return BindOutcome::ScanFailed;
    }
    if (summary.totals.file_count == 1 && summary.totals.deduplicated_entries == 1) {
        return BindOutcome::CountedOnce;
    }
    return BindOutcome::CountedTwice;
}

void test_a_single_link_file_reached_twice_is_counted_once() {
    // The counting policy says an inode reached twice is counted once. A bind
    // mount of a single file is the case that policy is easiest to get wrong
    // for: the inode appears at two paths with one link at each, on one
    // device, under two different directories. Nothing else in the walk
    // notices. The boundary check does not fire because a bind mount of the
    // same filesystem shares its device number, and directory deduplication
    // does not apply because the two paths sit under different directories.
    // Restricting the identity set to entries with more than one link
    // therefore counted the bytes twice and left deduplicated_entries at
    // zero, so the inflation was not merely wrong but undetectable.
    const odysea::test::TemporaryTree tree("usage_bind_mount");
    // The two holders are created by the files placed inside them.
    const BindSource source{.path = tree.file("left/shared.bin", payload(3000))};
    const BindTarget target{.path = tree.file("right/covered.bin", payload(64))};

    const pid_t child = ::fork();
    check(child >= 0, "the fixture can fork a namespace holder");
    if (child < 0) {
        return;
    }
    if (child == 0) {
        // Never returns to the harness: this process exists only to carry the
        // namespace, and running the remaining cases in it would report them
        // twice.
        ::_exit(static_cast<int>(bind_mount_walk_outcome(tree.root(), source, target)));
    }

    int status = 0;
    const pid_t waited = ::waitpid(child, &status, 0);
    check(waited == child && WIFEXITED(status) != 0,
          "the namespace holder reports an outcome instead of dying");
    if (waited != child || WIFEXITED(status) == 0) {
        return;
    }

    const auto outcome = static_cast<BindOutcome>(WEXITSTATUS(status));
    if (outcome == BindOutcome::Unavailable) {
        // Stated rather than silent. A kernel without unprivileged user
        // namespaces cannot build this fixture, and a case that quietly
        // reported success there would be claiming a guarantee it never
        // tested.
        std::fputs("storage_usage: bind-mount case skipped, no unprivileged mount namespace\n",
                   stdout);
        return;
    }
    check(outcome != BindOutcome::NotReproduced,
          "the bind mount really does present one inode at two paths with one link at each");
    check(outcome != BindOutcome::ScanFailed, "the walk over the bind-mounted tree completes");
    check(outcome == BindOutcome::CountedOnce,
          "an inode reached twice through a bind mount is counted once and the repeat reported");
}

void test_a_symlink_cycle_terminates() {
    const odysea::test::TemporaryTree tree("usage_cycle");
    const fs::path cycle = tree.directory("cycle");
    const fs::path inner = tree.directory("cycle/inner");
    static_cast<void>(tree.file("cycle/inner/leaf.bin", payload(120)));
    std::error_code link_error;
    fs::create_directory_symlink(cycle, inner / "back", link_error);
    fs::create_directory_symlink(tree.root(), tree.root() / "top", link_error);
    check(!link_error, "the fixture can build a directory symlink loop");
    if (link_error) {
        return;
    }

    const UsageSummary unfollowed = walk(tree.root());
    check(!unfollowed.cancelled && !unfollowed.error,
          "a loop that is not followed completes normally");
    check(unfollowed.entries_visited == 6,
          "an unfollowed link is counted as an entry and never entered");

    UsageOptions following;
    following.follow_directory_symlinks = true;
    const UsageSummary followed = walk(tree.root(), following);
    check(!followed.cancelled && !followed.error,
          "a followed loop terminates instead of running on");
    check(followed.totals.deduplicated_entries >= 2,
          "each link that closes the cycle is reported as an already-visited directory");
    check(followed.entries_visited < 32,
          "the cycle guard stops the walk rather than letting it spiral");

    const UsageChild* link_child = child_named(followed, "top");
    if (link_child == nullptr) {
        check(false, "a symbolic link child is reported");
        return;
    }
    check(link_child->kind == EntryKind::Symlink,
          "a symbolic link keeps its own kind even when the walk follows it");
}

void test_an_unreadable_subtree_reports_a_partial_result() {
    const odysea::test::TemporaryTree tree("usage_denied");
    const fs::path open = tree.directory("open");
    const fs::path readable = tree.file("open/visible.bin", payload(900));
    const fs::path closed = tree.directory("closed");
    static_cast<void>(tree.file("closed/secret.bin", payload(900)));

    if (::geteuid() == 0) {
        std::puts("storage_usage: skipping the unreadable subtree case for a superuser");
        return;
    }

    std::error_code permission_error;
    fs::permissions(closed, fs::perms::none, fs::perm_options::replace, permission_error);
    check(!permission_error, "the fixture subtree can be made unreadable");

    const UsageSummary summary = walk(tree.root());
    fs::permissions(closed, fs::perms::owner_all, fs::perm_options::replace, permission_error);

    check(!summary.error, "an unreadable subtree does not fail the whole walk");
    check(summary.totals.unreadable_directories == 1,
          "the subtree that could not be read is reported");
    check(summary.partial(), "a walk missing a subtree reports itself as partial");
    check(summary.children.size() == 2, "both children are still reported");

    const UsageChild* open_child = child_named(summary, "open");
    const UsageChild* closed_child = child_named(summary, "closed");
    if (open_child == nullptr || closed_child == nullptr) {
        check(false, "both children are reported under their own names");
        return;
    }
    check(open_child->totals.apparent_bytes == raw_total({open, readable}).apparent,
          "a readable sibling is measured in full despite the failure");
    check(open_child->totals.unreadable_directories == 0,
          "the failure is attributed to the subtree it happened in");
    check(closed_child->totals.unreadable_directories == 1,
          "the child holding the unreadable subtree carries the report");
    check(closed_child->totals.file_count == 0,
          "nothing is invented for a subtree that could not be listed");
    check(closed_child->totals.apparent_bytes == raw_size(closed).apparent,
          "an unreadable directory still counts its own metadata");
}

void test_cancellation_stops_a_walk_at_depth() {
    const odysea::test::TemporaryTree tree("usage_cancel");
    constexpr int branch_count = 12;
    constexpr int leaf_count = 200;
    for (int branch = 0; branch < branch_count; ++branch) {
        const std::string base = "branch_" + std::to_string(branch) + "/nested/";
        for (int leaf = 0; leaf < leaf_count; ++leaf) {
            static_cast<void>(tree.file(base + "leaf_" + std::to_string(leaf) + ".bin"));
        }
    }
    // Root, twelve branches, and for each branch one nested directory plus its
    // leaves: the walk has plenty left to do when it is cancelled.
    constexpr std::uint64_t total_entries = 1 + branch_count + (branch_count * (1 + leaf_count));

    UsageRecorder recorder;
    UsageScanner scanner;
    UsageOptions options;
    options.progress_interval = 1;

    UsageScanner::Request request = request_for(tree.root(), recorder, options);
    request.on_progress = [&recorder, &scanner](std::uint64_t, UsageProgress progress) {
        const bool deep_enough = progress.entries_visited >= 20;
        recorder.record_progress(std::move(progress));
        if (deep_enough) {
            // Cancelling from inside the walk's own callback is the shape a
            // UI takes when the user navigates away mid-scan.
            scanner.cancel();
        }
    };
    static_cast<void>(scanner.start(std::move(request)));

    check(recorder.wait_for_completion(), "a cancelled walk still reports completion");
    const UsageSummary summary = recorder.summary();
    check(summary.cancelled, "the summary reports the cancellation");
    check(summary.partial(), "a cancelled walk is a partial result");
    check(summary.entries_visited >= 20, "the walk did real work before it was cancelled");
    // Promptness, not eventual termination. A walk that only checked for
    // cancellation between directories would finish the two hundred entries
    // of the directory it was in, which is still far short of the whole tree
    // and would pass a loose bound.
    check(summary.entries_visited < 60,
          "cancellation stops the walk within a few entries, at depth, not at the "
          "next directory boundary");
    check(summary.entries_visited < total_entries / 4, "a cancelled walk leaves the tree unread");
    scanner.wait_idle();
}

void test_cancellation_stops_a_queue_of_unreadable_directories() {
    const odysea::test::TemporaryTree tree("usage_cancel_queue");
    constexpr int blocked_count = 300;
    static_cast<void>(tree.directory("forest"));

    if (::geteuid() == 0) {
        std::puts("storage_usage: skipping the queued-cancellation case for a superuser");
        return;
    }

    std::vector<fs::path> blocked;
    blocked.reserve(blocked_count);
    for (int index = 0; index < blocked_count; ++index) {
        blocked.push_back(tree.directory("forest/closed_" + std::to_string(index)));
    }
    std::error_code permission_error;
    for (const fs::path& directory : blocked) {
        fs::permissions(directory, fs::perms::none, fs::perm_options::replace, permission_error);
    }
    check(!permission_error, "the fixture directories can be made unreadable");

    // The root, the forest, and every directory inside it: cancelling on the
    // last of those lands precisely when the listing loop has just ended and
    // the queue of directories still to enter is at its longest. Nothing but
    // the check between directories can stop the walk there.
    constexpr std::uint64_t last_listed = 2 + blocked_count;

    UsageRecorder recorder;
    UsageScanner scanner;
    UsageOptions options;
    options.progress_interval = 1;
    UsageScanner::Request request = request_for(tree.root(), recorder, options);
    request.on_progress = [&recorder, &scanner](std::uint64_t, UsageProgress progress) {
        const bool queue_is_full = progress.entries_visited >= last_listed;
        recorder.record_progress(std::move(progress));
        if (queue_is_full) {
            scanner.cancel();
        }
    };
    static_cast<void>(scanner.start(std::move(request)));

    check(recorder.wait_for_completion(), "the walk reports completion");
    const UsageSummary summary = recorder.summary();
    for (const fs::path& directory : blocked) {
        fs::permissions(directory, fs::perms::owner_all, fs::perm_options::replace,
                        permission_error);
    }

    check(summary.cancelled, "the walk is reported as cancelled");
    check(summary.totals.unreadable_directories <= 2,
          "cancellation is checked between directories, so a long queue of them is "
          "abandoned rather than drained");
    scanner.wait_idle();
}

/// A directory on another filesystem, removed however the test leaves.
class ForeignDirectory {
  public:
    ForeignDirectory() {
        path_ = fs::path("/dev/shm") / ("odysea_usage_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
        usable_ = !ec;
    }

    ForeignDirectory(const ForeignDirectory&) = delete;
    ForeignDirectory& operator=(const ForeignDirectory&) = delete;
    ForeignDirectory(ForeignDirectory&&) = delete;
    ForeignDirectory& operator=(ForeignDirectory&&) = delete;

    ~ForeignDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }
    [[nodiscard]] bool usable() const noexcept { return usable_; }

  private:
    fs::path path_;
    bool usable_ = false;
};

void test_crossing_a_filesystem_boundary_is_the_callers_decision() {
    const odysea::test::TemporaryTree tree("usage_boundary");
    const ForeignDirectory foreign;
    if (!foreign.usable() || device_of(foreign.path()) == device_of(tree.root())) {
        std::puts("storage_usage: skipping the boundary case, no second filesystem is available");
        return;
    }

    const fs::path payload_file = foreign.path() / "payload.bin";
    {
        std::ofstream stream(payload_file, std::ios::binary | std::ios::trunc);
        stream << payload(5000);
    }
    std::error_code link_error;
    fs::create_directory_symlink(foreign.path(), tree.root() / "elsewhere", link_error);
    check(!link_error, "the fixture can reach the second filesystem");
    if (link_error) {
        return;
    }

    UsageOptions staying;
    staying.follow_directory_symlinks = true;
    const UsageSummary stayed = walk(tree.root(), staying);
    check(stayed.totals.skipped_boundaries == 1,
          "a directory on another filesystem is reported as a skipped boundary by default");
    check(stayed.totals.apparent_bytes < 5000,
          "staying on one filesystem leaves the other filesystem's bytes uncounted");

    UsageOptions crossing = staying;
    crossing.cross_filesystem_boundaries = true;
    const UsageSummary crossed = walk(tree.root(), crossing);
    check(crossed.totals.skipped_boundaries == 0, "a walk told to cross boundaries skips none");
    check(crossed.totals.apparent_bytes >= 5000,
          "crossing the boundary counts what lies on the other side");
    check(crossed.totals.apparent_bytes > stayed.totals.apparent_bytes,
          "the boundary policy is the only difference between the two walks");
}

void test_progress_reports_are_growing_snapshots() {
    const odysea::test::TemporaryTree tree("usage_progress");
    for (int branch = 0; branch < 4; ++branch) {
        const std::string base = "part_" + std::to_string(branch) + "/";
        for (int leaf = 0; leaf < 60; ++leaf) {
            static_cast<void>(tree.file(base + "leaf_" + std::to_string(leaf) + ".bin"));
        }
    }

    UsageRecorder recorder;
    UsageScanner scanner;
    UsageOptions options;
    options.progress_interval = 16;
    const std::uint64_t token = scanner.start(request_for(tree.root(), recorder, options));
    check(recorder.wait_for_completion(), "the walk completes");

    const UsageSummary summary = recorder.summary();
    const std::vector<UsageProgress> reports = recorder.reports();
    check(reports.size() >= 4, "a long walk reports progress more than once");
    if (reports.empty()) {
        return;
    }

    bool tokens_match = true;
    bool counts_grow = true;
    bool totals_grow = true;
    std::size_t previous_children = 0;
    std::uintmax_t previous_bytes = 0;
    std::uint64_t previous_visited = 0;
    for (const UsageProgress& report : reports) {
        tokens_match = tokens_match && report.token == token && report.root == tree.root();
        counts_grow = counts_grow && report.children.size() >= previous_children &&
                      report.entries_visited >= previous_visited;
        totals_grow = totals_grow && report.totals.apparent_bytes >= previous_bytes;
        previous_children = report.children.size();
        previous_bytes = report.totals.apparent_bytes;
        previous_visited = report.entries_visited;
    }
    check(tokens_match, "every report names the request it belongs to");
    check(counts_grow, "each report is a complete snapshot, never a shrinking one");
    check(totals_grow, "running totals only grow while a walk proceeds");

    const UsageProgress& last = reports.back();
    check(last.entries_visited <= summary.entries_visited,
          "the summary is never behind the last progress report");
    check(last.children.size() == summary.children.size(),
          "the last report already lists every child the summary reports");
    const bool any_finished =
        std::ranges::any_of(last.children, [](const UsageChild& child) { return child.finished; });
    check(any_finished, "children are reported as finished while the walk is still running");
}

void test_a_finished_child_is_reported_even_in_a_small_tree() {
    const odysea::test::TemporaryTree tree("usage_settle");
    for (int branch = 0; branch < 3; ++branch) {
        const std::string base = "part_" + std::to_string(branch) + "/";
        static_cast<void>(tree.file(base + "one.bin", payload(40)));
        static_cast<void>(tree.file(base + "two.bin", payload(40)));
    }
    static_cast<void>(tree.file("loose.bin", payload(40)));

    UsageRecorder recorder;
    UsageScanner scanner;
    UsageOptions options;
    // Far more entries than this tree holds, so no report can come from the
    // interval. Whatever arrives, arrives because a child settled.
    options.progress_interval = 100000;
    static_cast<void>(scanner.start(request_for(tree.root(), recorder, options)));
    check(recorder.wait_for_completion(), "the walk completes");

    const std::vector<UsageProgress> reports = recorder.reports();
    check(reports.size() == 3,
          "a tree too small to reach the progress interval still reports each child as it "
          "settles");
    if (reports.empty()) {
        return;
    }
    const auto finished = static_cast<std::size_t>(std::ranges::count_if(
        reports.back().children, [](const UsageChild& child) { return child.finished; }));
    check(finished == 4, "the last report shows every child settled, files included");
}

void test_hidden_entries_are_always_counted() {
    const odysea::test::TemporaryTree tree("usage_hidden");
    const fs::path hidden = tree.file(".hidden.bin", payload(600));
    const fs::path visible = tree.file("visible.bin", payload(600));

    const UsageSummary summary = walk(tree.root());
    check(summary.children.size() == 2,
          "a hidden entry occupies real space and is reported like any other");
    check(summary.totals.file_count == 2, "a hidden entry is counted");
    check(summary.totals.apparent_bytes == raw_total({tree.root(), hidden, visible}).apparent,
          "hiding an entry from a listing does not change a measurement");
}

void test_an_unusable_root_reports_an_error() {
    const odysea::test::TemporaryTree tree("usage_root");
    const fs::path file = tree.file("plain.bin", payload(10));

    const UsageSummary missing = walk(tree.root() / "absent");
    check(missing.error == std::errc::no_such_file_or_directory,
          "a missing root is reported through the summary");
    check(missing.children.empty() && missing.totals.apparent_bytes == 0,
          "a failed walk reports no totals to render");

    const UsageSummary not_a_directory = walk(file);
    check(not_a_directory.error == std::errc::not_a_directory,
          "a root that is not a directory is reported rather than half-measured");
}

void test_children_sort_into_presentation_order() {
    std::vector<UsageChild> children(4);
    children[0].name = "small";
    children[0].totals.allocated_bytes = 100;
    children[0].totals.apparent_bytes = 100;
    children[1].name = "largest";
    children[1].totals.allocated_bytes = 900;
    children[1].totals.apparent_bytes = 900;
    children[2].name = "beta";
    children[2].totals.allocated_bytes = 100;
    children[2].totals.apparent_bytes = 400;
    children[3].name = "alpha";
    children[3].totals.allocated_bytes = 100;
    children[3].totals.apparent_bytes = 400;

    sort_usage_children(children);
    const std::vector<std::string> order{children[0].name, children[1].name, children[2].name,
                                         children[3].name};
    check(order == std::vector<std::string>({"largest", "alpha", "beta", "small"}),
          "children order by allocated size, then apparent size, then name");
}

} // namespace

int main() {
    test_totals_match_a_tree_of_known_sizes();
    test_apparent_and_allocated_sizes_stay_apart();
    test_a_file_reached_twice_is_counted_once();
    test_a_single_link_file_reached_twice_is_counted_once();
    test_a_symlink_cycle_terminates();
    test_an_unreadable_subtree_reports_a_partial_result();
    test_cancellation_stops_a_walk_at_depth();
    test_cancellation_stops_a_queue_of_unreadable_directories();
    test_crossing_a_filesystem_boundary_is_the_callers_decision();
    test_progress_reports_are_growing_snapshots();
    test_a_finished_child_is_reported_even_in_a_small_tree();
    test_hidden_entries_are_always_counted();
    test_an_unusable_root_reports_an_error();
    test_children_sort_into_presentation_order();
    return odysea::test::report("storage_usage");
}

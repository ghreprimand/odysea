// Headless tests for incremental directory watching.
//
// Filesystem notifications are asynchronous, so each expectation is polled
// until it holds or a generous deadline expires. A failing test therefore
// reports a genuine missing event rather than a race.
#include "odysea/core/directory_watcher.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;
using namespace std::chrono_literals;

namespace {

constexpr auto event_deadline = 5s;

using ChangePredicate = std::function<bool(const std::vector<DirectoryChange>&)>;

/// Collect changes until the predicate accepts the accumulated batch list.
std::vector<DirectoryChange> collect_until(DirectoryWatcher& watcher,
                                           const ChangePredicate& satisfied) {
    std::vector<DirectoryChange> collected;
    const auto deadline = std::chrono::steady_clock::now() + event_deadline;
    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code ec;
        std::vector<DirectoryChange> batch = watcher.wait(200ms, ec);
        if (ec) {
            break;
        }
        collected.insert(collected.end(), batch.begin(), batch.end());
        if (satisfied(collected)) {
            break;
        }
    }
    return collected;
}

ChangePredicate saw(ChangeKind kind, std::string name) {
    return [kind, moved_name = std::move(name)](const std::vector<DirectoryChange>& changes) {
        return std::ranges::any_of(changes, [&](const DirectoryChange& change) {
            return change.kind == kind && change.name == moved_name;
        });
    };
}

std::optional<DirectoryChange> find_change(const std::vector<DirectoryChange>& changes,
                                           ChangeKind kind, const std::string& name) {
    const auto found = std::ranges::find_if(changes, [&](const DirectoryChange& change) {
        return change.kind == kind && change.name == name;
    });
    if (found == changes.end()) {
        return std::nullopt;
    }
    return *found;
}

DirectoryWatcher make_watcher() {
    std::error_code ec;
    std::optional<DirectoryWatcher> watcher = DirectoryWatcher::create(ec);
    check(!ec && watcher.has_value(), "a watcher should be creatable");
    return std::move(*watcher);
}

void test_missing_directory_is_rejected() {
    const odysea::test::TemporaryTree tree("watch_missing");
    DirectoryWatcher watcher = make_watcher();

    std::error_code ec;
    check(!watcher.add(tree.root() / "absent", ec), "watching a missing directory fails");
    check(ec == std::errc::no_such_file_or_directory, "the failure names the missing directory");
    check(watcher.watched().empty(), "a failed watch is not recorded");
}

void test_creation_and_deletion() {
    const odysea::test::TemporaryTree tree("watch_create");
    DirectoryWatcher watcher = make_watcher();

    std::error_code ec;
    check(watcher.add(tree.root(), ec), "watching a real directory succeeds");
    check(watcher.watched().size() == 1, "the watched directory is recorded");

    const fs::path file = tree.file("appeared.txt", "hello");
    std::vector<DirectoryChange> changes =
        collect_until(watcher, saw(ChangeKind::Created, "appeared.txt"));
    const std::optional<DirectoryChange> created =
        find_change(changes, ChangeKind::Created, "appeared.txt");
    check(created.has_value(), "creating a file reports a Created change");
    check(created.has_value() && created->directory == tree.root(),
          "a change carries the directory it belongs to");
    check(created.has_value() && !created->is_directory, "a created file is not a directory");

    fs::remove(file);
    changes = collect_until(watcher, saw(ChangeKind::Deleted, "appeared.txt"));
    check(find_change(changes, ChangeKind::Deleted, "appeared.txt").has_value(),
          "removing a file reports a Deleted change");
}

void test_modification_and_subdirectories() {
    const odysea::test::TemporaryTree tree("watch_modify");
    const fs::path file = tree.file("notes.txt", "one");
    DirectoryWatcher watcher = make_watcher();

    std::error_code ec;
    check(watcher.add(tree.root(), ec), "watching a real directory succeeds");

    {
        std::ofstream stream(file, std::ios::app);
        stream << "two";
    }
    std::vector<DirectoryChange> changes =
        collect_until(watcher, saw(ChangeKind::Modified, "notes.txt"));
    check(find_change(changes, ChangeKind::Modified, "notes.txt").has_value(),
          "writing to a file reports a Modified change");

    static_cast<void>(tree.directory("child"));
    changes = collect_until(watcher, saw(ChangeKind::Created, "child"));
    const std::optional<DirectoryChange> created =
        find_change(changes, ChangeKind::Created, "child");
    check(created.has_value() && created->is_directory,
          "a created directory is flagged as a directory");
}

void test_rename_pairs_share_a_cookie() {
    const odysea::test::TemporaryTree tree("watch_rename");
    const fs::path file = tree.file("before.txt", "x");
    DirectoryWatcher watcher = make_watcher();

    std::error_code ec;
    check(watcher.add(tree.root(), ec), "watching a real directory succeeds");

    fs::rename(file, tree.root() / "after.txt");
    const std::vector<DirectoryChange> changes =
        collect_until(watcher, saw(ChangeKind::MovedTo, "after.txt"));

    const std::optional<DirectoryChange> from =
        find_change(changes, ChangeKind::MovedFrom, "before.txt");
    const std::optional<DirectoryChange> to =
        find_change(changes, ChangeKind::MovedTo, "after.txt");
    check(from.has_value(), "a rename reports the departing name");
    check(to.has_value(), "a rename reports the arriving name");
    check(from.has_value() && to.has_value() && from->rename_cookie != 0 &&
              from->rename_cookie == to->rename_cookie,
          "both halves of a rename share a non-zero cookie");
}

void test_watch_removal_stops_events() {
    const odysea::test::TemporaryTree tree("watch_remove");
    DirectoryWatcher watcher = make_watcher();

    std::error_code ec;
    check(watcher.add(tree.root(), ec), "watching a real directory succeeds");
    watcher.remove(tree.root());
    check(watcher.watched().empty(), "removing a watch clears it from the list");

    static_cast<void>(tree.file("ignored.txt"));
    std::error_code wait_ec;
    const std::vector<DirectoryChange> batch = watcher.wait(300ms, wait_ec);
    check(!wait_ec, "waiting after a removal is not an error");
    const bool reported_entry = std::ranges::any_of(
        batch, [](const DirectoryChange& change) { return change.name == "ignored.txt"; });
    check(!reported_entry, "an unwatched directory produces no entry changes");
}

void test_deleting_the_watched_directory() {
    const odysea::test::TemporaryTree tree("watch_self");
    const fs::path target = tree.directory("doomed");
    DirectoryWatcher watcher = make_watcher();

    std::error_code ec;
    check(watcher.add(target, ec), "watching a real directory succeeds");
    fs::remove(target);

    const std::vector<DirectoryChange> changes =
        collect_until(watcher, [](const std::vector<DirectoryChange>& seen) {
            return std::ranges::any_of(seen, [](const DirectoryChange& change) {
                return change.kind == ChangeKind::WatchRemoved;
            });
        });
    const bool reported = std::ranges::any_of(changes, [](const DirectoryChange& change) {
        return change.kind == ChangeKind::WatchRemoved;
    });
    check(reported, "deleting the watched directory reports WatchRemoved");
    check(watcher.watched().empty(), "a dropped watch is forgotten");
}

void test_interrupt_releases_a_blocking_wait() {
    const odysea::test::TemporaryTree tree("watch_interrupt");
    DirectoryWatcher watcher = make_watcher();

    std::error_code ec;
    check(watcher.add(tree.root(), ec), "watching a real directory succeeds");

    std::thread waker([&watcher] {
        std::this_thread::sleep_for(100ms);
        watcher.interrupt();
    });

    const auto started = std::chrono::steady_clock::now();
    std::error_code wait_ec;
    const std::vector<DirectoryChange> batch =
        watcher.wait(DirectoryWatcher::wait_forever, wait_ec);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    waker.join();

    check(!wait_ec, "an interrupted wait is not an error");
    check(batch.empty(), "an interrupted wait reports no changes");
    check(elapsed < event_deadline, "interrupt releases a blocking wait promptly");
}

void test_multiple_directories_are_distinguished() {
    const odysea::test::TemporaryTree tree("watch_multiple");
    const fs::path left = tree.directory("left");
    const fs::path right = tree.directory("right");
    DirectoryWatcher watcher = make_watcher();

    std::error_code ec;
    check(watcher.add(left, ec) && watcher.add(right, ec), "two directories can be watched");
    check(watcher.watched().size() == 2, "both watches are recorded");

    static_cast<void>(tree.file("right/only.txt"));
    const std::vector<DirectoryChange> changes =
        collect_until(watcher, saw(ChangeKind::Created, "only.txt"));
    const std::optional<DirectoryChange> created =
        find_change(changes, ChangeKind::Created, "only.txt");
    check(created.has_value() && created->directory == right,
          "a change names the directory it came from");
}

} // namespace

int main() {
    test_missing_directory_is_rejected();
    test_creation_and_deletion();
    test_modification_and_subdirectories();
    test_rename_pairs_share_a_cookie();
    test_watch_removal_stops_events();
    test_deleting_the_watched_directory();
    test_interrupt_releases_a_blocking_wait();
    test_multiple_directories_are_distinguished();
    return odysea::test::report("directory_watcher");
}

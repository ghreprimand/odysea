// Headless tests for cancellable off-thread directory scanning.
#include "odysea/core/directory_scanner.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;
using namespace std::chrono_literals;

namespace {

/// Thread-safe collector for the results a scan delivers.
class ScanRecorder {
  public:
    void record_batch(std::vector<Entry> entries) {
        const std::lock_guard<std::mutex> guard(mutex_);
        ++batch_count_;
        batch_thread_ = std::this_thread::get_id();
        for (Entry& entry : entries) {
            names_.push_back(entry.name);
        }
    }

    void record_completion(ScanSummary summary) {
        {
            const std::lock_guard<std::mutex> guard(mutex_);
            summary_ = std::move(summary);
        }
        finished_.notify_all();
    }

    /// Wait for the completion callback. Returns false if it never arrived.
    bool wait_for_completion() {
        std::unique_lock<std::mutex> guard(mutex_);
        return finished_.wait_for(guard, 10s, [this] { return summary_.has_value(); });
    }

    [[nodiscard]] ScanSummary summary() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return summary_.value_or(ScanSummary{});
    }

    [[nodiscard]] std::vector<std::string> names() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return names_;
    }

    [[nodiscard]] std::size_t batch_count() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return batch_count_;
    }

    [[nodiscard]] std::thread::id batch_thread() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return batch_thread_;
    }

  private:
    std::mutex mutex_;
    std::condition_variable finished_;
    std::vector<std::string> names_;
    std::optional<ScanSummary> summary_;
    std::size_t batch_count_ = 0;
    std::thread::id batch_thread_;
};

DirectoryScanner::Request request_for(const fs::path& directory, ScanRecorder& recorder,
                                      std::size_t batch_size, bool show_hidden = false) {
    DirectoryScanner::Request request;
    request.directory = directory;
    request.options.show_hidden = show_hidden;
    request.batch_size = batch_size;
    request.on_batch = [&recorder](std::uint64_t, std::vector<Entry> entries) {
        recorder.record_batch(std::move(entries));
    };
    request.on_complete = [&recorder](ScanSummary summary) {
        recorder.record_completion(std::move(summary));
    };
    return request;
}

void populate(const odysea::test::TemporaryTree& tree, const std::string& prefix,
              std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        static_cast<void>(tree.file(prefix + "/entry_" + std::to_string(index) + ".txt"));
    }
}

void test_streaming_scan_delivers_every_entry() {
    const odysea::test::TemporaryTree tree("scan_stream");
    constexpr std::size_t entry_count = 500;
    populate(tree, "many", entry_count);

    ScanRecorder recorder;
    DirectoryScanner scanner;
    const std::uint64_t token = scanner.start(request_for(tree.root() / "many", recorder, 64));

    check(recorder.wait_for_completion(), "a scan reports completion");
    const ScanSummary summary = recorder.summary();
    check(summary.token == token, "the summary answers the request token");
    check(!summary.error, "scanning a readable directory reports no error");
    check(!summary.cancelled, "an undisturbed scan is not cancelled");
    check(summary.entry_count == entry_count, "the summary counts every delivered entry");

    std::vector<std::string> names = recorder.names();
    check(names.size() == entry_count, "every entry arrives through a batch");
    std::ranges::sort(names);
    check(std::ranges::adjacent_find(names) == names.end(), "no entry is delivered twice");
    check(recorder.batch_count() >= entry_count / 64, "results arrive incrementally, not at once");
    check(recorder.batch_thread() != std::this_thread::get_id(),
          "batches are delivered off the calling thread");
}

void test_hidden_entries_follow_the_options() {
    const odysea::test::TemporaryTree tree("scan_hidden");
    static_cast<void>(tree.file("mixed/visible.txt"));
    static_cast<void>(tree.file("mixed/.hidden"));

    ScanRecorder visible_recorder;
    DirectoryScanner scanner;
    static_cast<void>(scanner.start(request_for(tree.root() / "mixed", visible_recorder, 16)));
    check(visible_recorder.wait_for_completion(), "the default scan completes");
    check(visible_recorder.summary().entry_count == 1, "dotfiles are filtered by default");

    ScanRecorder full_recorder;
    static_cast<void>(scanner.start(request_for(tree.root() / "mixed", full_recorder, 16, true)));
    check(full_recorder.wait_for_completion(), "the show-hidden scan completes");
    check(full_recorder.summary().entry_count == 2, "show_hidden includes the dotfile");
}

void test_missing_directory_reports_an_error() {
    const odysea::test::TemporaryTree tree("scan_missing");

    ScanRecorder recorder;
    DirectoryScanner scanner;
    static_cast<void>(scanner.start(request_for(tree.root() / "absent", recorder, 16)));

    check(recorder.wait_for_completion(), "a failing scan still reports completion");
    const ScanSummary summary = recorder.summary();
    check(summary.error == std::errc::no_such_file_or_directory,
          "a missing directory is reported through the summary");
    check(summary.entry_count == 0, "a failing scan delivers no entries");
    check(!summary.cancelled, "a failing scan is not reported as cancelled");
}

void test_a_new_request_cancels_the_one_in_flight() {
    const odysea::test::TemporaryTree tree("scan_supersede");
    populate(tree, "first", 400);
    populate(tree, "second", 5);

    // The first scan blocks inside its initial batch until released, which
    // makes the hand-over deterministic rather than timing-dependent.
    std::mutex gate_mutex;
    std::condition_variable gate;
    bool released = false;
    bool first_batch_seen = false;

    ScanRecorder first_recorder;
    ScanRecorder second_recorder;
    DirectoryScanner scanner;

    DirectoryScanner::Request blocking = request_for(tree.root() / "first", first_recorder, 8);
    blocking.on_batch = [&](std::uint64_t, std::vector<Entry> entries) {
        first_recorder.record_batch(std::move(entries));
        std::unique_lock<std::mutex> guard(gate_mutex);
        if (!first_batch_seen) {
            first_batch_seen = true;
            gate.notify_all();
            gate.wait_for(guard, 10s, [&] { return released; });
        }
    };

    const std::uint64_t first_token = scanner.start(std::move(blocking));
    {
        std::unique_lock<std::mutex> guard(gate_mutex);
        check(gate.wait_for(guard, 10s, [&] { return first_batch_seen; }),
              "the first scan starts delivering before it is superseded");
    }

    const std::uint64_t second_token =
        scanner.start(request_for(tree.root() / "second", second_recorder, 8));
    check(second_token > first_token, "each request receives a fresh token");
    {
        const std::lock_guard<std::mutex> guard(gate_mutex);
        released = true;
    }
    gate.notify_all();

    check(first_recorder.wait_for_completion(), "the superseded scan reports completion");
    check(first_recorder.summary().cancelled, "the superseded scan is reported as cancelled");
    check(first_recorder.names().size() < 400,
          "a cancelled scan stops before delivering the whole directory");

    check(second_recorder.wait_for_completion(), "the newer scan runs after the cancelled one");
    const ScanSummary second_summary = second_recorder.summary();
    check(second_summary.token == second_token, "the newer summary answers the newer token");
    check(!second_summary.cancelled && second_summary.entry_count == 5,
          "the newer scan completes normally");
}

void test_explicit_cancel_stops_a_scan() {
    const odysea::test::TemporaryTree tree("scan_cancel");
    populate(tree, "big", 400);

    std::mutex gate_mutex;
    std::condition_variable gate;
    bool released = false;
    bool batch_seen = false;

    ScanRecorder recorder;
    DirectoryScanner scanner;

    DirectoryScanner::Request blocking = request_for(tree.root() / "big", recorder, 8);
    blocking.on_batch = [&](std::uint64_t, std::vector<Entry> entries) {
        recorder.record_batch(std::move(entries));
        std::unique_lock<std::mutex> guard(gate_mutex);
        if (!batch_seen) {
            batch_seen = true;
            gate.notify_all();
            gate.wait_for(guard, 10s, [&] { return released; });
        }
    };

    static_cast<void>(scanner.start(std::move(blocking)));
    {
        std::unique_lock<std::mutex> guard(gate_mutex);
        check(gate.wait_for(guard, 10s, [&] { return batch_seen; }), "the scan starts");
    }

    scanner.cancel();
    {
        const std::lock_guard<std::mutex> guard(gate_mutex);
        released = true;
    }
    gate.notify_all();

    check(recorder.wait_for_completion(), "a cancelled scan reports completion");
    check(recorder.summary().cancelled, "an explicit cancel is reported in the summary");
    scanner.wait_idle();
}

void test_destruction_during_a_scan_is_clean() {
    const odysea::test::TemporaryTree tree("scan_teardown");
    populate(tree, "wide", 800);

    ScanRecorder recorder;
    {
        DirectoryScanner scanner;
        static_cast<void>(scanner.start(request_for(tree.root() / "wide", recorder, 4)));
        // Leaving the scope tears the scanner down mid-scan: the worker must
        // stop promptly and the destructor must not deadlock.
    }
    check(true, "destroying a scanner during a scan completes without hanging");
}

} // namespace

int main() {
    test_streaming_scan_delivers_every_entry();
    test_hidden_entries_follow_the_options();
    test_missing_directory_reports_an_error();
    test_a_new_request_cancels_the_one_in_flight();
    test_explicit_cancel_stops_a_scan();
    test_destruction_during_a_scan_is_clean();
    return odysea::test::report("directory_scanner");
}

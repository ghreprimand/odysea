// Headless tests for transfer reporting, estimates, and cooperative control.
//
// Two levels. The engine-level cases drive the internal seam with a supplied
// clock, so a rule about intervals is asserted without spending them: this
// project has already retired one bound that decided by machine load, and a
// test that waits a hundred milliseconds to observe a hundred-millisecond
// rule is that bound again. The end-to-end cases go through the public copy
// and move, where what matters is what a cancelled or interrupted transfer
// leaves on disk.
#include "odysea/core/transfer.hpp"

#include "odysea/core/operation_journal.hpp"

#include "file_operations_internal.hpp"
#include "test_support.hpp"
#include "transfer_engine.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;
using namespace std::chrono_literals;

namespace {

/// A clock the test moves by hand.
///
/// Held by shared pointer because the engine keeps the callable and the case
/// keeps the dial, and they have to be looking at the same value.
struct ManualClock {
    std::chrono::steady_clock::time_point now;

    void advance(std::chrono::milliseconds interval) { now += interval; }
};

detail::TransferClock reading(const std::shared_ptr<ManualClock>& clock) {
    return [clock] { return clock->now; };
}

/// Processor time this process has used, in milliseconds.
///
/// Work rather than lateness. A cost measured on the wall clock is the work
/// plus every interval the machine spent elsewhere, and a ratio of two such
/// readings across different amounts of work has already been retired in this
/// repository for measuring the machine instead of the code.
double processor_milliseconds() {
    timespec spec{};
    if (::clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &spec) != 0) {
        return -1.0;
    }
    return (static_cast<double>(spec.tv_sec) * 1000.0) +
           (static_cast<double>(spec.tv_nsec) / 1000000.0);
}

/// How large each file in a built fixture is. A named type so it cannot be
/// transposed with the count beside it, which is the same guard the model's
/// settle budget carries for the same reason.
struct FileSize {
    std::size_t bytes = 0;
};

/// Builds a directory of `count` files, each holding `size` bytes.
void build_tree(const fs::path& root, int count, FileSize size) {
    std::error_code ec;
    fs::create_directories(root, ec);
    const std::string contents(size.bytes, 'x');
    for (int index = 0; index < count; ++index) {
        std::ofstream stream(root / ("entry-" + std::to_string(index)), std::ios::binary);
        stream << contents;
    }
}

/// Whether any entry in `directory` is one the operations created for their
/// own use. A transfer that failed or was cancelled must leave none.
bool holds_no_working_entry(const fs::path& directory) {
    std::error_code ec;
    fs::directory_iterator element(directory, ec);
    if (ec) {
        return true;
    }
    const fs::directory_iterator end;
    for (; element != end; ++element) {
        if (is_working_entry(element->path().filename().string())) {
            return false;
        }
    }
    return true;
}

std::uint64_t count_entries(const fs::path& root) {
    std::error_code ec;
    std::uint64_t total = 1;
    for (fs::recursive_directory_iterator element(root, ec), end; element != end; ++element) {
        ++total;
    }
    return total;
}

// ---------------------------------------------------------------------------
// The unit progress is measured in.
// ---------------------------------------------------------------------------

void test_an_entry_is_charged_beyond_its_bytes() {
    check(transfer_work_units(0, 1) == entry_work_bytes,
          "an entry with no contents still costs something");
    check(transfer_work_units(100, 2) == 100 + (2 * entry_work_bytes),
          "bytes and entries are summed in one unit");

    // The case the unit exists for. A thousand empty files is real work, and
    // a bar counting bytes alone sits at zero for all of it.
    TransferProgress halfway;
    halfway.totals = TransferTotals{.bytes = 0, .entries = 1000, .known = true};
    halfway.entries_done = 500;
    halfway.bytes_done = 0;
    check(halfway.completed_fraction_known(), "a measured tree of empty files can report progress");
    check(halfway.completed_fraction() > 0.49 && halfway.completed_fraction() < 0.51,
          "half of a thousand empty files reads as half done, not as none");
}

void test_a_fraction_is_withheld_rather_than_guessed() {
    TransferProgress unmeasured;
    unmeasured.totals = TransferTotals{.bytes = 4096, .entries = 4, .known = false};
    unmeasured.bytes_done = 2048;
    check(!unmeasured.completed_fraction_known(),
          "a transfer that was never measured offers no fraction");

    // A source that grew after it was measured. Reporting more than all of
    // itself done would be worse than reporting all of it.
    TransferProgress overrun;
    overrun.totals = TransferTotals{.bytes = 100, .entries = 1, .known = true};
    overrun.bytes_done = 100000;
    overrun.entries_done = 1;
    check(overrun.completed_fraction() <= 1.0, "a fraction is never more than all of it");
}

// ---------------------------------------------------------------------------
// Reporting cadence: bounded in time, never in work.
// ---------------------------------------------------------------------------

void test_reporting_is_bounded_by_time_and_not_by_the_size_of_the_job() {
    // The cost bound, and it is a count rather than a duration. Reporting
    // once per entry would make the reports the dominant cost of a large
    // directory, and a wall-clock bound on that would be a bound on the
    // machine. With the clock held still, the number of reports a transfer
    // makes is exactly the number it is obliged to make, whatever it moves.
    const odysea::test::TemporaryTree tree("transfer_cadence");
    const fs::path small = tree.root() / "small";
    const fs::path large = tree.root() / "large";
    build_tree(small, 50, FileSize{.bytes = 16});
    build_tree(large, 2000, FileSize{.bytes = 16});

    const auto measure = [&tree](const fs::path& source, const std::string& label) {
        const auto clock = std::make_shared<ManualClock>();
        std::uint64_t reports = 0;
        TransferOptions options;
        options.on_progress = [&reports](const TransferProgress&) { ++reports; };
        options.report_interval = 100ms;

        detail::TransferRun run(options, reading(clock));
        const std::error_code ec =
            detail::run_transfer(source, tree.root() / ("copy-" + label), run);
        check(!ec, "a transfer with a still clock completes");
        return reports;
    };

    const std::uint64_t small_reports = measure(small, "small");
    const std::uint64_t large_reports = measure(large, "large");

    // Two: the first report of the measuring phase and the first of the
    // transferring phase. Everything else is due only once the interval has
    // passed, and it never does.
    check(small_reports == 2, "a still clock allows exactly the two obligatory reports");
    check(large_reports == small_reports,
          "forty times the entries produces the same number of reports");
}

void test_a_report_is_made_once_the_interval_has_passed() {
    const odysea::test::TemporaryTree tree("transfer_interval");
    const fs::path source = tree.root() / "source";
    build_tree(source, 40, FileSize{.bytes = 64});

    const auto clock = std::make_shared<ManualClock>();
    std::uint64_t reports = 0;
    TransferOptions options;
    options.report_interval = 100ms;
    options.on_progress = [&reports, clock](const TransferProgress&) {
        ++reports;
        // Every report moves the clock past the interval, so the next entry
        // is due one. This is the opposite extreme from the still clock: it
        // shows the rule admits reports rather than suppressing them.
        clock->advance(150ms);
    };

    detail::TransferRun run(options, reading(clock));
    const std::error_code ec = detail::run_transfer(source, tree.root() / "copy", run);
    check(!ec, "a transfer whose clock keeps moving completes");
    check(reports > 10, "a clock past the interval produces reports as the transfer proceeds");
}

void test_an_unwatched_transfer_neither_measures_nor_reports() {
    const odysea::test::TemporaryTree tree("transfer_unwatched");
    const fs::path source = tree.root() / "source";
    build_tree(source, 20, FileSize{.bytes = 32});

    const auto clock = std::make_shared<ManualClock>();
    TransferOptions options;
    detail::TransferRun run(options, reading(clock));
    const std::error_code ec = detail::run_transfer(source, tree.root() / "copy", run);
    check(!ec, "an unwatched transfer completes");
    check(run.report_count() == 0, "an unwatched transfer makes no reports");
    check(!run.totals().known, "an unwatched transfer does not pay for a measurement pass");
    check(fs::exists(tree.root() / "copy/entry-0"), "an unwatched transfer still copies");
}

// ---------------------------------------------------------------------------
// What is reported.
// ---------------------------------------------------------------------------

void test_a_watched_transfer_reports_what_it_has_done() {
    const odysea::test::TemporaryTree tree("transfer_reports");
    const fs::path source = tree.root() / "source";
    build_tree(source, 8, FileSize{.bytes = 1024});

    const auto clock = std::make_shared<ManualClock>();
    std::vector<TransferProgress> reports;
    TransferOptions options;
    options.report_interval = 0ms;
    options.on_progress = [&reports, clock](const TransferProgress& progress) {
        reports.push_back(progress);
        clock->advance(10ms);
    };

    detail::TransferRun run(options, reading(clock));
    const std::error_code ec = detail::run_transfer(source, tree.root() / "copy", run);
    check(!ec, "the watched transfer completes");
    check(!reports.empty(), "a watched transfer reports");
    check(reports.front().phase == TransferPhase::Measuring, "the first report is the measurement");

    const TransferProgress& last = reports.back();
    check(last.phase == TransferPhase::Transferring, "the last report is the transfer");
    check(last.totals.known, "a small tree is measured within its budget");
    check(last.totals.entries == 9, "the totals count the directory and its eight entries");
    check(last.totals.bytes == std::uint64_t{8} * 1024, "the totals sum the file contents");
    check(last.entries_done == last.totals.entries, "every entry is reported as done");
    check(last.bytes_done == last.totals.bytes, "every byte is reported as done");
    check(!last.current_entry.empty(), "a report names the entry it was working on");
}

void test_an_estimate_is_withheld_until_the_window_holds_enough() {
    const odysea::test::TemporaryTree tree("transfer_estimate");
    const fs::path source = tree.root() / "source";
    build_tree(source, 400, FileSize{.bytes = 256});

    const auto clock = std::make_shared<ManualClock>();
    std::vector<TransferProgress> reports;
    TransferOptions options;
    options.report_interval = 100ms;
    options.on_progress = [&reports, clock](const TransferProgress& progress) {
        reports.push_back(progress);
        clock->advance(100ms);
    };

    detail::TransferRun run(options, reading(clock));
    const std::error_code ec = detail::run_transfer(source, tree.root() / "copy", run);
    check(!ec, "the measured transfer completes");
    check(reports.size() > 8, "the fixture produces enough reports to fill the window");

    check(!reports.front().estimate.throughput_known,
          "the first report offers no rate, because nothing has been timed yet");
    check(!reports.front().estimate.remaining_known,
          "no time remaining is offered before a rate is known");

    bool became_known = false;
    for (const TransferProgress& progress : reports) {
        if (progress.estimate.throughput_known) {
            became_known = true;
            check(progress.estimate.work_units_per_second > 0.0,
                  "a rate that is offered is a rate above zero");
            check(progress.estimate.remaining_known,
                  "a known rate over measured totals yields a time remaining");
        }
    }
    check(became_known, "a rate is offered once the window covers enough time");
}

void test_no_time_remaining_is_offered_without_totals() {
    const odysea::test::TemporaryTree tree("transfer_estimate_unmeasured");
    const fs::path source = tree.root() / "source";
    build_tree(source, 200, FileSize{.bytes = 256});

    const auto clock = std::make_shared<ManualClock>();
    std::vector<TransferProgress> reports;
    TransferOptions options;
    options.report_interval = 100ms;
    // A budget of nothing: the measurement pass gives up before it starts, so
    // the transfer runs with unknown totals. That is the large-tree case,
    // reached here without building a large tree.
    options.measure_budget = 0ms;
    options.on_progress = [&reports, clock](const TransferProgress& progress) {
        reports.push_back(progress);
        clock->advance(100ms);
    };

    detail::TransferRun run(options, reading(clock));
    const std::error_code ec = detail::run_transfer(source, tree.root() / "copy", run);
    check(!ec, "a transfer that could not be measured still completes");
    check(!run.totals().known, "an abandoned measurement leaves the totals unknown");

    bool any_rate = false;
    for (const TransferProgress& progress : reports) {
        check(!progress.estimate.remaining_known, "no time remaining without totals");
        any_rate = any_rate || progress.estimate.throughput_known;
    }
    check(any_rate, "a rate is still offered without totals, because it needs no totals");
    check(fs::exists(tree.root() / "copy/entry-0"), "the unmeasured transfer still copies");
}

// ---------------------------------------------------------------------------
// Cancellation.
// ---------------------------------------------------------------------------

void test_a_cancelled_copy_leaves_nothing_behind() {
    const odysea::test::TemporaryTree tree("transfer_cancel");
    const fs::path source = tree.root() / "source";
    build_tree(source, 200, FileSize{.bytes = 4096});
    const fs::path target = tree.directory("target");

    const auto control = std::make_shared<TransferControl>();
    TransferOptions options;
    options.control = control;
    options.report_interval = 0ms;
    std::uint64_t seen = 0;
    options.on_progress = [&seen, control](const TransferProgress& progress) {
        ++seen;
        if (progress.phase == TransferPhase::Transferring && progress.entries_done > 5) {
            control->request_cancel();
        }
    };

    const OperationOutcome outcome = copy_into(source, target, options);
    check(!outcome.succeeded(), "a cancelled copy does not report success");
    check(outcome.error == std::errc::operation_canceled,
          "a cancelled copy is reported as cancelled and not as some other failure");
    check(!fs::exists(target / "source"), "a cancelled copy installs nothing at the destination");
    check(holds_no_working_entry(target), "a cancelled copy leaves no working entry behind");
    check(count_entries(source) == 201, "a cancelled copy leaves the source alone");
    check(seen > 0, "the fixture reached the point where it cancels");
}

void test_a_copy_cancelled_before_it_starts_does_nothing() {
    const odysea::test::TemporaryTree tree("transfer_cancel_early");
    const fs::path source = tree.root() / "source";
    build_tree(source, 4, FileSize{.bytes = 16});
    const fs::path target = tree.directory("target");

    const auto control = std::make_shared<TransferControl>();
    control->request_cancel();
    TransferOptions options;
    options.control = control;

    const OperationOutcome outcome = copy_into(source, target, options);
    check(outcome.error == std::errc::operation_canceled,
          "a copy cancelled before it begins reports cancellation");
    check(!fs::exists(target / "source"), "a copy cancelled before it begins copies nothing");
    check(holds_no_working_entry(target),
          "a copy cancelled before it begins leaves no working entry");
}

void test_a_move_cancelled_before_it_starts_leaves_the_source() {
    const odysea::test::TemporaryTree tree("transfer_move_cancel");
    const fs::path source = tree.file("source/report.txt", "contents");
    const fs::path target = tree.directory("target");

    const auto control = std::make_shared<TransferControl>();
    control->request_cancel();
    TransferOptions options;
    options.control = control;

    const OperationOutcome outcome = move_into(source, target, options);
    check(outcome.error == std::errc::operation_canceled, "a cancelled move reports cancellation");
    check(fs::exists(source), "a move cancelled before it begins leaves the source where it was");
    check(!fs::exists(target / "report.txt"), "a cancelled move moves nothing");
}

void test_a_cancelled_crossing_move_leaves_both_sides_intact() {
    const odysea::test::TemporaryTree tree("transfer_cross_cancel");
    const fs::path source = tree.root() / "source";
    build_tree(source, 120, FileSize{.bytes = 4096});
    const fs::path target = tree.directory("target");

    const auto control = std::make_shared<TransferControl>();
    TransferOptions options;
    options.control = control;
    options.report_interval = 0ms;
    options.on_progress = [control](const TransferProgress& progress) {
        if (progress.phase == TransferPhase::Transferring && progress.entries_done > 4) {
            control->request_cancel();
        }
    };

    // Forced across a filesystem boundary, which is the only move that copies
    // and therefore the only one a cancellation can interrupt part-way.
    const OperationOutcome outcome =
        detail::move_into_using(source, target, options,
                                [](detail::RenameKind kind, const fs::path& from,
                                   const fs::path& to, std::error_code& error) {
                                    if (kind == detail::RenameKind::Relocate) {
                                        error = std::make_error_code(std::errc::cross_device_link);
                                        return;
                                    }
                                    fs::rename(from, to, error);
                                });

    check(outcome.error == std::errc::operation_canceled,
          "a crossing move cancelled part-way reports cancellation");
    check(count_entries(source) == 121, "a cancelled crossing move leaves the whole source");
    check(!fs::exists(target / "source"), "a cancelled crossing move installs nothing");
    check(holds_no_working_entry(target), "a cancelled crossing move leaves no working entry");
}

// ---------------------------------------------------------------------------
// Pause, and the states reachable from it.
// ---------------------------------------------------------------------------

/// Starts a copy on its own thread and returns once it has parked.
struct PausedCopy {
    std::shared_ptr<TransferControl> control = std::make_shared<TransferControl>();
    std::thread worker;
    OperationOutcome outcome;
    std::atomic<std::uint64_t> entries_seen{0};
    std::atomic<bool> pause_requested{false};
    bool parked = false;

    void start(const fs::path& source, const fs::path& target) {
        TransferOptions options;
        options.control = control;
        options.report_interval = 0ms;
        auto* seen = &entries_seen;
        auto* once = &pause_requested;
        auto held = control;
        // Asked for exactly once. An observer that asks on every report
        // parks the transfer again the instant it is resumed, and the case
        // waiting for it to finish waits forever.
        options.on_progress = [seen, once, held](const TransferProgress& progress) {
            if (progress.phase != TransferPhase::Transferring) {
                return;
            }
            seen->store(progress.entries_done);
            if (progress.entries_done > 3 && !once->exchange(true)) {
                held->request_pause();
            }
        };
        worker = std::thread(
            [this, source, target, options] { outcome = copy_into(source, target, options); });
        parked = control->wait_until_parked(10s);
    }

    void join() {
        if (worker.joinable()) {
            worker.join();
        }
    }
};

void test_a_paused_transfer_stops_and_resumes() {
    const odysea::test::TemporaryTree tree("transfer_pause");
    const fs::path source = tree.root() / "source";
    build_tree(source, 400, FileSize{.bytes = 4096});
    const fs::path target = tree.directory("target");

    PausedCopy run;
    run.start(source, target);
    check(run.parked, "a paused transfer parks");
    check(run.control->parked(), "a parked transfer says so");

    const std::uint64_t at_pause = run.entries_seen.load();
    std::this_thread::sleep_for(50ms);
    check(run.entries_seen.load() == at_pause, "a parked transfer does no further work");

    run.control->resume();
    run.join();
    check(run.outcome.succeeded(), "a resumed transfer completes");
    check(fs::exists(target / "source/entry-0"), "a resumed transfer installs its result");
    check(holds_no_working_entry(target), "a completed transfer leaves no working entry");
}

void test_a_paused_transfer_can_be_cancelled() {
    const odysea::test::TemporaryTree tree("transfer_pause_cancel");
    const fs::path source = tree.root() / "source";
    build_tree(source, 400, FileSize{.bytes = 4096});
    const fs::path target = tree.directory("target");

    PausedCopy run;
    run.start(source, target);
    check(run.parked, "the transfer parked before being cancelled");

    // A cancel has to wake it. A parked transfer holds its descriptors and
    // its working entry, and nobody is coming to resume it.
    run.control->request_cancel();
    run.join();
    check(run.outcome.error == std::errc::operation_canceled,
          "a transfer cancelled while parked reports cancellation");
    check(!fs::exists(target / "source"), "a transfer cancelled while parked installs nothing");
    check(holds_no_working_entry(target),
          "a transfer cancelled while parked leaves no working entry");
    check(count_entries(source) == 401, "a transfer cancelled while parked leaves the source");
}

void test_a_source_that_vanishes_while_paused_fails_the_transfer() {
    const odysea::test::TemporaryTree tree("transfer_pause_vanish");
    const fs::path source = tree.root() / "source";
    build_tree(source, 400, FileSize{.bytes = 4096});
    const fs::path target = tree.directory("target");

    PausedCopy run;
    run.start(source, target);
    check(run.parked, "the transfer parked before the source was removed");

    std::error_code remove_ec;
    fs::remove_all(source, remove_ec);
    check(!remove_ec, "the fixture could remove the source while the transfer was parked");

    run.control->resume();
    run.join();
    check(!run.outcome.succeeded(), "a transfer whose source vanished does not report success");
    check(!fs::exists(target / "source"),
          "a transfer whose source vanished installs nothing at the destination");
    check(holds_no_working_entry(target),
          "a transfer whose source vanished leaves no working entry behind");
}

void test_a_destination_that_stops_accepting_writes_fails_the_transfer() {
    // The filesystem filling up while a transfer is parked reaches the same
    // place by the same route: the next write fails and the partial copy is
    // discarded. Provoked here by removing the right to write, which needs no
    // control of a mount and is deterministic.
    const odysea::test::TemporaryTree tree("transfer_pause_readonly");
    const fs::path source = tree.root() / "source";
    build_tree(source, 60, FileSize{.bytes = 4096});
    const fs::path destination = tree.root() / "destination";

    const auto clock = std::make_shared<ManualClock>();
    const auto control = std::make_shared<TransferControl>();
    TransferOptions options;
    options.control = control;
    options.report_interval = 0ms;
    std::atomic<bool> pause_requested{false};
    options.on_progress = [control, &pause_requested](const TransferProgress& progress) {
        if (progress.phase == TransferPhase::Transferring && progress.entries_done > 3 &&
            !pause_requested.exchange(true)) {
            control->request_pause();
        }
    };

    detail::TransferRun run(options, reading(clock));
    std::error_code outcome;
    std::thread worker([&] { outcome = detail::run_transfer(source, destination, run); });
    const bool parked = control->wait_until_parked(10s);
    check(parked, "the transfer parked before the destination was closed off");

    std::error_code permission_ec;
    fs::permissions(destination, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, permission_ec);
    check(!permission_ec, "the fixture could close the destination off");

    control->resume();
    worker.join();
    check(static_cast<bool>(outcome),
          "a transfer whose destination stopped accepting writes reports a failure");
    check(outcome != std::errc::operation_canceled,
          "a destination that will not take writes is reported as that, not as a cancellation");

    // Put the rights back so the fixture can remove itself.
    fs::permissions(destination, fs::perms::owner_all, fs::perm_options::replace, permission_ec);
}

// ---------------------------------------------------------------------------
// What a transfer refuses.
// ---------------------------------------------------------------------------

void test_reporting_costs_a_bounded_share_of_the_transfer() {
    // The other half of the cost bound. The count above holds the number of
    // reports; this holds what they cost, which is the measurement pass and
    // the reports themselves against the same transfer with neither.
    //
    // Both halves run back to back on the same tree in the same process, so
    // the ratio is between two equal amounts of work rather than between two
    // sizes. That distinction is the whole reason this is asserted at all:
    // measured beside thirty-one competing copies, a ratio between two sizes
    // of the same operation ran from 1.40 to 4.05, while a ratio between two
    // measurements of the same size stayed inside 0.40 to 1.48.
    //
    // Measured on a directory of many small files, which is the shape that
    // pays most for the measurement pass because almost all of its cost is
    // per entry: reporting cost 1.13 times the unwatched transfer at 8,000
    // empty files, 1.13 at 2,000 small ones, and 1.05 at 40 large ones. The
    // ceiling is set well above that, because what it exists to catch is
    // reporting that has become proportional to the work rather than to the
    // time — a report per entry, which costs multiples rather than percents.
    constexpr double reporting_cost_ceiling = 2.5;
    constexpr double reporting_cost_floor = 0.1;

    const odysea::test::TemporaryTree tree("transfer_cost");
    const fs::path source = tree.root() / "source";
    build_tree(source, 4000, FileSize{.bytes = 64});

    const auto unwatched = [&tree, &source] {
        TransferOptions options;
        detail::TransferRun run(options, &detail::transfer_steady_now);
        const double before = processor_milliseconds();
        const std::error_code ec = detail::run_transfer(source, tree.root() / "plain", run);
        const double spent = processor_milliseconds() - before;
        check(!ec, "the unwatched transfer completes");
        check(run.report_count() == 0, "the unwatched transfer reported nothing");
        return spent;
    }();

    std::uint64_t reports = 0;
    const auto watched = [&tree, &source, &reports] {
        TransferOptions options;
        options.on_progress = [&reports](const TransferProgress&) { ++reports; };
        detail::TransferRun run(options, &detail::transfer_steady_now);
        const double before = processor_milliseconds();
        const std::error_code ec = detail::run_transfer(source, tree.root() / "reported", run);
        const double spent = processor_milliseconds() - before;
        check(!ec, "the watched transfer completes");
        return spent;
    }();

    check(unwatched > 0.0, "the unwatched transfer was measured");
    check(watched > 0.0, "the watched transfer was measured");
    check(reports > 0, "the watched transfer reported");

    const double ratio = watched / unwatched;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::fprintf(stderr,
                 "transfer: 4000 entries cost %.1f ms unwatched and %.1f ms watched, %.2f times "
                 "as much, floor %.2f ceiling %.2f, over %llu reports\n",
                 unwatched, watched, ratio, reporting_cost_floor, reporting_cost_ceiling,
                 static_cast<unsigned long long>(reports));

    // Bounded below as well: a numerator that stopped being measured, or a
    // transfer that stopped doing the work, reads as free and satisfies any
    // ceiling on its own.
    check(ratio > reporting_cost_floor, "a watched transfer is not free");
    check(ratio < reporting_cost_ceiling, "reporting costs a bounded share of the transfer");
}

void test_a_cancelled_copy_records_nothing_in_the_journal() {
    // The other half of a cancellation, and the one that matters most. A
    // record of an operation that only partly happened would reverse only
    // part of it, and the history offers no way to say so afterwards. The
    // rule is upstream of that: nothing was installed, so nothing is
    // recorded.
    const odysea::test::TemporaryTree tree("transfer_journal_cancel");
    const fs::path source = tree.root() / "source";
    build_tree(source, 200, FileSize{.bytes = 4096});
    const fs::path target = tree.directory("target");

    OperationJournal journal;
    const fs::path settled = tree.file("settled/kept.txt", "kept");
    const OperationOutcome first = journal.copy_into(settled, target, OperationOptions{});
    check(first.succeeded(), "the fixture recorded one completed copy");
    check(journal.size() == 1, "the completed copy is in the history");

    const auto control = std::make_shared<TransferControl>();
    TransferOptions options;
    options.control = control;
    options.report_interval = 0ms;
    options.on_progress = [control](const TransferProgress& progress) {
        if (progress.phase == TransferPhase::Transferring && progress.entries_done > 5) {
            control->request_cancel();
        }
    };

    const OperationOutcome cancelled = journal.copy_into(source, target, options);
    check(cancelled.error == std::errc::operation_canceled,
          "the cancelled copy is reported as cancelled");
    check(journal.size() == 1, "a cancelled copy adds nothing to the history");

    const UndoOutcome reversal = journal.undo();
    check(reversal.succeeded(), "the history still reverses the operation that did complete");
    check(!fs::exists(target / "kept.txt"), "the completed copy was the one reversed");
}

void test_an_entry_that_cannot_be_reproduced_is_refused() {
    const odysea::test::TemporaryTree tree("transfer_unsupported");
    const fs::path source = tree.root() / "source";
    build_tree(source, 2, FileSize{.bytes = 16});
    check(::mkfifo((source / "pipe").c_str(), 0600) == 0, "the fixture could create a named pipe");
    const fs::path target = tree.directory("target");

    TransferOptions options;
    const OperationOutcome outcome = copy_into(source, target, options);
    check(outcome.error == std::errc::not_supported,
          "a tree holding an entry that cannot be reproduced is refused as unsupported");
    check(!fs::exists(target / "source"),
          "a refused transfer installs no partial tree at the destination");
    check(holds_no_working_entry(target), "a refused transfer leaves no working entry");
}

} // namespace

int main() {
    test_an_entry_is_charged_beyond_its_bytes();
    test_a_fraction_is_withheld_rather_than_guessed();
    test_reporting_is_bounded_by_time_and_not_by_the_size_of_the_job();
    test_a_report_is_made_once_the_interval_has_passed();
    test_an_unwatched_transfer_neither_measures_nor_reports();
    test_a_watched_transfer_reports_what_it_has_done();
    test_an_estimate_is_withheld_until_the_window_holds_enough();
    test_no_time_remaining_is_offered_without_totals();
    test_a_cancelled_copy_leaves_nothing_behind();
    test_a_copy_cancelled_before_it_starts_does_nothing();
    test_a_move_cancelled_before_it_starts_leaves_the_source();
    test_a_cancelled_crossing_move_leaves_both_sides_intact();
    test_a_paused_transfer_stops_and_resumes();
    test_a_paused_transfer_can_be_cancelled();
    test_a_source_that_vanishes_while_paused_fails_the_transfer();
    test_a_destination_that_stops_accepting_writes_fails_the_transfer();
    test_reporting_costs_a_bounded_share_of_the_transfer();
    test_a_cancelled_copy_records_nothing_in_the_journal();
    test_an_entry_that_cannot_be_reproduced_is_refused();
    return odysea::test::report("transfer");
}

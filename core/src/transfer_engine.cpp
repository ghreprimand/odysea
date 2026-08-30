#include "transfer_engine.hpp"

#include "odysea/core/descriptor.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <span>
#include <utility>
#include <vector>

namespace odysea::core {
namespace fs = std::filesystem;

namespace detail {

namespace {

/// How much file content one read-and-write handles.
///
/// Large enough that the checkpoint between chunks costs nothing measurable
/// against the copy, and small enough that a pause or a cancel takes effect
/// promptly on a slow filesystem rather than after the rest of a large file.
constexpr std::size_t chunk_bytes = std::size_t{256} * 1024;

/// How far back the throughput window reaches.
///
/// Long enough to average over a filesystem's bursts and a directory's
/// awkward corners, short enough that the rate still describes now rather
/// than a minute ago.
constexpr std::chrono::milliseconds throughput_window{2000};

/// The least span the window must cover before a rate is offered at all.
///
/// Below this the divisor is small enough that ordinary jitter dominates the
/// answer, and a time remaining computed from it swings by more than it is
/// worth. Until the span is reached the estimate reports that it does not
/// know, which is the honest answer and not the same as zero.
constexpr std::chrono::milliseconds throughput_minimum_span{500};

/// A source entry and where it is being reproduced.
struct TransferPair {
    fs::path from;
    fs::path to;
};

std::error_code error_from_errno() {
    return {errno, std::generic_category()};
}

std::error_code cancelled_error() {
    return std::make_error_code(std::errc::operation_canceled);
}

/// Copy one regular file's contents, checkpointing between chunks.
std::error_code copy_file_contents(const fs::path& source, const fs::path& destination,
                                   TransferRun& run, std::vector<char>& buffer) {
    // POSIX declares open as a variadic function and offers no fixed-arity
    // form of it, so the call cannot be written without one.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const Descriptor input(::open(source.c_str(), O_RDONLY | O_CLOEXEC));
    if (!input.valid()) {
        return error_from_errno();
    }

    // Created exclusively: the destination is a working entry this operation
    // reserved, so anything already at that name is somebody else's and must
    // not be written through.
    const Descriptor output(::open( // NOLINT(cppcoreguidelines-pro-type-vararg)
        destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    if (!output.valid()) {
        return error_from_errno();
    }

    while (true) {
        if (const std::error_code stopped = run.checkpoint()) {
            return stopped;
        }

        const ssize_t read_bytes = ::read(input.get(), buffer.data(), buffer.size());
        if (read_bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return error_from_errno();
        }
        if (read_bytes == 0) {
            break;
        }

        // The chunk is handed on as a span so what remains to be written is
        // expressed as a narrowing of the buffer rather than as a pointer
        // walked forward, which is one fewer place an off-by-one can live.
        std::span<char> remaining(buffer.data(), static_cast<std::size_t>(read_bytes));
        while (!remaining.empty()) {
            const ssize_t wrote = ::write(output.get(), remaining.data(), remaining.size());
            if (wrote < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return error_from_errno();
            }
            remaining = remaining.subspan(static_cast<std::size_t>(wrote));
        }
        run.note_bytes(static_cast<std::uint64_t>(read_bytes));
        run.report_if_due();
    }

    // Permissions last, so the file is written through a descriptor opened
    // before they were narrowed. A source with no owner-write bit would
    // otherwise be reproduced as a file this process could not finish
    // writing.
    std::error_code permission_ec;
    const fs::file_status status = fs::symlink_status(source, permission_ec);
    if (permission_ec) {
        return permission_ec;
    }
    fs::permissions(destination, status.permissions(), fs::perm_options::replace, permission_ec);
    return permission_ec;
}

/// One entry of the walk: reproduce it, and queue its children when it has
/// any. Separated from the walk so that neither the queue's bookkeeping nor
/// the per-kind reproduction has to be read through the other.
std::error_code copy_one_entry(const TransferPair& current, TransferRun& run,
                               std::vector<char>& buffer, std::vector<TransferPair>& pending) {
    std::error_code status_ec;
    const fs::file_status status = fs::symlink_status(current.from, status_ec);
    if (status_ec) {
        return status_ec;
    }

    run.note_entry(current.from);

    if (fs::is_symlink(status)) {
        std::error_code link_ec;
        fs::copy_symlink(current.from, current.to, link_ec);
        if (!link_ec) {
            run.report_if_due();
        }
        return link_ec;
    }

    if (fs::is_regular_file(status)) {
        const std::error_code file_ec = copy_file_contents(current.from, current.to, run, buffer);
        if (!file_ec) {
            run.report_if_due();
        }
        return file_ec;
    }

    if (!fs::is_directory(status)) {
        // A socket, a device node, or a named pipe: nothing here can
        // reproduce one. Refused rather than skipped, because skipping it
        // would report success for a tree that arrived incomplete, and
        // whoever asked for the copy would have no way to know a part of it
        // had been left behind. The transfer is abandoned and its working
        // entry discarded, so the destination is untouched.
        return std::make_error_code(std::errc::not_supported);
    }

    // Created from the source, so the directory carries the source's
    // permissions exactly as the standard library's recursive copy leaves
    // them.
    std::error_code create_ec;
    fs::create_directory(current.to, current.from, create_ec);
    if (create_ec) {
        return create_ec;
    }

    std::error_code iterate_ec;
    fs::directory_iterator element(current.from, iterate_ec);
    if (iterate_ec) {
        return iterate_ec;
    }
    const fs::directory_iterator end;
    while (element != end) {
        const fs::path child = element->path();
        pending.push_back(TransferPair{.from = child, .to = current.to / child.filename()});
        std::error_code step_ec;
        element.increment(step_ec);
        if (step_ec) {
            return step_ec;
        }
    }
    run.report_if_due();
    return {};
}

} // namespace

std::chrono::steady_clock::time_point transfer_steady_now() {
    return std::chrono::steady_clock::now();
}

TransferRun::TransferRun(const TransferOptions& options, TransferClock clock)
    : options_(options), clock_(std::move(clock)), started_(clock_()), last_report_(started_) {}

std::error_code TransferRun::checkpoint() {
    if (!options_.control) {
        return {};
    }
    if (!options_.control->checkpoint()) {
        return cancelled_error();
    }
    return {};
}

void TransferRun::set_phase(TransferPhase phase) noexcept {
    phase_ = phase;
}

void TransferRun::note_entry(const fs::path& entry) {
    ++entries_done_;
    current_entry_ = entry;
}

bool TransferRun::measurement_budget_spent() {
    return (clock_() - started_) > options_.measure_budget;
}

void TransferRun::report_if_due() {
    if (!observed()) {
        return;
    }
    const std::chrono::steady_clock::time_point now = clock_();
    if (reported_ && (now - last_report_) < options_.report_interval) {
        return;
    }
    report_now();
}

TransferEstimate TransferRun::estimate(std::chrono::steady_clock::time_point now) {
    const std::uint64_t work = transfer_work_units(bytes_done_, entries_done_);

    // Stale samples go before the new one arrives, so a transfer that was
    // paused, or stalled for any other reason, is left with nothing older
    // than the window and starts measuring its rate again from the resumption
    // rather than averaging across the gap.
    while (!window_.empty() && (now - window_.front().at) > throughput_window) {
        window_.pop_front();
    }
    window_.push_back(Sample{.at = now, .work = work});

    TransferEstimate reading;
    const Sample& oldest = window_.front();
    const auto span = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest.at);
    if (span < throughput_minimum_span || work < oldest.work) {
        return reading;
    }

    const double seconds = std::chrono::duration<double>(span).count();
    if (seconds <= 0.0) {
        return reading;
    }
    reading.throughput_known = true;
    reading.work_units_per_second = static_cast<double>(work - oldest.work) / seconds;

    if (!totals_.known || reading.work_units_per_second <= 0.0) {
        return reading;
    }
    const std::uint64_t total = transfer_work_units(totals_.bytes, totals_.entries);
    if (total <= work) {
        reading.remaining_known = true;
        reading.remaining = std::chrono::seconds{0};
        return reading;
    }
    const double left = static_cast<double>(total - work) / reading.work_units_per_second;
    reading.remaining_known = true;
    reading.remaining = std::chrono::seconds{std::llround(left)};
    return reading;
}

void TransferRun::report_now() {
    if (!observed()) {
        return;
    }
    const std::chrono::steady_clock::time_point now = clock_();

    TransferProgress progress;
    progress.phase = phase_;
    progress.bytes_done = bytes_done_;
    progress.entries_done = entries_done_;
    progress.totals = totals_;
    progress.current_entry = current_entry_;
    progress.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_);
    progress.estimate = estimate(now);

    last_report_ = now;
    reported_ = true;
    ++report_count_;
    options_.on_progress(progress);
}

std::error_code measure_tree(const fs::path& source, TransferRun& run) {
    TransferTotals totals;
    std::vector<fs::path> pending{source};

    while (!pending.empty()) {
        if (const std::error_code stopped = run.checkpoint()) {
            return stopped;
        }
        if (run.measurement_budget_spent()) {
            // Abandoned rather than failed. The transfer runs with unknown
            // totals: no fraction and no time remaining, and everything else
            // as usual.
            run.set_totals(TransferTotals{});
            return {};
        }

        const fs::path current = std::move(pending.back());
        pending.pop_back();

        std::error_code status_ec;
        const fs::file_status status = fs::symlink_status(current, status_ec);
        if (status_ec) {
            run.set_totals(TransferTotals{});
            return {};
        }

        ++totals.entries;
        if (fs::is_regular_file(status)) {
            std::error_code size_ec;
            const std::uintmax_t size = fs::file_size(current, size_ec);
            if (!size_ec) {
                totals.bytes += static_cast<std::uint64_t>(size);
            }
            continue;
        }
        if (!fs::is_directory(status)) {
            continue;
        }

        std::error_code iterate_ec;
        fs::directory_iterator element(current, iterate_ec);
        if (iterate_ec) {
            run.set_totals(TransferTotals{});
            return {};
        }
        const fs::directory_iterator end;
        while (element != end) {
            pending.push_back(element->path());
            std::error_code step_ec;
            element.increment(step_ec);
            if (step_ec) {
                run.set_totals(TransferTotals{});
                return {};
            }
        }
        run.report_if_due();
    }

    totals.known = true;
    run.set_totals(totals);
    return {};
}

std::error_code copy_tree(const fs::path& source, const fs::path& destination, TransferRun& run) {
    std::vector<char> buffer(chunk_bytes);
    std::vector<TransferPair> pending{TransferPair{.from = source, .to = destination}};

    while (!pending.empty()) {
        if (const std::error_code stopped = run.checkpoint()) {
            return stopped;
        }

        const TransferPair current = std::move(pending.back());
        pending.pop_back();

        if (const std::error_code failed = copy_one_entry(current, run, buffer, pending)) {
            return failed;
        }
    }
    return {};
}

std::error_code run_transfer(const fs::path& source, const fs::path& destination,
                             TransferRun& run) {
    if (run.observed()) {
        run.set_phase(TransferPhase::Measuring);
        run.report_now();
        if (const std::error_code stopped = measure_tree(source, run)) {
            return stopped;
        }
    }

    run.set_phase(TransferPhase::Transferring);
    if (run.observed()) {
        run.report_now();
    }
    return copy_tree(source, destination, run);
}

} // namespace detail
} // namespace odysea::core

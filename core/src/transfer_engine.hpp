// Internal seam for the transfer engine. Not part of the public API and not
// installed: only transfer_engine.cpp, file_operations.cpp, and the headless
// tests include it.
//
// The engine exists because progress cannot be observed from outside a single
// call into the standard library. Reproducing a tree here — entry by entry and
// chunk by chunk — is what makes it possible to say how far along a transfer
// is, to stop it, and to hold it, without handing the operation to another
// program and reading its output.
#pragma once

#include "odysea/core/transfer.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <system_error>

namespace odysea::core::detail {

/// The clock a transfer reads.
///
/// Injectable so the reporting cadence can be tested without spending the
/// intervals it bounds. A test that had to wait for a hundred milliseconds to
/// observe a hundred-millisecond rule would be deciding by machine load, and
/// this project has already retired one bound for exactly that.
using TransferClock = std::function<std::chrono::steady_clock::time_point()>;

/// The production clock.
[[nodiscard]] std::chrono::steady_clock::time_point transfer_steady_now();

/// Accumulates what a transfer has done and decides when to say so.
///
/// One instance per transfer, used from the thread running it and from
/// nowhere else. The control it shares with other threads does its own
/// locking; nothing else here is touched concurrently.
class TransferRun {
  public:
    TransferRun(const TransferOptions& options, TransferClock clock);

    TransferRun(const TransferRun&) = delete;
    TransferRun& operator=(const TransferRun&) = delete;
    TransferRun(TransferRun&&) = delete;
    TransferRun& operator=(TransferRun&&) = delete;

    ~TransferRun() = default;

    /// Whether anything is watching. A transfer nobody watches skips the
    /// measurement pass and every report, so it costs what it always did.
    [[nodiscard]] bool observed() const noexcept { return static_cast<bool>(options_.on_progress); }

    /// Reach a checkpoint: park while paused, and report whether the
    /// transfer should carry on. Returns std::errc::operation_canceled when
    /// it should not, so a caller can return it as an ordinary failure.
    [[nodiscard]] std::error_code checkpoint();

    void set_phase(TransferPhase phase) noexcept;
    void set_totals(TransferTotals totals) noexcept { totals_ = totals; }
    [[nodiscard]] const TransferTotals& totals() const noexcept { return totals_; }

    void note_entry(const std::filesystem::path& entry);
    void note_bytes(std::uint64_t bytes) noexcept { bytes_done_ += bytes; }

    /// Report if the interval since the last report has passed.
    void report_if_due();
    /// Report whatever the interval says. Used for the first report of a
    /// phase and the last report of the transfer, so a consumer always sees a
    /// beginning and an end.
    void report_now();

    /// Whether the measurement pass has used up its budget.
    [[nodiscard]] bool measurement_budget_spent();

    [[nodiscard]] std::uint64_t bytes_done() const noexcept { return bytes_done_; }
    [[nodiscard]] std::uint64_t entries_done() const noexcept { return entries_done_; }
    /// How many reports have been made. Read by the cost cases, which bound
    /// reporting by what a transfer does rather than by how long it took.
    [[nodiscard]] std::uint64_t report_count() const noexcept { return report_count_; }

  private:
    struct Sample {
        std::chrono::steady_clock::time_point at;
        std::uint64_t work = 0;
    };

    [[nodiscard]] TransferEstimate estimate(std::chrono::steady_clock::time_point now);

    const TransferOptions& options_;
    TransferClock clock_;
    std::chrono::steady_clock::time_point started_;
    std::chrono::steady_clock::time_point last_report_;
    bool reported_ = false;

    TransferPhase phase_ = TransferPhase::Measuring;
    TransferTotals totals_{};
    std::uint64_t bytes_done_ = 0;
    std::uint64_t entries_done_ = 0;
    std::uint64_t report_count_ = 0;
    std::filesystem::path current_entry_;

    std::deque<Sample> window_;
};

/// Walk `source` and record its size in `run`.
///
/// Reports std::errc::operation_canceled when the transfer was cancelled
/// during the walk. Any other failure, and running out of budget, leave the
/// totals unknown and are not errors: a transfer that could not be measured
/// still runs.
[[nodiscard]] std::error_code measure_tree(const std::filesystem::path& source, TransferRun& run);

/// Reproduce `source` at `destination`, which must not exist.
///
/// Directories are reproduced recursively, symlinks are reproduced as
/// symlinks rather than followed, and file contents are copied in chunks with
/// a checkpoint between them. An entry of any other kind — a socket, a device
/// node, a named pipe — is refused with std::errc::not_supported rather than
/// passed over, so a tree is never reported as copied while arriving
/// incomplete.
[[nodiscard]] std::error_code copy_tree(const std::filesystem::path& source,
                                        const std::filesystem::path& destination, TransferRun& run);

/// Measure and then copy, which is what a watched transfer does.
[[nodiscard]] std::error_code run_transfer(const std::filesystem::path& source,
                                           const std::filesystem::path& destination,
                                           TransferRun& run);

} // namespace odysea::core::detail

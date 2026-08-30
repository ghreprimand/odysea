// OdySea core: progress, throughput, estimates, and cooperative pause for the
// mutation primitives.
//
// Toolkit-agnostic. A copy or a move reports what it has done as it does it,
// and can be paused, resumed, or cancelled from another thread. The work stays
// here: the byte loop belongs to this library, so the reporting belongs to it
// too, and nothing in this file hands the operation to another program.
//
// WHAT AN INTERRUPTED TRANSFER LEAVES BEHIND. A reported cancellation happens
// before installation: the destination is unchanged, the source is untouched,
// and the working entry is discarded. A failure before installation follows
// the same recovery path, so the destination is never a partial copy.
//
// A crossing move has one later failure point. Once its complete copy is
// installed, source removal is deliberately not interruptible; if that removal
// fails, the operation reports failure while the complete destination and any
// source remainder stay on disk. The completed-operations-only journal records
// neither a cancellation nor this failed move.
//
// A replacement failure has a separate absence case. If installing the new
// entry fails and restoring the former occupant also fails, the destination
// name is absent and its former occupant remains beside it under a Replaced
// working name, recognizable with classify_working_entry(). If recovery also
// cannot return a relocated source, the source name is absent and that entry
// remains under a Prepared working name. These entries may be the only copies;
// recovery is through their working names, never by deleting them. A pause is
// not an ending — it holds the working entry and the descriptors open and
// waits.
#pragma once

#include "odysea/core/file_operations.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>

namespace odysea::core {

/// What a transfer is doing when it reports.
enum class TransferPhase : std::uint8_t {
    /// Walking the source to learn how much there is. Bounded: a tree too
    /// large to measure quickly is transferred with unknown totals rather
    /// than delaying the first byte.
    Measuring,
    /// Reproducing the source at the destination.
    Transferring,
};

/// How much there is to do, and whether that is known at all.
///
/// `known` is false when no measurement was requested, when the measurement
/// was abandoned against its budget, or when it failed. Every consumer has to
/// handle that: a transfer with unknown totals still reports what it has
/// done, still throttles, and still cancels; it cannot offer a fraction or a
/// time remaining, and says so rather than inventing one.
struct TransferTotals {
    std::uint64_t bytes = 0;
    std::uint64_t entries = 0;
    bool known = false;
};

/// What one entry costs beyond its bytes, expressed in bytes.
///
/// Progress counted in bytes alone reads worst on exactly the common case:
/// a directory of many small files spends nearly all of its time creating
/// entries and almost none moving contents, so a byte-counting bar sits at
/// zero and then jumps. Each entry is charged this many byte-equivalents so
/// the two costs are summed in one unit.
///
/// Measured rather than guessed. Copying 20,000 empty files and one large
/// file on the same filesystem, in three runs, put one entry at 16,253,
/// 18,159, and 17,942 byte-equivalents; 16 KiB is the round value at the
/// conservative end of that range.
///
/// It is a fixed constant and not a per-machine calibration. A calibrated
/// figure would make the same transfer report differently on two machines and
/// make a test of the arithmetic unrepeatable, and the estimate is not
/// precise enough for the difference to be worth that.
inline constexpr std::uint64_t entry_work_bytes = 16384;

/// Bytes and entries summed into the single unit progress is measured in.
[[nodiscard]] constexpr std::uint64_t transfer_work_units(std::uint64_t bytes,
                                                          std::uint64_t entries) noexcept {
    return bytes + (entries * entry_work_bytes);
}

/// A rate and a time remaining, both of which are estimates.
///
/// Named so at every use. Each carries its own "known" flag rather than a
/// sentinel value, because a rate of zero and a rate nobody has measured yet
/// are different answers and a consumer that cannot tell them apart will
/// present the second as the first.
///
/// THE WINDOW, and why it is not instantaneous. A rate taken between two
/// consecutive reports is dominated by whatever the filesystem happened to be
/// doing in those milliseconds, and a time remaining derived from it swings
/// by minutes between one report and the next — a number that changes faster
/// than it can be read is worse than no number. The rate here is measured
/// over a sliding window of the recent past, and is withheld until the window
/// holds enough of it to mean something. The window's length and the minimum
/// span are stated where they are applied.
struct TransferEstimate {
    /// Whether `work_units_per_second` was measured.
    bool throughput_known = false;
    /// Recent rate in the unit above: bytes plus charged entries, per second.
    double work_units_per_second = 0.0;
    /// Whether `remaining` was derived. Requires both a measured rate and
    /// known totals; either missing leaves this false.
    bool remaining_known = false;
    /// An estimate of the time left, rounded to whole seconds because the
    /// precision behind it does not justify anything finer.
    std::chrono::seconds remaining{0};
};

/// One report from a running transfer.
struct TransferProgress {
    TransferPhase phase = TransferPhase::Measuring;
    /// Bytes of file content written so far.
    std::uint64_t bytes_done = 0;
    /// Entries reproduced so far, of every kind: files, directories, links.
    std::uint64_t entries_done = 0;
    TransferTotals totals;
    /// The entry being worked on when the report was made. Empty while
    /// measuring.
    std::filesystem::path current_entry;
    /// Time since the transfer began, including any time spent paused.
    std::chrono::milliseconds elapsed{0};
    TransferEstimate estimate;

    /// Whether a completed fraction can be offered at all.
    [[nodiscard]] bool completed_fraction_known() const noexcept {
        return totals.known && transfer_work_units(totals.bytes, totals.entries) > 0;
    }

    /// How much of the work is done, between 0 and 1.
    ///
    /// Clamped at 1: a source that grew after it was measured would otherwise
    /// report more than all of itself done.
    [[nodiscard]] double completed_fraction() const noexcept;
};

/// The cooperative pause, resume, and cancel shared with a running transfer.
///
/// Held by shared pointer because the two sides outlive each other in either
/// order: the thread driving the transfer and the surface holding the button.
/// Every method is safe to call from any thread at any time, including before
/// the transfer starts and after it has ended.
///
/// Cancellation is one-way. A cancelled transfer cannot be resumed, and
/// asking it to pause does nothing: a request to stop is never weakened by a
/// request to wait.
class TransferControl {
  public:
    TransferControl() = default;

    TransferControl(const TransferControl&) = delete;
    TransferControl& operator=(const TransferControl&) = delete;
    TransferControl(TransferControl&&) = delete;
    TransferControl& operator=(TransferControl&&) = delete;

    ~TransferControl() = default;

    /// Ask the transfer to stop at its next checkpoint. Wakes a paused
    /// transfer, so a cancel is never held up by a pause nobody released.
    void request_cancel() noexcept;

    /// Ask the transfer to wait at its next checkpoint. Ignored once
    /// cancelled.
    void request_pause() noexcept;

    /// Release a pause. Harmless when nothing is paused.
    void resume() noexcept;

    [[nodiscard]] bool cancel_requested() const noexcept;
    [[nodiscard]] bool pause_requested() const noexcept;

    /// Whether the transfer is actually parked, as opposed to having been
    /// asked to park. The two differ for as long as it takes to reach the
    /// next checkpoint, and a surface that reports the request as the state
    /// tells the user the transfer has stopped touching the disk before it
    /// has.
    [[nodiscard]] bool parked() const noexcept;

    /// Wait until the transfer is parked, or until the timeout expires.
    /// Returns whether it parked. Exists so a caller — a test, or a surface
    /// that wants to show a settled state — can wait for the fact rather
    /// than poll for it.
    [[nodiscard]] bool wait_until_parked(std::chrono::milliseconds timeout);

    /// Called by the transfer at a checkpoint. Parks while paused and
    /// reports whether the transfer should continue. Not for callers.
    [[nodiscard]] bool checkpoint();

  private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    bool cancel_requested_ = false;
    bool pause_requested_ = false;
    bool parked_ = false;
};

/// Receives a report. Invoked on the thread running the transfer, never
/// concurrently with itself, and never while a lock inside the control is
/// held. A consumer with thread affinity marshals the report onto its own
/// thread, as the directory scanner's consumers already do.
using TransferObserver = std::function<void(const TransferProgress&)>;

/// Everything a transfer needs beyond its source and destination.
struct TransferOptions {
    /// How the destination name is resolved. The same options the plain
    /// operations take.
    OperationOptions operation;

    /// Where reports go. An absent observer skips both the measurement pass
    /// and the reporting, so a transfer nobody is watching costs what it
    /// always did.
    TransferObserver on_progress;

    /// Pause, resume, and cancel. An absent control means the transfer runs
    /// to completion.
    std::shared_ptr<TransferControl> control;

    /// The shortest interval between two reports. Reporting is bounded in
    /// time rather than in entries or bytes, so the cost of reporting is set
    /// by how long a transfer runs and not by how much it moves: a directory
    /// of a million entries reports no more often than one of ten. The first
    /// report of a phase and the last report of the transfer are made
    /// regardless, so a consumer always sees a beginning and an end.
    std::chrono::milliseconds report_interval{100};

    /// How long the measurement pass may run before the transfer gives up on
    /// knowing its totals and starts moving data. A tree large enough to
    /// exceed this is a tree where waiting to draw an accurate bar costs more
    /// than the bar is worth.
    std::chrono::milliseconds measure_budget{2000};
};

/// Copy `source` into `destination_directory` with reporting and control.
///
/// Identical to the plain copy in every guarantee it makes about what is left
/// on disk. A cancelled transfer reports std::errc::operation_canceled; a
/// cancelled or failed copy installs no new result.
[[nodiscard]] OperationOutcome copy_into(const std::filesystem::path& source,
                                         const std::filesystem::path& destination_directory,
                                         const TransferOptions& options);

/// Move `source` into `destination_directory` with reporting and control.
///
/// A move within one filesystem is a rename: it moves no bytes, cannot be
/// paused usefully, and reports once. A move across filesystems copies and
/// then removes, and reports throughout the copy. Cancelling before the
/// install leaves the source where it was. If source removal fails after a
/// crossing install, the destination remains complete and the source or a
/// remainder of its tree may also remain.
[[nodiscard]] OperationOutcome move_into(const std::filesystem::path& source,
                                         const std::filesystem::path& destination_directory,
                                         const TransferOptions& options);

} // namespace odysea::core

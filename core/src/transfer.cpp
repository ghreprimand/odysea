// The transfer control and the arithmetic of a progress report.
//
// Kept apart from the engine deliberately. The control is a mutex and a
// condition variable, the engine's inner loop reads and writes a file, and an
// analyser that can see both in one translation unit reports the read as a
// blocking call inside a critical section — which it is not, because the lock
// is released before the checkpoint returns. Separating them removes the
// false report without weakening the check that produced it, and the two
// halves have nothing else to say to each other.
#include "odysea/core/transfer.hpp"

#include <algorithm>
#include <mutex>

namespace odysea::core {

double TransferProgress::completed_fraction() const noexcept {
    if (!completed_fraction_known()) {
        return 0.0;
    }
    const auto total = static_cast<double>(transfer_work_units(totals.bytes, totals.entries));
    const auto done = static_cast<double>(transfer_work_units(bytes_done, entries_done));
    return std::min(1.0, done / total);
}

void TransferControl::request_cancel() noexcept {
    {
        const std::scoped_lock held(mutex_);
        cancel_requested_ = true;
        // A cancel releases a pause rather than queueing behind it. A
        // transfer parked with nobody left to resume it would otherwise hold
        // its descriptors and its working entry forever.
        pause_requested_ = false;
    }
    changed_.notify_all();
}

void TransferControl::request_pause() noexcept {
    {
        const std::scoped_lock held(mutex_);
        if (cancel_requested_) {
            return;
        }
        pause_requested_ = true;
    }
    changed_.notify_all();
}

void TransferControl::resume() noexcept {
    {
        const std::scoped_lock held(mutex_);
        pause_requested_ = false;
    }
    changed_.notify_all();
}

bool TransferControl::cancel_requested() const noexcept {
    const std::scoped_lock held(mutex_);
    return cancel_requested_;
}

bool TransferControl::pause_requested() const noexcept {
    const std::scoped_lock held(mutex_);
    return pause_requested_;
}

bool TransferControl::parked() const noexcept {
    const std::scoped_lock held(mutex_);
    return parked_;
}

bool TransferControl::wait_until_parked(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> held(mutex_);
    return changed_.wait_for(held, timeout, [this] { return parked_; });
}

bool TransferControl::checkpoint() {
    std::unique_lock<std::mutex> held(mutex_);
    if (cancel_requested_) {
        return false;
    }
    if (!pause_requested_) {
        return true;
    }

    parked_ = true;
    // Announced under the same lock the waiter reads, so a caller that waited
    // for the parked state cannot miss it.
    changed_.notify_all();
    changed_.wait(held, [this] { return !pause_requested_ || cancel_requested_; });
    parked_ = false;
    changed_.notify_all();
    return !cancel_requested_;
}

} // namespace odysea::core

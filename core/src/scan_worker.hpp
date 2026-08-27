// OdySea core: the single-worker, newest-request-wins scanning contract.
//
// Internal seam. Not installed, not part of the public API, and free of Qt.
//
// Directory listing and storage-usage accounting are both long-running walks
// that a user can abandon at any moment, and both need exactly the same
// guarantees:
//
//   * one worker thread, so a walk never runs on the caller's thread;
//   * monotonic tokens, where starting a request cancels everything issued
//     before it, which is what makes rapid navigation feel instant;
//   * exactly one completion callback per request, including for a request
//     replaced before it ever started;
//   * no callback delivered after the owner begins tearing down, so a handler
//     can safely capture the owner's address;
//   * a cancellation flag the running walk polls, so cancellation is prompt
//     at any depth rather than only between requests.
//
// Those guarantees live here once. A second implementation of the same
// contract would be a second set of subtly different edge cases, and the
// edges — a superseded request that never started, a teardown mid-walk — are
// exactly where the differences would hide.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace odysea::core::detail {

/// Runs `Request` values one at a time on a private thread, newest wins.
///
/// `Request` must be default-constructible and movable. The worker thread
/// starts with the object and is joined by its destructor, so an owner must
/// declare a `ScanWorker` as its last member: reverse member destruction then
/// joins the worker before any state the callbacks reach is destroyed.
template <typename Request>
class ScanWorker {
  public:
    /// Runs one request to completion on the worker thread. Responsible for
    /// delivering that request's completion callback.
    ///
    /// Wrapped in a named type rather than passed as a bare callable: the two
    /// constructor arguments have identical call signatures, so transposing
    /// them would compile and would then execute the requests that should
    /// have been abandoned and abandon the one that should have run. Distinct
    /// types make that a compile error instead of a runtime mystery.
    struct Execute {
        std::function<void(Request request, std::uint64_t token)> run;
    };

    /// Answers a request that was replaced before it ever started. Takes the
    /// request by reference: the worker still owns it and destroys it as soon
    /// as the callback returns.
    struct Abandon {
        std::function<void(const Request& request, std::uint64_t token)> answer;
    };

    ScanWorker(Execute execute, Abandon abandon)
        : execute_(std::move(execute)), abandon_(std::move(abandon)), worker_([this] { run(); }) {}

    ScanWorker(const ScanWorker&) = delete;
    ScanWorker& operator=(const ScanWorker&) = delete;
    ScanWorker(ScanWorker&&) = delete;
    ScanWorker& operator=(ScanWorker&&) = delete;

    /// Cancels work in flight and joins the worker. Requests that have not
    /// completed receive no further callbacks.
    ~ScanWorker() {
        {
            const std::scoped_lock guard(mutex_);
            shutdown_ = true;
            cancelled_through_.store(next_token_, std::memory_order_relaxed);
            deliver_callbacks_.store(false, std::memory_order_relaxed);
            pending_.reset();
            superseded_.clear();
        }
        work_available_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    /// Queue a request and return immediately with its token. Zero once the
    /// worker is shutting down, which is the one case where no callback
    /// follows.
    std::uint64_t start(Request request) {
        std::uint64_t token = 0;
        {
            const std::scoped_lock guard(mutex_);
            if (shutdown_) {
                return 0;
            }

            token = next_token_++;
            // Everything issued before this request loses.
            cancelled_through_.store(token - 1, std::memory_order_relaxed);

            if (pending_.has_value()) {
                superseded_.emplace_back(std::move(*pending_), pending_token_);
            }
            pending_ = std::move(request);
            pending_token_ = token;
        }
        work_available_.notify_one();
        return token;
    }

    /// Cancel the request in flight and anything queued behind it.
    void cancel() {
        {
            const std::scoped_lock guard(mutex_);
            cancelled_through_.store(next_token_, std::memory_order_relaxed);
            if (pending_.has_value()) {
                superseded_.emplace_back(std::move(*pending_), pending_token_);
                pending_.reset();
            }
        }
        work_available_.notify_one();
    }

    /// Block until nothing is running or queued. Intended for shutdown and for
    /// tests; ordinary callers rely on the completion callback.
    void wait_idle() {
        std::unique_lock<std::mutex> guard(mutex_);
        idle_.wait(guard, [this] {
            return (!pending_.has_value() && !busy_ && superseded_.empty()) || shutdown_;
        });
    }

    /// Whether `token` has been superseded or cancelled. Polled by a running
    /// walk; cheap enough to check per entry at any depth.
    [[nodiscard]] bool cancelled(std::uint64_t token) const {
        return cancelled_through_.load(std::memory_order_relaxed) >= token;
    }

    /// Whether callbacks may still be delivered. False once teardown begins.
    [[nodiscard]] bool delivering() const {
        return deliver_callbacks_.load(std::memory_order_relaxed);
    }

  private:
    void run() {
        while (true) {
            Request request;
            std::uint64_t token = 0;
            std::vector<std::pair<Request, std::uint64_t>> abandoned;

            {
                std::unique_lock<std::mutex> guard(mutex_);
                work_available_.wait(guard, [this] {
                    return shutdown_ || pending_.has_value() || !superseded_.empty();
                });
                if (shutdown_) {
                    return;
                }

                abandoned.swap(superseded_);
                if (pending_.has_value()) {
                    request = std::move(*pending_);
                    token = pending_token_;
                    pending_.reset();
                    busy_ = true;
                }
            }

            // Answer requests that were replaced before they started, outside
            // the lock: every request gets exactly one completion callback.
            for (const auto& [abandoned_request, abandoned_token] : abandoned) {
                if (abandon_.answer && delivering()) {
                    abandon_.answer(abandoned_request, abandoned_token);
                }
            }

            if (token != 0) {
                execute_.run(std::move(request), token);
                {
                    const std::scoped_lock guard(mutex_);
                    busy_ = false;
                }
            }
            idle_.notify_all();
        }
    }

    Execute execute_;
    Abandon abandon_;

    mutable std::mutex mutex_;
    std::condition_variable work_available_;
    std::condition_variable idle_;
    std::optional<Request> pending_;
    std::uint64_t pending_token_ = 0;
    std::uint64_t next_token_ = 1;
    /// Requests replaced before they started, awaiting a cancelled summary.
    std::vector<std::pair<Request, std::uint64_t>> superseded_;
    bool busy_ = false;
    bool shutdown_ = false;
    std::atomic<std::uint64_t> cancelled_through_{0};
    std::atomic<bool> deliver_callbacks_{true};
    std::thread worker_;
};

} // namespace odysea::core::detail

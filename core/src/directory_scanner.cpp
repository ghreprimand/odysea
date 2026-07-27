#include "odysea/core/directory_scanner.hpp"

#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

DirectoryScanner::DirectoryScanner() : worker_([this] { run(); }) {}

DirectoryScanner::~DirectoryScanner() {
    {
        const std::lock_guard<std::mutex> guard(mutex_);
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

std::uint64_t DirectoryScanner::start(Request request) {
    std::uint64_t token = 0;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
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

void DirectoryScanner::cancel() {
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        cancelled_through_.store(next_token_, std::memory_order_relaxed);
        if (pending_.has_value()) {
            superseded_.emplace_back(std::move(*pending_), pending_token_);
            pending_.reset();
        }
    }
    work_available_.notify_one();
}

void DirectoryScanner::wait_idle() {
    std::unique_lock<std::mutex> guard(mutex_);
    idle_.wait(guard, [this] {
        return (!pending_.has_value() && !busy_ && superseded_.empty()) || shutdown_;
    });
}

bool DirectoryScanner::cancelled(std::uint64_t token) const {
    return cancelled_through_.load(std::memory_order_relaxed) >= token;
}

void DirectoryScanner::run() {
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

        // Answer requests that were replaced before they started, outside the
        // lock: every request gets exactly one completion callback.
        for (auto& [abandoned_request, abandoned_token] : abandoned) {
            if (abandoned_request.on_complete &&
                deliver_callbacks_.load(std::memory_order_relaxed)) {
                abandoned_request.on_complete(ScanSummary{.directory = abandoned_request.directory,
                                                          .token = abandoned_token,
                                                          .entry_count = 0,
                                                          .error = {},
                                                          .cancelled = true});
            }
        }

        if (token != 0) {
            execute(std::move(request), token);
            {
                const std::lock_guard<std::mutex> guard(mutex_);
                busy_ = false;
            }
        }
        idle_.notify_all();
    }
}

void DirectoryScanner::execute(Request request, std::uint64_t token) {
    ScanSummary summary{.directory = request.directory,
                        .token = token,
                        .entry_count = 0,
                        .error = {},
                        .cancelled = false};

    const std::size_t batch_size = request.batch_size == 0 ? 1 : request.batch_size;
    std::vector<Entry> batch;
    batch.reserve(batch_size);

    const bool deliver = deliver_callbacks_.load(std::memory_order_relaxed);
    auto flush = [&] {
        if (batch.empty()) {
            return;
        }
        summary.entry_count += batch.size();
        if (request.on_batch && deliver_callbacks_.load(std::memory_order_relaxed)) {
            request.on_batch(token, std::move(batch));
        }
        batch.clear();
        batch.reserve(batch_size);
    };

    std::error_code open_error;
    fs::directory_iterator iterator(request.directory,
                                    fs::directory_options::skip_permission_denied, open_error);
    if (open_error) {
        summary.error = open_error;
    } else {
        const fs::directory_iterator end;
        std::error_code step_error;
        while (iterator != end) {
            if (cancelled(token)) {
                summary.cancelled = true;
                break;
            }

            const fs::directory_entry& element = *iterator;
            if (request.options.show_hidden ||
                !is_hidden_name(element.path().filename().string())) {
                batch.push_back(make_entry(element));
                if (batch.size() >= batch_size) {
                    flush();
                }
            }

            iterator.increment(step_error);
            if (step_error) {
                summary.error = step_error;
                break;
            }
        }
    }

    if (!summary.cancelled) {
        flush();
    }
    if (cancelled(token)) {
        summary.cancelled = true;
    }

    if (request.on_complete && deliver && deliver_callbacks_.load(std::memory_order_relaxed)) {
        request.on_complete(std::move(summary));
    }
}

} // namespace odysea::core

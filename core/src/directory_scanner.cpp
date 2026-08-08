#include "odysea/core/directory_scanner.hpp"

#include "scan_worker.hpp"

#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

/// Deliver the summary a request receives when it was replaced before it ever
/// ran. Every request gets exactly one completion callback.
void abandon(const DirectoryScanner::Request& request, std::uint64_t token) {
    if (request.on_complete) {
        request.on_complete(ScanSummary{.directory = request.directory,
                                        .token = token,
                                        .entry_count = 0,
                                        .error = {},
                                        .cancelled = true});
    }
}

} // namespace

struct DirectoryScanner::State {
    using Worker = detail::ScanWorker<Request>;

    void execute(Request request, std::uint64_t token) const;

    // Declared last: reverse member destruction joins the worker before
    // anything a callback reaches is destroyed.
    Worker worker;

    State()
        : worker(Worker::Execute{[this](Request request, std::uint64_t token) {
                     execute(std::move(request), token);
                 }},
                 Worker::Abandon{[](const Request& request, std::uint64_t token) {
                     abandon(request, token);
                 }}) {}
};

void DirectoryScanner::State::execute(Request request, std::uint64_t token) const {
    ScanSummary summary{.directory = request.directory,
                        .token = token,
                        .entry_count = 0,
                        .error = {},
                        .cancelled = false};

    const std::size_t batch_size = request.batch_size == 0 ? 1 : request.batch_size;
    std::vector<Entry> batch;
    batch.reserve(batch_size);

    const bool deliver = worker.delivering();
    auto flush = [&] {
        if (batch.empty()) {
            return;
        }
        summary.entry_count += batch.size();
        if (request.on_batch && worker.delivering()) {
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
            if (worker.cancelled(token)) {
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
    if (worker.cancelled(token)) {
        summary.cancelled = true;
    }

    if (request.on_complete && deliver && worker.delivering()) {
        request.on_complete(std::move(summary));
    }
}

DirectoryScanner::DirectoryScanner() : state_(std::make_unique<State>()) {}

DirectoryScanner::~DirectoryScanner() = default;

std::uint64_t DirectoryScanner::start(Request request) {
    return state_->worker.start(std::move(request));
}

void DirectoryScanner::cancel() {
    state_->worker.cancel();
}

void DirectoryScanner::wait_idle() {
    state_->worker.wait_idle();
}

} // namespace odysea::core

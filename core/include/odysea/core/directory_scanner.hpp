// OdySea core: cancellable off-thread directory scanning.
//
// Toolkit-agnostic. Navigation must never block on a slow or enormous
// directory, so scanning happens on a worker thread and results arrive as
// incremental batches. Starting a new scan cancels the previous one, which is
// what makes rapid navigation feel instant: the abandoned directory stops
// costing anything as soon as the user moves on.
#pragma once

#include "odysea/core/directory_model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <system_error>
#include <vector>

namespace odysea::core {

/// How a scan ended.
struct ScanSummary {
    std::filesystem::path directory;
    /// The token returned by the `start` call this summary answers.
    std::uint64_t token = 0;
    /// Entries delivered through batches before the scan ended.
    std::size_t entry_count = 0;
    /// Set when the directory could not be opened or iteration failed.
    std::error_code error;
    /// True when a newer request or an explicit cancel ended the scan early.
    bool cancelled = false;
};

/// A single-worker directory scanner where the newest request wins.
///
/// Callbacks are invoked on the scanner's worker thread, never on the caller's
/// thread, and never while a lock is held. Consumers that own thread affinity,
/// such as a UI model, marshal the batch onto their own thread. The scanner is
/// neither copyable nor movable so callbacks can safely capture its address.
class DirectoryScanner {
  public:
    /// Receives entries in filesystem discovery order. Use `sort_entries` to
    /// place them in presentation order.
    using BatchHandler = std::function<void(std::uint64_t token, std::vector<Entry> entries)>;
    using CompletionHandler = std::function<void(ScanSummary summary)>;

    struct Request {
        std::filesystem::path directory;
        ListOptions options;
        /// Entries per batch. Smaller batches show results sooner, larger
        /// batches cost fewer hand-offs.
        std::size_t batch_size = 256;
        BatchHandler on_batch;
        CompletionHandler on_complete;
    };

    DirectoryScanner();

    DirectoryScanner(const DirectoryScanner&) = delete;
    DirectoryScanner& operator=(const DirectoryScanner&) = delete;
    DirectoryScanner(DirectoryScanner&&) = delete;
    DirectoryScanner& operator=(DirectoryScanner&&) = delete;

    /// Cancels any work in flight and joins the worker. Requests that have not
    /// completed receive no further callbacks.
    ~DirectoryScanner();

    /// Queue a scan and return immediately with its token.
    ///
    /// Any scan in flight is cancelled and a request still waiting is
    /// superseded; both report a cancelled summary.
    std::uint64_t start(Request request);

    /// Cancel the scan in flight and anything queued behind it.
    void cancel();

    /// Block until no scan is running or queued. Intended for shutdown and
    /// for tests; ordinary callers rely on the completion callback.
    void wait_idle();

  private:
    /// The worker and the shared cancellation contract live behind this
    /// pointer so the public header stays free of threading internals.
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace odysea::core

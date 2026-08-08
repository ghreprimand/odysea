// Headless tests for the shared single-worker, newest-request-wins contract.
//
// Two long-running walks depend on these guarantees — directory listing and
// storage-usage accounting — so they are pinned once, here, against the seam
// itself rather than only through whichever walk happens to use them.
#include "scan_worker.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using odysea::core::detail::ScanWorker;
using odysea::test::check;
using namespace std::chrono_literals;

namespace {

/// The smallest request a worker can carry: an identifier to trace.
struct Job {
    int id = 0;
};

/// Records what the worker did, and lets one job block until released.
class Harness {
  public:
    /// `blocking_id` names the one job that waits inside its execution.
    explicit Harness(int blocking_id) : blocking_id_(blocking_id) {}

    [[nodiscard]] std::vector<int> executed() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return executed_;
    }

    [[nodiscard]] std::vector<int> abandoned() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return abandoned_;
    }

    [[nodiscard]] std::vector<std::uint64_t> tokens() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return tokens_;
    }

    [[nodiscard]] std::thread::id execution_thread() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return execution_thread_;
    }

    /// True once the blocking job has entered its execution.
    bool wait_until_blocked() {
        std::unique_lock<std::mutex> guard(mutex_);
        return gate_.wait_for(guard, 10s, [this] { return blocked_; });
    }

    void release() {
        {
            const std::lock_guard<std::mutex> guard(mutex_);
            released_ = true;
        }
        gate_.notify_all();
    }

    /// Whether the running job saw its own token reported as cancelled.
    [[nodiscard]] bool observed_cancellation() {
        const std::lock_guard<std::mutex> guard(mutex_);
        return observed_cancellation_;
    }

    /// Bind the worker the callbacks belong to. Called once, before any
    /// request is queued, so no job can read it unset.
    void attach(const ScanWorker<Job>* worker) { worker_ = worker; }

    void run(Job job, std::uint64_t token) {
        {
            const std::lock_guard<std::mutex> guard(mutex_);
            executed_.push_back(job.id);
            tokens_.push_back(token);
            execution_thread_ = std::this_thread::get_id();
        }
        if (job.id != blocking_id_) {
            return;
        }

        std::unique_lock<std::mutex> guard(mutex_);
        blocked_ = true;
        gate_.notify_all();
        gate_.wait_for(guard, 10s, [this] { return released_; });
        guard.unlock();
        const bool cancelled = worker_->cancelled(token);
        const std::lock_guard<std::mutex> record(mutex_);
        observed_cancellation_ = cancelled;
    }

    void abandon(const Job& job, std::uint64_t token) {
        const std::lock_guard<std::mutex> guard(mutex_);
        abandoned_.push_back(job.id);
        tokens_.push_back(token);
    }

  private:
    const ScanWorker<Job>* worker_ = nullptr;
    std::mutex mutex_;
    std::condition_variable gate_;
    std::vector<int> executed_;
    std::vector<int> abandoned_;
    std::vector<std::uint64_t> tokens_;
    std::thread::id execution_thread_;
    int blocking_id_ = 0;
    bool blocked_ = false;
    bool released_ = false;
    bool observed_cancellation_ = false;
};

/// Build a worker bound to `harness`. A factory rather than a member so the
/// harness outlives the worker in every test scope, which is the ordering a
/// real owner gets for free by declaring the worker last.
std::unique_ptr<ScanWorker<Job>> worker_for(Harness& harness) {
    auto worker = std::make_unique<ScanWorker<Job>>(
        ScanWorker<Job>::Execute{
            [&harness](Job job, std::uint64_t token) { harness.run(job, token); }},
        ScanWorker<Job>::Abandon{
            [&harness](const Job& job, std::uint64_t token) { harness.abandon(job, token); }});
    harness.attach(worker.get());
    return worker;
}

void test_work_runs_off_the_calling_thread() {
    Harness harness(0);
    auto worker = worker_for(harness);

    const std::uint64_t token = worker->start(Job{.id = 1});
    worker->wait_idle();

    check(token == 1, "the first request receives a non-zero token");
    check(harness.executed() == std::vector<int>{1}, "a queued request runs");
    check(harness.execution_thread() != std::this_thread::get_id(),
          "requests run off the calling thread");
}

void test_every_request_receives_exactly_one_callback() {
    Harness harness(1);
    auto worker = worker_for(harness);

    static_cast<void>(worker->start(Job{.id = 1}));
    check(harness.wait_until_blocked(), "the first request starts before it is superseded");

    // Requests two through four are replaced while still waiting; only the
    // last one queued survives to run.
    for (int id = 2; id <= 5; ++id) {
        static_cast<void>(worker->start(Job{.id = id}));
    }
    harness.release();
    worker->wait_idle();

    const std::vector<int> executed = harness.executed();
    const std::vector<int> abandoned = harness.abandoned();
    check(executed == std::vector<int>({1, 5}), "the newest queued request is the one that runs");
    check(abandoned == std::vector<int>({2, 3, 4}),
          "requests replaced before starting are answered as abandoned");
    check(executed.size() + abandoned.size() == 5,
          "every request receives exactly one callback, and only one");

    const std::vector<std::uint64_t> tokens = harness.tokens();
    check(tokens.size() == 5, "every callback carries the token of its own request");
    std::vector<std::uint64_t> sorted = tokens;
    std::ranges::sort(sorted);
    check(sorted == std::vector<std::uint64_t>({1, 2, 3, 4, 5}),
          "tokens are handed out in order and none is reused");
}

void test_a_newer_request_cancels_the_running_one() {
    Harness harness(1);
    auto worker = worker_for(harness);

    const std::uint64_t first = worker->start(Job{.id = 1});
    check(harness.wait_until_blocked(), "the first request starts");
    check(!worker->cancelled(first), "a request running alone is not cancelled");

    const std::uint64_t second = worker->start(Job{.id = 2});
    check(second > first, "each request receives a fresh token");
    check(worker->cancelled(first), "a newer request cancels the one in flight");
    check(!worker->cancelled(second), "a newer request does not cancel itself");

    harness.release();
    worker->wait_idle();
    check(harness.observed_cancellation(),
          "the running request can see its own cancellation from inside the walk");
}

void test_explicit_cancel_stops_the_running_request() {
    Harness harness(1);
    auto worker = worker_for(harness);

    const std::uint64_t token = worker->start(Job{.id = 1});
    check(harness.wait_until_blocked(), "the request starts");

    worker->cancel();
    check(worker->cancelled(token), "an explicit cancel reaches the request in flight");

    harness.release();
    worker->wait_idle();
    check(harness.observed_cancellation(), "the cancelled request sees the cancellation");
}

void test_teardown_stops_delivery_and_joins() {
    Harness harness(1);
    bool delivering_during_teardown = true;
    std::uint64_t late_token = 1;

    {
        auto worker = worker_for(harness);
        ScanWorker<Job>& live = *worker;
        static_cast<void>(worker->start(Job{.id = 1}));
        check(harness.wait_until_blocked(), "the request starts before teardown");
        check(live.delivering(), "callbacks are delivered while the worker is alive");

        // Destroying the worker from another thread lets this thread observe
        // the flag flip while a request is still inside its execution: the
        // destructor stops delivery first, then blocks joining the worker,
        // and the join cannot finish until the blocked request is released.
        // That is what keeps the worker alive for the checks below.
        std::thread destroyer([&worker] { worker.reset(); });
        for (int attempt = 0; attempt < 1000 && delivering_during_teardown; ++attempt) {
            delivering_during_teardown = live.delivering();
            std::this_thread::sleep_for(10ms);
        }
        late_token = live.start(Job{.id = 9});
        harness.release();
        destroyer.join();
    }

    check(!delivering_during_teardown, "teardown stops callback delivery before it joins");
    check(late_token == 0, "a request queued during teardown is refused with a zero token");
    const std::vector<int> executed = harness.executed();
    const std::vector<int> abandoned = harness.abandoned();
    check(std::ranges::find(executed, 9) == executed.end() &&
              std::ranges::find(abandoned, 9) == abandoned.end(),
          "a refused request receives no callback at all");
    check(true, "destroying a worker mid-request completes without hanging");
}

} // namespace

int main() {
    test_work_runs_off_the_calling_thread();
    test_every_request_receives_exactly_one_callback();
    test_a_newer_request_cancels_the_running_one();
    test_explicit_cancel_stops_the_running_request();
    test_teardown_stops_delivery_and_joins();
    return odysea::test::report("scan_worker");
}

// Headless tests for thumbnail scheduling, caching, and cancellation.
//
// The producer and the store are fakes, so no codec, no image file, and no
// display server is involved: what is under test is the scheduling behaviour
// itself. Every wait is a latch or a counter rather than a sleep, so the suite
// is deterministic and fails rather than flaking when a guarantee is broken.
#include "odysea/core/thumbnail_service.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

using odysea::core::StoredThumbnail;
using odysea::core::ThumbnailImage;
using odysea::core::ThumbnailKey;
using odysea::core::ThumbnailPriority;
using odysea::core::ThumbnailResult;
using odysea::core::ThumbnailService;
using odysea::core::ThumbnailServiceOptions;
using odysea::core::ThumbnailSize;
using odysea::test::check;

namespace {

constexpr std::size_t image_bytes = 64;

[[nodiscard]] ThumbnailImage make_image() {
    return ThumbnailImage{
        .pixels = std::vector<std::byte>(image_bytes, std::byte{0x7f}), .width = 4, .height = 4};
}

[[nodiscard]] ThumbnailKey make_key(int index) {
    return ThumbnailKey{.uri = "file:///tmp/odysea/source_" + std::to_string(index) + ".png",
                        .modified_seconds = 1000 + index,
                        .size = 4096,
                        .edge = ThumbnailSize::Normal};
}

[[nodiscard]] std::filesystem::path make_source(int index) {
    return std::filesystem::path("/tmp/odysea/source_" + std::to_string(index) + ".png");
}

/// A rendezvous the producer blocks on, so a test can hold work in flight and
/// release it at a chosen moment without ever sleeping.
class Gate {
  public:
    void arrive() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++arrived_;
        arrivals_.notify_all();
        opened_.wait(lock, [this] { return open_; });
    }

    void await_arrivals(std::size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);
        arrivals_.wait(lock, [this, count] { return arrived_ >= count; });
    }

    void open() {
        {
            const std::scoped_lock lock(mutex_);
            open_ = true;
        }
        opened_.notify_all();
    }

  private:
    std::mutex mutex_;
    std::condition_variable arrivals_;
    std::condition_variable opened_;
    std::size_t arrived_ = 0;
    bool open_ = false;
};

/// A producer that counts its calls, can be made to refuse, and can be held at
/// a gate.
class FakeProducer : public odysea::core::ThumbnailProducer {
  public:
    ThumbnailImage produce(const std::filesystem::path& source, ThumbnailSize size,
                           std::error_code& error) override {
        static_cast<void>(source);
        static_cast<void>(size);
        {
            const std::scoped_lock lock(mutex_);
            ++calls_;
        }
        if (gate_ != nullptr) {
            gate_->arrive();
        }
        if (refuse_) {
            error = std::make_error_code(std::errc::not_supported);
            return {};
        }
        return make_image();
    }

    [[nodiscard]] std::size_t calls() const {
        const std::scoped_lock lock(mutex_);
        return calls_;
    }

    void hold_at(Gate* gate) { gate_ = gate; }
    void refuse(bool refuse) { refuse_ = refuse; }

  private:
    mutable std::mutex mutex_;
    std::size_t calls_ = 0;
    Gate* gate_ = nullptr;
    bool refuse_ = false;
};

/// A store that reports whatever a test planted and records what was saved.
class FakeStore : public odysea::core::ThumbnailStore {
  public:
    std::optional<StoredThumbnail> load(const ThumbnailKey& key, std::error_code& error) override {
        static_cast<void>(error);
        const std::scoped_lock lock(mutex_);
        ++loads_;
        const auto found = planted_.find(key.uri);
        if (found == planted_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    void save(const ThumbnailKey& key, const ThumbnailImage& image,
              std::error_code& error) override {
        static_cast<void>(image);
        static_cast<void>(error);
        const std::scoped_lock lock(mutex_);
        saved_.push_back(key);
    }

    void plant(const std::string& uri, StoredThumbnail stored) {
        const std::scoped_lock lock(mutex_);
        planted_.insert_or_assign(uri, std::move(stored));
    }

    [[nodiscard]] std::vector<ThumbnailKey> saved() const {
        const std::scoped_lock lock(mutex_);
        return saved_;
    }

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, StoredThumbnail> planted_;
    std::vector<ThumbnailKey> saved_;
    std::size_t loads_ = 0;
};

/// Collects results from every worker thread that delivers one.
class Recorder {
  public:
    void record(ThumbnailResult result) {
        const std::scoped_lock lock(mutex_);
        results_.push_back(std::move(result));
    }

    [[nodiscard]] std::vector<ThumbnailResult> results() const {
        const std::scoped_lock lock(mutex_);
        return results_;
    }

    [[nodiscard]] std::size_t count() const {
        const std::scoped_lock lock(mutex_);
        return results_.size();
    }

  private:
    mutable std::mutex mutex_;
    std::vector<ThumbnailResult> results_;
};

[[nodiscard]] ThumbnailServiceOptions single_worker(std::size_t budget = 1024UL * 1024UL) {
    return ThumbnailServiceOptions{.worker_count = 1,
                                   .memory_budget_bytes = budget,
                                   .queue_bound = 4096,
                                   .failure_memory = 1024};
}

void test_repeated_requests_for_one_key_decode_once() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;
    Gate gate;
    producer.hold_at(&gate);

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t generation = service.begin_generation();
    const ThumbnailKey key = make_key(0);

    service.request(make_source(0), key, generation, ThumbnailPriority::Visible);
    // Hold the first request inside the producer so the rest are guaranteed to
    // arrive while it is still running.
    gate.await_arrivals(1);

    constexpr int extra_requests = 49;
    for (int index = 0; index < extra_requests; ++index) {
        service.request(make_source(0), key, generation, ThumbnailPriority::Visible);
    }
    gate.open();
    service.wait_idle();

    check(producer.calls() == 1, "one source is decoded once no matter how often it is asked for");
    check(recorder.count() == static_cast<std::size_t>(extra_requests) + 1,
          "every request is still answered separately");
}

void test_a_second_pass_is_served_from_memory() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t generation = service.begin_generation();
    service.request(make_source(1), make_key(1), generation, ThumbnailPriority::Visible);
    service.wait_idle();
    service.request(make_source(1), make_key(1), generation, ThumbnailPriority::Visible);
    service.wait_idle();

    check(producer.calls() == 1, "a cached thumbnail is not decoded again");
    const auto results = recorder.results();
    check(results.size() == 2, "both passes are answered");
    check(results.size() == 2 && !results[0].from_cache && results[1].from_cache,
          "the second answer is reported as cached");
    check(results.size() == 2 && results[1].image != nullptr &&
              results[1].image->byte_cost() == image_bytes,
          "a cached answer carries the image");
}

void test_cancelled_work_is_never_delivered() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;
    Gate gate;
    producer.hold_at(&gate);

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t abandoned = service.begin_generation();
    constexpr int queued = 500;
    for (int index = 0; index < queued; ++index) {
        service.request(make_source(index), make_key(index), abandoned,
                        ThumbnailPriority::Background);
    }
    // One request is inside the producer; the rest are queued behind it.
    gate.await_arrivals(1);

    const std::uint64_t current = service.begin_generation();
    service.cancel_before(current);
    gate.open();
    service.wait_idle();

    check(recorder.count() == 0, "an abandoned generation delivers nothing");
    check(producer.calls() == 1, "work queued for an abandoned generation is never started");
}

void test_a_request_from_an_abandoned_generation_is_refused() {
    // A view that has already moved on can still be finishing its own loop and
    // file requests for what it used to show. Those are refused outright rather
    // than queued and later discarded, so a fast scroll does not pay for every
    // row it passed.
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t abandoned = service.begin_generation();
    const std::uint64_t current = service.begin_generation();
    service.cancel_before(current);

    service.request(make_source(15), make_key(15), abandoned, ThumbnailPriority::Visible);
    service.wait_idle();

    check(producer.calls() == 0, "a request for an abandoned generation is never started");
    check(recorder.count() == 0, "a request for an abandoned generation is never answered");

    service.request(make_source(15), make_key(15), current, ThumbnailPriority::Visible);
    service.wait_idle();
    check(producer.calls() == 1, "the same source is still served for the current generation");
}

void test_the_newest_generation_still_arrives() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;
    Gate gate;
    producer.hold_at(&gate);

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t abandoned = service.begin_generation();
    service.request(make_source(10), make_key(10), abandoned, ThumbnailPriority::Visible);
    gate.await_arrivals(1);
    service.request(make_source(11), make_key(11), abandoned, ThumbnailPriority::Visible);

    const std::uint64_t current = service.begin_generation();
    service.cancel_before(current);
    service.request(make_source(12), make_key(12), current, ThumbnailPriority::Visible);
    gate.open();
    service.wait_idle();

    const auto results = recorder.results();
    check(results.size() == 1, "only the current generation is answered");
    check(results.size() == 1 && results[0].key == make_key(12),
          "the answer belongs to the entry the view actually shows");
    check(results.size() == 1 && results[0].generation == current,
          "the answer carries the generation it was filed under");
}

void test_memory_stays_inside_its_budget() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;

    // Room for exactly two decoded images.
    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker(image_bytes * 2));

    const std::uint64_t generation = service.begin_generation();
    const auto fetch = [&service, generation](int index) {
        service.request(make_source(index), make_key(index), generation,
                        ThumbnailPriority::Visible);
        service.wait_idle();
    };

    fetch(20);
    fetch(21);
    check(service.cached_bytes() == image_bytes * 2, "the cache fills to its budget");

    // Touching the older entry makes it the most recent, so the other one is
    // what a further insertion must evict.
    fetch(20);
    check(producer.calls() == 2, "a retained entry is served without decoding");

    fetch(22);
    check(service.cached_bytes() == image_bytes * 2, "the cache never exceeds its budget");
    check(producer.calls() == 3, "a new source is decoded");

    fetch(20);
    check(producer.calls() == 3, "the recently used entry survived");
    fetch(21);
    check(producer.calls() == 4, "the least recently used entry was the one evicted");
}

void test_a_refused_source_is_not_retried_until_it_changes() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;
    producer.refuse(true);

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t generation = service.begin_generation();
    const ThumbnailKey key = make_key(30);
    for (int attempt = 0; attempt < 5; ++attempt) {
        service.request(make_source(30), key, generation, ThumbnailPriority::Visible);
        service.wait_idle();
    }

    check(producer.calls() == 1, "a source that cannot be decoded is not decoded again");
    const auto results = recorder.results();
    check(results.size() == 5, "every attempt is still answered");
    check(std::ranges::all_of(results,
                              [](const ThumbnailResult& result) {
                                  return result.error == std::errc::not_supported &&
                                         result.image == nullptr;
                              }),
          "every answer reports the refusal");

    service.invalidate(key);
    service.request(make_source(30), key, generation, ThumbnailPriority::Visible);
    service.wait_idle();
    check(producer.calls() == 2, "forgetting a refusal makes the source eligible again");
}

void test_a_stored_thumbnail_is_used_only_while_it_is_current() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;

    const ThumbnailKey current = make_key(40);
    store.plant(current.uri, StoredThumbnail{.image = make_image(),
                                             .uri = current.uri,
                                             .modified_seconds = current.modified_seconds,
                                             .size = current.size,
                                             .size_recorded = true});

    const ThumbnailKey stale = make_key(41);
    store.plant(stale.uri, StoredThumbnail{.image = make_image(),
                                           .uri = stale.uri,
                                           .modified_seconds = stale.modified_seconds - 1,
                                           .size = stale.size,
                                           .size_recorded = true});

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t generation = service.begin_generation();
    service.request(make_source(40), current, generation, ThumbnailPriority::Visible);
    service.wait_idle();
    check(producer.calls() == 0, "a current stored thumbnail is used as it is");

    service.request(make_source(41), stale, generation, ThumbnailPriority::Visible);
    service.wait_idle();
    check(producer.calls() == 1, "a stored thumbnail describing an older revision is not used");

    const auto saved = store.saved();
    check(saved.size() == 1 && saved.front() == stale,
          "a freshly decoded thumbnail is written back with the description that verifies it");
}

void test_visible_work_is_served_before_background_work() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;
    Gate gate;
    producer.hold_at(&gate);

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t generation = service.begin_generation();
    // Occupy the only worker so everything below is queued, not raced.
    service.request(make_source(50), make_key(50), generation, ThumbnailPriority::Visible);
    gate.await_arrivals(1);

    constexpr int background_count = 8;
    for (int index = 0; index < background_count; ++index) {
        service.request(make_source(60 + index), make_key(60 + index), generation,
                        ThumbnailPriority::Background);
    }
    service.request(make_source(70), make_key(70), generation, ThumbnailPriority::Visible);

    gate.open();
    service.wait_idle();

    const auto results = recorder.results();
    check(results.size() == static_cast<std::size_t>(background_count) + 2,
          "everything queued is answered");
    check(results.size() >= 2 && results[1].key == make_key(70),
          "the entry the user can see is decoded before the ones they cannot");
}

void test_a_released_request_is_withdrawn() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;
    Gate gate;
    producer.hold_at(&gate);

    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        single_worker());

    const std::uint64_t generation = service.begin_generation();
    service.request(make_source(80), make_key(80), generation, ThumbnailPriority::Visible);
    gate.await_arrivals(1);

    const ThumbnailKey abandoned = make_key(81);
    service.request(make_source(81), abandoned, generation, ThumbnailPriority::Background);
    service.release(abandoned, generation);

    gate.open();
    service.wait_idle();

    check(producer.calls() == 1, "a withdrawn request is never decoded");
    const auto results = recorder.results();
    check(std::ranges::none_of(
              results, [&abandoned](const ThumbnailResult& r) { return r.key == abandoned; }),
          "a withdrawn request is not answered");
}

void test_the_queue_does_not_grow_without_bound() {
    FakeProducer producer;
    FakeStore store;
    Recorder recorder;
    Gate gate;
    producer.hold_at(&gate);

    constexpr std::size_t bound = 8;
    ThumbnailService service(
        producer, store,
        [&recorder](ThumbnailResult result) { recorder.record(std::move(result)); },
        ThumbnailServiceOptions{.worker_count = 1,
                                .memory_budget_bytes = 1024UL * 1024UL,
                                .queue_bound = bound,
                                .failure_memory = 1024});

    const std::uint64_t generation = service.begin_generation();
    service.request(make_source(90), make_key(90), generation, ThumbnailPriority::Visible);
    gate.await_arrivals(1);

    constexpr int flood = 400;
    for (int index = 0; index < flood; ++index) {
        service.request(make_source(100 + index), make_key(100 + index), generation,
                        ThumbnailPriority::Background);
    }

    gate.open();
    service.wait_idle();

    check(recorder.count() <= bound + 1,
          "a flood of requests is bounded rather than queued without limit");
    check(recorder.count() >= 1, "the bounded queue still does useful work");
}

void test_destruction_while_working_delivers_nothing_afterwards() {
    // What matters is that tearing down a busy service terminates, leaves no
    // worker running behind it, and never calls back into a handler once it is
    // gone. Repeated so the window between finishing a decode and joining is
    // hit rather than assumed.
    struct Observation {
        std::mutex mutex;
        bool closed = false;
        bool delivered_after_close = false;
    };

    constexpr int rounds = 100;
    bool late_delivery = false;
    for (int round = 0; round < rounds; ++round) {
        auto observation = std::make_shared<Observation>();
        FakeProducer producer;
        FakeStore store;
        Gate gate;
        producer.hold_at(&gate);

        {
            ThumbnailService service(
                producer, store,
                [observation](ThumbnailResult result) {
                    static_cast<void>(result);
                    const std::scoped_lock lock(observation->mutex);
                    if (observation->closed) {
                        observation->delivered_after_close = true;
                    }
                },
                single_worker());

            const std::uint64_t generation = service.begin_generation();
            constexpr int queued = 32;
            for (int index = 0; index < queued; ++index) {
                service.request(make_source(200 + index), make_key(200 + index), generation,
                                ThumbnailPriority::Visible);
            }
            gate.await_arrivals(1);
            // Release the decode and tear the service down in the same breath.
            gate.open();
        }

        {
            const std::scoped_lock lock(observation->mutex);
            observation->closed = true;
            late_delivery = late_delivery || observation->delivered_after_close;
        }
    }

    check(!late_delivery, "no result is delivered once the service is gone");
}

} // namespace

int main() {
    test_repeated_requests_for_one_key_decode_once();
    test_a_second_pass_is_served_from_memory();
    test_cancelled_work_is_never_delivered();
    test_a_request_from_an_abandoned_generation_is_refused();
    test_the_newest_generation_still_arrives();
    test_memory_stays_inside_its_budget();
    test_a_refused_source_is_not_retried_until_it_changes();
    test_a_stored_thumbnail_is_used_only_while_it_is_current();
    test_visible_work_is_served_before_background_work();
    test_a_released_request_is_withdrawn();
    test_the_queue_does_not_grow_without_bound();
    test_destruction_while_working_delivers_nothing_afterwards();

    return odysea::test::report("core_thumbnail_service");
}

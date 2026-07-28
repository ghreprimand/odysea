// OdySea core: thumbnail scheduling, caching, and cancellation.
//
// Toolkit-agnostic. Producing pixels needs a codec and storing them needs an
// image writer, so both are supplied by the caller through the two interfaces
// below. What remains here is the part that has to be right under load and can
// be verified headless: work is deduplicated, visible entries are served first,
// abandoned work stops costing anything, memory stays bounded, and a source
// that cannot be thumbnailed is not retried in a loop.
#pragma once

#include "odysea/core/thumbnail.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

namespace odysea::core {

/// Turns a file into pixels.
///
/// Implemented outside this library, by the layer that already links an image
/// codec. Called on a worker thread and possibly on several at once, so an
/// implementation must be safe to enter concurrently. It must not throw:
/// refusal is reported through `error`, which covers an unreadable file, an
/// unsupported format, and a source large enough that decoding it would be
/// unwise.
class ThumbnailProducer {
  public:
    ThumbnailProducer() = default;
    ThumbnailProducer(const ThumbnailProducer&) = delete;
    ThumbnailProducer& operator=(const ThumbnailProducer&) = delete;
    ThumbnailProducer(ThumbnailProducer&&) = delete;
    ThumbnailProducer& operator=(ThumbnailProducer&&) = delete;
    virtual ~ThumbnailProducer() = default;

    /// Produce an image whose longest edge is at most the pixel bound of
    /// `size`. Returns an empty image when `error` is set.
    [[nodiscard]] virtual ThumbnailImage produce(const std::filesystem::path& source,
                                                 ThumbnailSize size, std::error_code& error) = 0;
};

/// Reads and writes the persistent thumbnail cache.
///
/// Implemented outside this library, because the standard stores thumbnails as
/// PNG files carrying their source description in text chunks. Called on a
/// worker thread and possibly on several at once. Must not throw.
class ThumbnailStore {
  public:
    ThumbnailStore() = default;
    ThumbnailStore(const ThumbnailStore&) = delete;
    ThumbnailStore& operator=(const ThumbnailStore&) = delete;
    ThumbnailStore(ThumbnailStore&&) = delete;
    ThumbnailStore& operator=(ThumbnailStore&&) = delete;
    virtual ~ThumbnailStore() = default;

    /// Read whatever is stored for `key`, without judging whether it is
    /// current. Validity is decided here, by `thumbnail_matches`, so a store
    /// reports what it found and nothing more. Yields no value when nothing is
    /// stored; `error` describes a read that failed.
    [[nodiscard]] virtual std::optional<StoredThumbnail> load(const ThumbnailKey& key,
                                                              std::error_code& error) = 0;

    /// Record `image` as the thumbnail of the source `key` describes, together
    /// with the URI, modification time, and length that make it verifiable
    /// later.
    virtual void save(const ThumbnailKey& key, const ThumbnailImage& image,
                      std::error_code& error) = 0;
};

/// Whether a request is for something the user can see right now.
enum class ThumbnailPriority { Visible, Background };

/// One answer to one accepted request.
struct ThumbnailResult {
    ThumbnailKey key;
    std::filesystem::path source;
    /// Null when `error` is set. Shared, so a result already handed out stays
    /// valid after the memory cache evicts it.
    std::shared_ptr<const ThumbnailImage> image;
    std::error_code error;
    /// The generation the answered request was filed under.
    std::uint64_t generation = 0;
    /// True when the image came from the memory cache or the persistent store
    /// rather than from the producer.
    bool from_cache = false;
};

struct ThumbnailServiceOptions {
    /// Worker threads. Zero selects a small bounded default, because decoding
    /// is not the only thing the machine is doing.
    std::size_t worker_count = 0;
    /// Ceiling on decoded pixels retained in memory. Cache entries are evicted
    /// least-recently-used until the total fits.
    std::size_t memory_budget_bytes = 64UL * 1024UL * 1024UL;
    /// Ceiling on requests waiting to start. Beyond it the oldest background
    /// request is dropped, exactly as if it had been released.
    std::size_t queue_bound = 4096;
    /// How many refused sources are remembered, so a directory full of files
    /// no codec understands is not decoded again on every pass.
    std::size_t failure_memory = 1024;
};

/// A pool of thumbnail workers with a bounded cache.
///
/// Results are delivered on a worker thread, never on the calling thread and
/// never while a lock is held, matching the directory scanner so a consumer
/// that owns thread affinity marshals both the same way. Several workers can
/// enter the handler at once, so a handler that touches its own state either
/// guards it or, as a user-interface adapter does, immediately posts the result
/// onto the thread that owns that state. Every accepted request produces
/// exactly one result, or none once it is cancelled or released.
///
/// Neither copyable nor movable, so a handler may safely capture its address.
/// The producer and the store are referenced, not owned: both must outlive the
/// service.
class ThumbnailService {
  public:
    using ResultHandler = std::function<void(ThumbnailResult result)>;

    ThumbnailService(ThumbnailProducer& producer, ThumbnailStore& store, ResultHandler handler,
                     ThumbnailServiceOptions options = {});

    ThumbnailService(const ThumbnailService&) = delete;
    ThumbnailService& operator=(const ThumbnailService&) = delete;
    ThumbnailService(ThumbnailService&&) = delete;
    ThumbnailService& operator=(ThumbnailService&&) = delete;

    /// Stops accepting work, suppresses further delivery, and joins every
    /// worker before returning. A decode already running finishes rather than
    /// being torn down, so no result is delivered once the destructor returns,
    /// and the handler must stay valid until it does.
    ~ThumbnailService();

    /// Open a new generation and return its token. Cancelling is a separate,
    /// explicit act, because a view can legitimately have several generations
    /// in flight at once.
    [[nodiscard]] std::uint64_t begin_generation();

    /// Queue a request and return immediately.
    ///
    /// Requesting a key that is already queued or running attaches to that
    /// work instead of repeating it; both requests are still answered
    /// separately. A source excluded from the cache is refused through a result
    /// rather than silently ignored.
    void request(std::filesystem::path source, ThumbnailKey key, std::uint64_t generation,
                 ThumbnailPriority priority);

    /// Withdraw one request for a key that the view no longer shows. Work
    /// already running is left to finish and populate the cache, since paying
    /// for it twice is worse than finishing it once.
    void release(const ThumbnailKey& key, std::uint64_t generation);

    /// Cancel every request filed under a generation older than `generation`.
    /// Queued work is dropped; running work finishes but is not delivered.
    void cancel_before(std::uint64_t generation);

    /// Forget what is remembered about a key, including a remembered refusal,
    /// so the next request reads the source again.
    void invalidate(const ThumbnailKey& key);

    /// Forget everything remembered about a source at any size.
    void invalidate_source(const std::filesystem::path& source);

    /// Block until nothing is queued or running. For shutdown and for tests;
    /// ordinary callers rely on the result handler.
    void wait_idle();

    /// Decoded bytes currently retained in memory.
    [[nodiscard]] std::size_t cached_bytes() const;

  private:
    struct Pending {
        std::filesystem::path source;
        ThumbnailKey key;
        /// One entry per request still waiting for this key.
        std::vector<std::uint64_t> generations;
        ThumbnailPriority priority = ThumbnailPriority::Background;
        bool running = false;
    };

    struct CacheEntry {
        std::shared_ptr<const ThumbnailImage> image;
        std::list<ThumbnailKey>::iterator position;
    };

    void run();
    [[nodiscard]] std::optional<Pending> take_next(std::unique_lock<std::mutex>& lock);
    [[nodiscard]] std::vector<ThumbnailResult> finish(const Pending& claimed,
                                                      std::shared_ptr<const ThumbnailImage> image,
                                                      std::error_code error, bool from_cache);
    void deliver(std::vector<ThumbnailResult> results);
    void remember(const ThumbnailKey& key, const std::shared_ptr<const ThumbnailImage>& image);
    void remember_failure(const ThumbnailKey& key, std::error_code error);
    [[nodiscard]] std::shared_ptr<const ThumbnailImage> recall(const ThumbnailKey& key);
    void enforce_queue_bound();
    void drop_queued(const ThumbnailKey& key);

    ThumbnailProducer& producer_;
    ThumbnailStore& store_;
    ResultHandler handler_;
    ThumbnailServiceOptions options_;

    mutable std::mutex mutex_;
    std::condition_variable work_available_;
    std::condition_variable idle_;

    std::unordered_map<ThumbnailKey, Pending> pending_;
    std::deque<ThumbnailKey> visible_queue_;
    std::deque<ThumbnailKey> background_queue_;

    std::list<ThumbnailKey> cache_order_;
    std::unordered_map<ThumbnailKey, CacheEntry> cache_;
    std::size_t cached_bytes_ = 0;

    std::unordered_map<ThumbnailKey, std::error_code> failures_;
    std::deque<ThumbnailKey> failure_order_;

    std::uint64_t next_generation_ = 1;
    std::uint64_t cancelled_before_ = 0;
    std::size_t running_ = 0;
    bool shutdown_ = false;
    std::atomic<bool> deliver_results_{true};

    std::vector<std::thread> workers_;
};

} // namespace odysea::core

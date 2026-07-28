#include "odysea/core/thumbnail_service.hpp"

#include <algorithm>
#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

constexpr std::size_t default_worker_ceiling = 4;

[[nodiscard]] std::size_t resolve_worker_count(std::size_t requested) {
    if (requested > 0) {
        return requested;
    }
    const unsigned int available = std::thread::hardware_concurrency();
    if (available == 0) {
        return 1;
    }
    return std::min(default_worker_ceiling, static_cast<std::size_t>(available));
}

[[nodiscard]] std::error_code make_error(std::errc code) {
    return std::make_error_code(code);
}

} // namespace

ThumbnailService::ThumbnailService(ThumbnailProducer& producer, ThumbnailStore& store,
                                   ResultHandler handler, ThumbnailServiceOptions options)
    : producer_(producer), store_(store), handler_(std::move(handler)), options_(options) {
    const std::size_t workers = resolve_worker_count(options_.worker_count);
    workers_.reserve(workers);
    for (std::size_t index = 0; index < workers; ++index) {
        workers_.emplace_back([this] { run(); });
    }
}

ThumbnailService::~ThumbnailService() {
    deliver_results_.store(false, std::memory_order_release);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        pending_.clear();
        visible_queue_.clear();
        background_queue_.clear();
    }
    work_available_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::uint64_t ThumbnailService::begin_generation() {
    const std::lock_guard<std::mutex> lock(mutex_);
    return next_generation_++;
}

void ThumbnailService::request(fs::path source, ThumbnailKey key, std::uint64_t generation,
                               ThumbnailPriority priority) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_ || generation < cancelled_before_) {
            return;
        }

        const auto existing = pending_.find(key);
        if (existing != pending_.end()) {
            existing->second.generations.push_back(generation);
            if (priority == ThumbnailPriority::Visible &&
                existing->second.priority == ThumbnailPriority::Background &&
                !existing->second.running) {
                // A background entry that scrolled into view is promoted by
                // queueing it again; the stale background position is skipped
                // because the queue is filtered against `pending_`.
                existing->second.priority = ThumbnailPriority::Visible;
                visible_queue_.push_back(key);
            }
            return;
        }

        Pending entry;
        entry.source = std::move(source);
        entry.key = key;
        entry.generations.push_back(generation);
        entry.priority = priority;
        pending_.emplace(key, std::move(entry));

        if (priority == ThumbnailPriority::Visible) {
            visible_queue_.push_back(std::move(key));
        } else {
            background_queue_.push_back(std::move(key));
        }
        enforce_queue_bound();
    }
    work_available_.notify_one();
}

void ThumbnailService::release(const ThumbnailKey& key, std::uint64_t generation) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto entry = pending_.find(key);
    if (entry == pending_.end()) {
        return;
    }

    auto& generations = entry->second.generations;
    const auto found = std::ranges::find(generations, generation);
    if (found != generations.end()) {
        generations.erase(found);
    }
    if (generations.empty() && !entry->second.running) {
        pending_.erase(entry);
        drop_queued(key);
    }
}

void ThumbnailService::cancel_before(std::uint64_t generation) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        cancelled_before_ = std::max(cancelled_before_, generation);

        for (auto entry = pending_.begin(); entry != pending_.end();) {
            auto& generations = entry->second.generations;
            std::erase_if(generations,
                          [this](std::uint64_t filed) { return filed < cancelled_before_; });
            if (generations.empty() && !entry->second.running) {
                const ThumbnailKey key = entry->first;
                entry = pending_.erase(entry);
                drop_queued(key);
            } else {
                ++entry;
            }
        }
    }
    idle_.notify_all();
}

void ThumbnailService::invalidate(const ThumbnailKey& key) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto cached = cache_.find(key);
    if (cached != cache_.end()) {
        cached_bytes_ -= cached->second.image->byte_cost();
        cache_order_.erase(cached->second.position);
        cache_.erase(cached);
    }
    if (failures_.erase(key) > 0) {
        std::erase(failure_order_, key);
    }
}

void ThumbnailService::invalidate_source(const fs::path& source) {
    const std::string uri = file_uri(source);
    const std::lock_guard<std::mutex> lock(mutex_);

    for (auto entry = cache_.begin(); entry != cache_.end();) {
        if (entry->first.uri == uri) {
            cached_bytes_ -= entry->second.image->byte_cost();
            cache_order_.erase(entry->second.position);
            entry = cache_.erase(entry);
        } else {
            ++entry;
        }
    }

    std::erase_if(failures_, [&uri](const auto& entry) { return entry.first.uri == uri; });
    std::erase_if(failure_order_, [&uri](const ThumbnailKey& key) { return key.uri == uri; });
}

void ThumbnailService::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] {
        return running_ == 0 && visible_queue_.empty() && background_queue_.empty();
    });
}

std::size_t ThumbnailService::cached_bytes() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return cached_bytes_;
}

void ThumbnailService::drop_queued(const ThumbnailKey& key) {
    std::erase(visible_queue_, key);
    std::erase(background_queue_, key);
}

void ThumbnailService::enforce_queue_bound() {
    while (pending_.size() > options_.queue_bound && !background_queue_.empty()) {
        const ThumbnailKey oldest = background_queue_.front();
        background_queue_.pop_front();
        const auto entry = pending_.find(oldest);
        if (entry != pending_.end() && !entry->second.running) {
            pending_.erase(entry);
        }
    }
}

std::optional<ThumbnailService::Pending>
ThumbnailService::take_next(std::unique_lock<std::mutex>& lock) {
    while (!shutdown_) {
        std::deque<ThumbnailKey>* queue = nullptr;
        if (!visible_queue_.empty()) {
            queue = &visible_queue_;
        } else if (!background_queue_.empty()) {
            queue = &background_queue_;
        }

        if (queue == nullptr) {
            idle_.notify_all();
            work_available_.wait(lock);
            continue;
        }

        const ThumbnailKey key = queue->front();
        queue->pop_front();

        const auto entry = pending_.find(key);
        if (entry == pending_.end() || entry->second.running) {
            // Released, cancelled, or already claimed through the other queue.
            continue;
        }

        std::erase_if(entry->second.generations,
                      [this](std::uint64_t filed) { return filed < cancelled_before_; });
        if (entry->second.generations.empty()) {
            pending_.erase(entry);
            continue;
        }

        entry->second.running = true;
        ++running_;
        return entry->second;
    }
    return std::nullopt;
}

std::vector<ThumbnailResult> ThumbnailService::finish(const Pending& claimed,
                                                      std::shared_ptr<const ThumbnailImage> image,
                                                      std::error_code error, bool from_cache) {
    std::vector<ThumbnailResult> results;

    const std::lock_guard<std::mutex> lock(mutex_);
    --running_;

    const auto entry = pending_.find(claimed.key);
    std::vector<std::uint64_t> generations =
        entry != pending_.end() ? entry->second.generations : claimed.generations;
    if (entry != pending_.end()) {
        pending_.erase(entry);
    }

    if (!error && image) {
        remember(claimed.key, image);
    } else if (error) {
        remember_failure(claimed.key, error);
    }

    results.reserve(generations.size());
    for (const std::uint64_t generation : generations) {
        if (generation < cancelled_before_) {
            continue;
        }
        results.push_back(ThumbnailResult{.key = claimed.key,
                                          .source = claimed.source,
                                          .image = image,
                                          .error = error,
                                          .generation = generation,
                                          .from_cache = from_cache});
    }
    return results;
}

void ThumbnailService::deliver(std::vector<ThumbnailResult> results) {
    for (ThumbnailResult& result : results) {
        if (!deliver_results_.load(std::memory_order_acquire)) {
            return;
        }
        handler_(std::move(result));
    }
}

void ThumbnailService::remember(const ThumbnailKey& key,
                                const std::shared_ptr<const ThumbnailImage>& image) {
    const auto existing = cache_.find(key);
    if (existing != cache_.end()) {
        cached_bytes_ -= existing->second.image->byte_cost();
        cache_order_.erase(existing->second.position);
        cache_.erase(existing);
    }

    cache_order_.push_front(key);
    cache_.emplace(key, CacheEntry{.image = image, .position = cache_order_.begin()});
    cached_bytes_ += image->byte_cost();

    while (cached_bytes_ > options_.memory_budget_bytes && !cache_order_.empty()) {
        const ThumbnailKey oldest = cache_order_.back();
        const auto evicted = cache_.find(oldest);
        if (evicted != cache_.end()) {
            cached_bytes_ -= evicted->second.image->byte_cost();
            cache_.erase(evicted);
        }
        cache_order_.pop_back();
    }
}

void ThumbnailService::remember_failure(const ThumbnailKey& key, std::error_code error) {
    if (options_.failure_memory == 0) {
        return;
    }
    if (failures_.emplace(key, error).second) {
        failure_order_.push_back(key);
    }
    while (failure_order_.size() > options_.failure_memory) {
        failures_.erase(failure_order_.front());
        failure_order_.pop_front();
    }
}

std::shared_ptr<const ThumbnailImage> ThumbnailService::recall(const ThumbnailKey& key) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto cached = cache_.find(key);
    if (cached == cache_.end()) {
        return nullptr;
    }
    cache_order_.splice(cache_order_.begin(), cache_order_, cached->second.position);
    cached->second.position = cache_order_.begin();
    return cached->second.image;
}

void ThumbnailService::run() {
    while (true) {
        std::optional<Pending> claimed;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            claimed = take_next(lock);
        }
        if (!claimed.has_value()) {
            idle_.notify_all();
            return;
        }

        if (const std::shared_ptr<const ThumbnailImage> cached = recall(claimed->key)) {
            deliver(finish(*claimed, cached, {}, true));
            idle_.notify_all();
            continue;
        }

        // A source that already refused to decode is answered from memory. The
        // lookup and the delivery are separate steps so nothing is handed to
        // the handler while a lock is held.
        std::error_code remembered;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto refused = failures_.find(claimed->key);
            if (refused != failures_.end()) {
                remembered = refused->second;
            }
        }
        if (remembered) {
            deliver(finish(*claimed, nullptr, remembered, true));
            idle_.notify_all();
            continue;
        }

        if (thumbnail_is_excluded(claimed->source)) {
            deliver(
                finish(*claimed, nullptr, make_error(std::errc::operation_not_supported), false));
            idle_.notify_all();
            continue;
        }

        std::error_code error;
        std::optional<StoredThumbnail> stored = store_.load(claimed->key, error);
        if (!error && stored.has_value() && thumbnail_matches(*stored, claimed->key)) {
            auto image = std::make_shared<const ThumbnailImage>(std::move(stored->image));
            deliver(finish(*claimed, std::move(image), {}, true));
            idle_.notify_all();
            continue;
        }

        error.clear();
        ThumbnailImage produced = producer_.produce(claimed->source, claimed->key.edge, error);
        if (error) {
            deliver(finish(*claimed, nullptr, error, false));
            idle_.notify_all();
            continue;
        }

        std::error_code save_error;
        store_.save(claimed->key, produced, save_error);

        auto image = std::make_shared<const ThumbnailImage>(std::move(produced));
        deliver(finish(*claimed, std::move(image), {}, false));
        idle_.notify_all();
    }
}

} // namespace odysea::core

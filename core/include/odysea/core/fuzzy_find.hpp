// OdySea core: cancellable tree indexing and fuzzy path ranking.
//
// Toolkit-agnostic. The filesystem is walked once to build a reusable corpus;
// changing the query only ranks that in-memory corpus. This separation is the
// performance contract: a keystroke never starts or repeats a tree walk.
#pragma once

#include "odysea/core/directory_model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace odysea::core {

/// One searchable entry below the indexed root.
struct FuzzyCandidate {
    std::string name;
    std::filesystem::path path;
    std::string relative_path;
    EntryKind kind = EntryKind::Other;
    bool target_is_directory = false;

    /// ASCII-folded forms built once during indexing and reused by every
    /// query. Bytes outside ASCII remain unchanged rather than being mangled
    /// by a locale-dependent bytewise conversion.
    std::string folded_name;
    std::string folded_relative_path;

    FuzzyCandidate() = default;
    FuzzyCandidate(std::string candidate_name, std::filesystem::path candidate_path,
                   std::string candidate_relative_path, EntryKind candidate_kind,
                   bool candidate_target_is_directory = false);

    [[nodiscard]] bool is_directory() const noexcept {
        return kind == EntryKind::Directory || target_is_directory;
    }
};

/// A ranked reference into the immutable corpus supplied to the matcher.
struct FuzzyMatch {
    std::size_t candidate_index = 0;
    std::uint32_t score = 0;
};

/// Deterministic work counters for performance gates.
struct FuzzyRankWork {
    std::uint64_t candidates_examined = 0;
    std::uint64_t character_comparisons = 0;
};

/// One completed rank request.
struct FuzzyRankSummary {
    std::shared_ptr<const std::vector<FuzzyCandidate>> corpus;
    std::string query;
    std::vector<FuzzyMatch> matches;
    FuzzyRankWork work;
    std::uint64_t token = 0;
    bool cancelled = false;
};

/// Rank a corpus synchronously. The worker wrapper below is the ordinary UI
/// path; this function is exposed for deterministic headless tests and other
/// callers that already own an off-thread execution context.
[[nodiscard]] FuzzyRankSummary
rank_fuzzy_candidates(std::shared_ptr<const std::vector<FuzzyCandidate>> corpus, std::string query,
                      std::size_t result_limit, const std::function<bool()>& cancelled = {});

/// Newest-request-wins off-thread ranker. Superseded queries are cancelled,
/// and each request receives exactly one completion callback.
class FuzzyRanker {
  public:
    using CompletionHandler = std::function<void(FuzzyRankSummary summary)>;

    struct Request {
        std::shared_ptr<const std::vector<FuzzyCandidate>> corpus;
        std::string query;
        std::size_t result_limit = 100;
        CompletionHandler on_complete;
    };

    FuzzyRanker();
    FuzzyRanker(const FuzzyRanker&) = delete;
    FuzzyRanker& operator=(const FuzzyRanker&) = delete;
    FuzzyRanker(FuzzyRanker&&) = delete;
    FuzzyRanker& operator=(FuzzyRanker&&) = delete;
    ~FuzzyRanker();

    std::uint64_t start(Request request);
    void cancel();
    void wait_idle();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

struct FuzzyFindOptions {
    bool show_hidden = false;
    /// Stay on the root filesystem by default. A tree search must not wander
    /// into removable, network, or pseudo filesystems without an explicit
    /// request.
    bool cross_filesystem_boundaries = false;
    /// Entries examined between progress callbacks. Zero is treated as one.
    std::size_t progress_interval = 512;
};

struct FuzzyFindProgress {
    std::filesystem::path root;
    std::uint64_t token = 0;
    std::uint64_t entries_visited = 0;
    std::uint64_t directories_visited = 0;
    std::uint64_t unreadable_directories = 0;
};

struct FuzzyFindSummary {
    std::filesystem::path root;
    std::uint64_t token = 0;
    std::vector<FuzzyCandidate> candidates;
    std::uint64_t entries_visited = 0;
    std::uint64_t directories_visited = 0;
    std::uint64_t unreadable_directories = 0;
    std::error_code error;
    bool cancelled = false;
};

/// Single-worker recursive indexer. Directory symbolic links are searchable
/// results but are never followed, preventing cycles and escaping the chosen
/// root through a link. Cancellation is polled for every entry and directory.
class FuzzyFindScanner {
  public:
    using ProgressHandler = std::function<void(FuzzyFindProgress progress)>;
    using CompletionHandler = std::function<void(FuzzyFindSummary summary)>;

    struct Request {
        std::filesystem::path root;
        FuzzyFindOptions options;
        ProgressHandler on_progress;
        CompletionHandler on_complete;
    };

    FuzzyFindScanner();
    FuzzyFindScanner(const FuzzyFindScanner&) = delete;
    FuzzyFindScanner& operator=(const FuzzyFindScanner&) = delete;
    FuzzyFindScanner(FuzzyFindScanner&&) = delete;
    FuzzyFindScanner& operator=(FuzzyFindScanner&&) = delete;
    ~FuzzyFindScanner();

    std::uint64_t start(Request request);
    void cancel();
    void wait_idle();

  private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace odysea::core

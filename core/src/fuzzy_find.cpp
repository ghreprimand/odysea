#include "odysea/core/fuzzy_find.hpp"

#include "entry_metadata.hpp"
#include "scan_worker.hpp"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

namespace odysea::core {
namespace {

using RankWorker = detail::ScanWorker<FuzzyRanker::Request>;
using FindWorker = detail::ScanWorker<FuzzyFindScanner::Request>;

std::string fold_ascii(std::string_view text) {
    std::string folded;
    folded.reserve(text.size());
    for (const char character : text) {
        const auto value = static_cast<unsigned char>(character);
        if (value >= 'A' && value <= 'Z') {
            folded.push_back(static_cast<char>(value - 'A' + 'a'));
        } else {
            folded.push_back(static_cast<char>(value));
        }
    }
    return folded;
}

struct QueryPattern {
    std::string text;
    std::vector<std::size_t> prefix;
};

QueryPattern make_pattern(std::string query) {
    QueryPattern pattern;
    pattern.text = std::move(query);
    pattern.prefix.resize(pattern.text.size());
    for (std::size_t offset = 1, length = 0; offset < pattern.text.size();) {
        if (pattern.text.at(offset) == pattern.text.at(length)) {
            pattern.prefix.at(offset++) = ++length;
        } else if (length > 0) {
            length = pattern.prefix.at(length - 1);
        } else {
            pattern.prefix.at(offset++) = 0;
        }
    }
    return pattern;
}

// Subsequence and contiguous matching intentionally share one pass so the work
// counter measures the exact hot path rather than two approximate scans.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::optional<std::uint32_t> score_text(const QueryPattern& query, std::string_view text,
                                        std::uint64_t& comparisons) {
    if (query.text.empty()) {
        return 0;
    }

    std::size_t query_offset = 0;
    std::size_t contiguous_offset = 0;
    std::size_t contiguous_position = std::string_view::npos;
    std::size_t first = std::string_view::npos;
    std::size_t previous = 0;
    std::uint32_t gaps = 0;
    std::uint32_t boundary_bonus = 0;
    for (std::size_t offset = 0; offset < text.size(); ++offset) {
        if (query_offset < query.text.size()) {
            ++comparisons;
            if (text.at(offset) == query.text.at(query_offset)) {
                if (first == std::string_view::npos) {
                    first = offset;
                } else {
                    gaps += static_cast<std::uint32_t>(offset - previous - 1);
                }
                if (offset == 0 || text.at(offset - 1) == '/' || text.at(offset - 1) == '_' ||
                    text.at(offset - 1) == '-' || text.at(offset - 1) == ' ') {
                    ++boundary_bonus;
                }
                previous = offset;
                ++query_offset;
            }
        }

        while (contiguous_offset > 0) {
            ++comparisons;
            if (text.at(offset) == query.text.at(contiguous_offset)) {
                break;
            }
            contiguous_offset = query.prefix.at(contiguous_offset - 1);
        }
        if (contiguous_offset == 0) {
            ++comparisons;
            if (text.at(offset) == query.text.front()) {
                contiguous_offset = 1;
            }
        } else {
            ++contiguous_offset;
        }
        if (contiguous_offset == query.text.size()) {
            if (contiguous_position == std::string_view::npos) {
                contiguous_position = offset + 1 - query.text.size();
            }
            contiguous_offset = query.prefix.at(contiguous_offset - 1);
        }
    }
    if (query_offset != query.text.size()) {
        return std::nullopt;
    }

    std::uint32_t score = 0;
    if (text.size() == query.text.size() && contiguous_position == 0) {
        score = 0;
    } else if (contiguous_position == 0) {
        score = 10 + static_cast<std::uint32_t>(text.size() - query.text.size());
    } else if (contiguous_position != std::string_view::npos) {
        score = 30 + static_cast<std::uint32_t>(contiguous_position * 2) +
                static_cast<std::uint32_t>(text.size() - query.text.size());
    } else {
        constexpr std::uint32_t fuzzy_base = 100;
        constexpr std::uint32_t gap_cost = 4;
        constexpr std::uint32_t boundary_reward = 3;
        const std::uint32_t raw = fuzzy_base + static_cast<std::uint32_t>(first) +
                                  (gaps * gap_cost) +
                                  static_cast<std::uint32_t>(text.size() - query.text.size());
        score =
            raw > (boundary_bonus * boundary_reward) ? raw - (boundary_bonus * boundary_reward) : 0;
    }
    return score;
}

std::optional<std::uint32_t> score_candidate(const QueryPattern& query,
                                             const FuzzyCandidate& candidate,
                                             std::uint64_t& comparisons) {
    if (const auto name = score_text(query, candidate.folded_name, comparisons)) {
        return name;
    }
    if (const auto path = score_text(query, candidate.folded_relative_path, comparisons)) {
        return *path + 250;
    }
    return std::nullopt;
}

bool match_orders_before(const FuzzyMatch& left, const FuzzyMatch& right,
                         const std::vector<FuzzyCandidate>& corpus) {
    const FuzzyCandidate& left_candidate = corpus.at(left.candidate_index);
    const FuzzyCandidate& right_candidate = corpus.at(right.candidate_index);
    return std::tie(left.score, left_candidate.folded_name, left_candidate.folded_relative_path) <
           std::tie(right.score, right_candidate.folded_name, right_candidate.folded_relative_path);
}

EntryKind kind_from_entry(const Entry& entry) {
    return entry.kind;
}

} // namespace

FuzzyCandidate::FuzzyCandidate(std::string candidate_name, std::filesystem::path candidate_path,
                               std::string candidate_relative_path, EntryKind candidate_kind,
                               bool candidate_target_is_directory)
    : name(std::move(candidate_name)), path(std::move(candidate_path)),
      relative_path(std::move(candidate_relative_path)), kind(candidate_kind),
      target_is_directory(candidate_target_is_directory), folded_name(fold_ascii(name)),
      folded_relative_path(fold_ascii(relative_path)) {}

FuzzyRankSummary rank_fuzzy_candidates(std::shared_ptr<const std::vector<FuzzyCandidate>> corpus,
                                       std::string query, std::size_t result_limit,
                                       const std::function<bool()>& cancelled) {
    FuzzyRankSummary summary;
    summary.corpus = std::move(corpus);
    summary.query = std::move(query);
    const QueryPattern pattern = make_pattern(fold_ascii(summary.query));
    if (!summary.corpus || pattern.text.empty() || result_limit == 0) {
        return summary;
    }

    summary.matches.reserve(std::min(result_limit * 4, summary.corpus->size()));
    for (std::size_t index = 0; index < summary.corpus->size(); ++index) {
        if (cancelled && cancelled()) {
            summary.cancelled = true;
            break;
        }
        ++summary.work.candidates_examined;
        std::uint64_t comparisons = 0;
        const auto scored = score_candidate(pattern, summary.corpus->at(index), comparisons);
        summary.work.character_comparisons += comparisons;
        if (!scored) {
            continue;
        }
        summary.matches.push_back(FuzzyMatch{.candidate_index = index, .score = *scored});
    }

    if (!summary.cancelled) {
        const std::size_t kept = std::min(result_limit, summary.matches.size());
        const auto comparator = [&summary](const FuzzyMatch& left, const FuzzyMatch& right) {
            return match_orders_before(left, right, *summary.corpus);
        };
        if (kept < summary.matches.size()) {
            std::partial_sort(summary.matches.begin(),
                              summary.matches.begin() + static_cast<std::ptrdiff_t>(kept),
                              summary.matches.end(), comparator);
            summary.matches.resize(kept);
        } else {
            std::ranges::sort(summary.matches, comparator);
        }
    }
    return summary;
}

struct FuzzyRanker::State {
    RankWorker worker;

    State()
        : worker(RankWorker::Execute{[this](Request request, std::uint64_t token) {
                     FuzzyRankSummary summary = rank_fuzzy_candidates(
                         std::move(request.corpus), std::move(request.query), request.result_limit,
                         [this, token] { return worker.cancelled(token); });
                     summary.token = token;
                     summary.cancelled = summary.cancelled || worker.cancelled(token);
                     if (request.on_complete && worker.delivering()) {
                         request.on_complete(std::move(summary));
                     }
                 }},
                 RankWorker::Abandon{[](const Request& request, std::uint64_t token) {
                     if (request.on_complete) {
                         FuzzyRankSummary summary;
                         summary.corpus = request.corpus;
                         summary.query = request.query;
                         summary.token = token;
                         summary.cancelled = true;
                         request.on_complete(std::move(summary));
                     }
                 }}) {}
};

FuzzyRanker::FuzzyRanker() : state_(std::make_unique<State>()) {}
FuzzyRanker::~FuzzyRanker() = default;
std::uint64_t FuzzyRanker::start(Request request) {
    return state_->worker.start(std::move(request));
}
void FuzzyRanker::cancel() {
    state_->worker.cancel();
}
void FuzzyRanker::wait_idle() {
    state_->worker.wait_idle();
}

struct FuzzyFindScanner::State {
    FindWorker worker;

    State()
        : worker(FindWorker::Execute{[this](Request request, std::uint64_t token) {
                     execute(std::move(request), token);
                 }},
                 FindWorker::Abandon{[](const Request& request, std::uint64_t token) {
                     if (request.on_complete) {
                         FuzzyFindSummary summary;
                         summary.root = request.root;
                         summary.token = token;
                         summary.cancelled = true;
                         request.on_complete(std::move(summary));
                     }
                 }}) {}

    // The recursive walk keeps cancellation, progress, hidden-subtree, device,
    // identity, and error policies adjacent so each entry has one decision path.
    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void execute(Request request, std::uint64_t token) const {
        FuzzyFindSummary summary;
        summary.root = request.root;
        summary.token = token;
        const std::size_t interval =
            request.options.progress_interval == 0 ? 1 : request.options.progress_interval;
        std::size_t since_progress = 0;
        const auto report = [&] {
            since_progress = 0;
            if (request.on_progress && worker.delivering()) {
                request.on_progress(
                    FuzzyFindProgress{.root = summary.root,
                                      .token = token,
                                      .entries_visited = summary.entries_visited,
                                      .directories_visited = summary.directories_visited,
                                      .unreadable_directories = summary.unreadable_directories});
            }
        };
        const auto finish = [&] {
            summary.cancelled = worker.cancelled(token);
            if (request.on_complete && worker.delivering()) {
                request.on_complete(std::move(summary));
            }
        };

        const detail::EntryMetadata root_metadata = detail::read_entry_metadata(summary.root);
        if (!root_metadata.known) {
            summary.error = std::error_code(root_metadata.error_number, std::generic_category());
            finish();
            return;
        }
        if (!detail::mode_is_directory(root_metadata.mode)) {
            summary.error = std::make_error_code(std::errc::not_a_directory);
            finish();
            return;
        }

        std::vector<std::filesystem::path> pending{summary.root};
        std::set<std::pair<std::uint64_t, std::uint64_t>> visited;
        visited.emplace(root_metadata.identity.device, root_metadata.identity.inode);
        while (!pending.empty() && !worker.cancelled(token)) {
            const std::filesystem::path directory = std::move(pending.back());
            pending.pop_back();
            ++summary.directories_visited;

            std::error_code open_error;
            std::filesystem::directory_iterator iterator(directory, open_error);
            if (open_error) {
                if (directory == summary.root) {
                    summary.error = open_error;
                } else {
                    ++summary.unreadable_directories;
                }
                continue;
            }

            const std::filesystem::directory_iterator end;
            std::error_code step_error;
            while (iterator != end) {
                if (worker.cancelled(token)) {
                    break;
                }
                Entry entry = make_entry(*iterator);
                ++summary.entries_visited;
                ++since_progress;

                const bool hidden = is_hidden_name(entry.name);
                if (!hidden || request.options.show_hidden) {
                    const std::filesystem::path relative =
                        entry.path.lexically_relative(summary.root);
                    summary.candidates.emplace_back(
                        entry.name, entry.path, relative.generic_string(), kind_from_entry(entry),
                        entry.target_is_directory);

                    if (entry.kind == EntryKind::Directory && entry.identity.known() &&
                        (request.options.cross_filesystem_boundaries ||
                         entry.identity.device == root_metadata.identity.device) &&
                        visited.emplace(entry.identity.device, entry.identity.inode).second) {
                        pending.push_back(entry.path);
                    }
                }

                if (since_progress >= interval) {
                    report();
                }
                iterator.increment(step_error);
                if (step_error) {
                    ++summary.unreadable_directories;
                    break;
                }
            }
        }
        if (since_progress > 0) {
            report();
        }
        finish();
    }
};

FuzzyFindScanner::FuzzyFindScanner() : state_(std::make_unique<State>()) {}
FuzzyFindScanner::~FuzzyFindScanner() = default;
std::uint64_t FuzzyFindScanner::start(Request request) {
    return state_->worker.start(std::move(request));
}
void FuzzyFindScanner::cancel() {
    state_->worker.cancel();
}
void FuzzyFindScanner::wait_idle() {
    state_->worker.wait_idle();
}

} // namespace odysea::core

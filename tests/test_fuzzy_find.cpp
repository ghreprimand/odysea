// Headless tests for one-walk fuzzy tree search, ranking, and cancellation.

#include "odysea/core/fuzzy_find.hpp"

#include "test_support.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using odysea::core::EntryKind;
using odysea::core::FuzzyCandidate;
using odysea::core::FuzzyFindScanner;
using odysea::core::FuzzyFindSummary;
using odysea::core::FuzzyRankSummary;
using odysea::test::check;

namespace {

class FindRecorder {
  public:
    void complete(FuzzyFindSummary summary) {
        {
            const std::scoped_lock lock(mutex_);
            summary_ = std::move(summary);
            complete_ = true;
        }
        condition_.notify_all();
    }

    bool wait() {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(10), [this] { return complete_; });
    }

    FuzzyFindSummary take() {
        const std::scoped_lock lock(mutex_);
        return std::move(summary_);
    }

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    FuzzyFindSummary summary_;
    bool complete_ = false;
};

std::shared_ptr<const std::vector<FuzzyCandidate>>
corpus_of(std::initializer_list<FuzzyCandidate> entries) {
    return std::make_shared<const std::vector<FuzzyCandidate>>(entries);
}

void write_file(const fs::path& path) {
    std::ofstream stream(path);
    stream << "x";
}

void test_rank_prefers_name_quality_then_path() {
    const auto corpus = corpus_of({
        FuzzyCandidate{"readme.txt", "/tree/readme.txt", "readme.txt", EntryKind::File},
        FuzzyCandidate{"reader.txt", "/tree/docs/reader.txt", "docs/reader.txt", EntryKind::File},
        FuzzyCandidate{"notes.txt", "/tree/readme/notes.txt", "readme/notes.txt", EntryKind::File},
        FuzzyCandidate{"renderer.cpp", "/tree/src/renderer.cpp", "src/renderer.cpp",
                       EntryKind::File},
    });
    const FuzzyRankSummary exact = odysea::core::rank_fuzzy_candidates(corpus, "readme.txt", 20);
    check(exact.matches.size() == 2, "the exact name and a relative-path match are found");
    check(corpus->at(exact.matches.front().candidate_index).name == "readme.txt",
          "an exact name outranks a match in the relative path");

    const FuzzyRankSummary fuzzy = odysea::core::rank_fuzzy_candidates(corpus, "rndr", 20);
    check(fuzzy.matches.size() == 1 &&
              corpus->at(fuzzy.matches.front().candidate_index).name == "renderer.cpp",
          "ordered non-contiguous characters produce a fuzzy match");

    const FuzzyRankSummary case_folded = odysea::core::rank_fuzzy_candidates(corpus, "README", 20);
    check(!case_folded.matches.empty() &&
              corpus->at(case_folded.matches.front().candidate_index).name == "readme.txt",
          "ASCII matching is case-insensitive");
}

void test_result_limit_is_total_and_stable() {
    auto mutable_corpus = std::make_shared<std::vector<FuzzyCandidate>>();
    for (int index = 20; index >= 0; --index) {
        const std::string name = "item-" + std::to_string(index);
        mutable_corpus->emplace_back(name, fs::path("/tree") / name, name, EntryKind::File);
    }
    const auto corpus = std::const_pointer_cast<const std::vector<FuzzyCandidate>>(mutable_corpus);
    const FuzzyRankSummary summary = odysea::core::rank_fuzzy_candidates(corpus, "item", 5);
    check(summary.matches.size() == 5, "the result ceiling is enforced");
    for (std::size_t offset = 1; offset < summary.matches.size(); ++offset) {
        const auto& previous = corpus->at(summary.matches.at(offset - 1).candidate_index);
        const auto& current = corpus->at(summary.matches.at(offset).candidate_index);
        check(previous.folded_name < current.folded_name,
              "ties are ordered deterministically by folded name");
    }
}

void test_scan_indexes_tree_once_without_following_links() {
    const odysea::test::TemporaryTree tree("fuzzy_tree");
    fs::create_directories(tree.root() / "docs" / "deep");
    fs::create_directories(tree.root() / ".hidden" / "nested");
    write_file(tree.root() / "root.txt");
    write_file(tree.root() / "docs" / "guide.txt");
    write_file(tree.root() / "docs" / "deep" / "manual.txt");
    write_file(tree.root() / ".hidden" / "nested" / "secret.txt");
    fs::create_directory_symlink(tree.root() / "docs", tree.root() / "docs-link");

    FuzzyFindScanner scanner;
    FindRecorder recorder;
    scanner.start({.root = tree.root(),
                   .options = {},
                   .on_progress = {},
                   .on_complete = [&recorder](FuzzyFindSummary summary) {
                       recorder.complete(std::move(summary));
                   }});
    check(recorder.wait(), "the tree walk completes");
    FuzzyFindSummary summary = recorder.take();
    check(!summary.cancelled && !summary.error, "an ordinary walk succeeds");
    check(summary.directories_visited == 3,
          "only the visible root, docs, and deep directories are entered");
    check(summary.entries_visited == 7,
          "hidden subtrees are observed once at their parent and not descended into");
    check(summary.candidates.size() == 6,
          "visible directories, files, and the directory link are searchable");
    check(std::ranges::count_if(summary.candidates,
                                [](const FuzzyCandidate& candidate) {
                                    return candidate.relative_path == "docs/deep/manual.txt";
                                }) == 1,
          "a nested file carries a root-relative search path");
    check(std::ranges::count_if(
              summary.candidates,
              [](const FuzzyCandidate& candidate) { return candidate.name == "guide.txt"; }) == 1,
          "a directory link is not followed into duplicate results");
}

void test_hidden_option_indexes_hidden_subtrees() {
    const odysea::test::TemporaryTree tree("fuzzy_hidden");
    fs::create_directories(tree.root() / ".private");
    write_file(tree.root() / ".private" / "note.txt");

    FuzzyFindScanner scanner;
    FindRecorder recorder;
    scanner.start({.root = tree.root(),
                   .options = {.show_hidden = true},
                   .on_progress = {},
                   .on_complete = [&recorder](FuzzyFindSummary summary) {
                       recorder.complete(std::move(summary));
                   }});
    check(recorder.wait(), "the hidden-inclusive walk completes");
    const FuzzyFindSummary summary = recorder.take();
    check(summary.candidates.size() == 2 && summary.directories_visited == 2,
          "the explicit hidden option includes and descends into hidden directories");
}

void test_cancellation_stops_inside_a_large_tree() {
    const odysea::test::TemporaryTree tree("fuzzy_cancel");
    constexpr int directory_count = 80;
    constexpr int files_per_directory = 40;
    for (int directory = 0; directory < directory_count; ++directory) {
        const fs::path path = tree.root() / ("branch-" + std::to_string(directory));
        fs::create_directory(path);
        for (int file = 0; file < files_per_directory; ++file) {
            write_file(path / ("file-" + std::to_string(file)));
        }
    }

    FuzzyFindScanner scanner;
    FindRecorder recorder;
    scanner.start(
        {.root = tree.root(),
         .options = {.progress_interval = 1},
         .on_progress =
             [&scanner](const auto& progress) {
                 if (progress.entries_visited >= 30) {
                     scanner.cancel();
                 }
             },
         .on_complete =
             [&recorder](FuzzyFindSummary summary) { recorder.complete(std::move(summary)); }});
    check(recorder.wait(), "a cancelled walk still completes");
    const FuzzyFindSummary summary = recorder.take();
    const std::uint64_t total = static_cast<std::uint64_t>(directory_count) *
                                static_cast<std::uint64_t>(files_per_directory + 1);
    check(summary.cancelled, "the completion records cancellation");
    check(summary.entries_visited >= 30,
          "the cancellation counter proves the walk performed real work");
    check(summary.entries_visited < total / 4,
          "per-entry cancellation leaves most of the tree unread");
}

void test_large_corpus_cost_is_bounded_per_keystroke() {
    constexpr std::size_t candidate_count = 50000;
    auto mutable_corpus = std::make_shared<std::vector<FuzzyCandidate>>();
    mutable_corpus->reserve(candidate_count);
    for (std::size_t index = 0; index < candidate_count; ++index) {
        const std::string relative = "branch-" + std::to_string(index % 200) + "/component-file-" +
                                     std::to_string(index) + ".cpp";
        const std::string name = "component-file-" + std::to_string(index) + ".cpp";
        mutable_corpus->emplace_back(name, fs::path("/tree") / relative, relative, EntryKind::File);
    }
    const auto corpus = std::const_pointer_cast<const std::vector<FuzzyCandidate>>(mutable_corpus);
    const std::vector<std::string> keystrokes{"c",     "co",     "com",     "comp",
                                              "compo", "compon", "compone", "component"};
    std::chrono::nanoseconds total{};
    std::chrono::nanoseconds maximum{};
    std::uint64_t comparison_floor = 0;
    for (const std::string& query : keystrokes) {
        const auto started = std::chrono::steady_clock::now();
        const FuzzyRankSummary summary = odysea::core::rank_fuzzy_candidates(corpus, query, 100);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        total += elapsed;
        maximum = std::max(maximum, elapsed);
        check(summary.work.candidates_examined == candidate_count,
              "every completed query accounts for the whole corpus");
        check(summary.work.character_comparisons >= candidate_count,
              "the comparison counter cannot pass by stopping at zero");
        check(summary.work.character_comparisons <= candidate_count * 80,
              "ranking performs bounded character work per candidate");
        comparison_floor += summary.work.character_comparisons;
    }
    const auto average_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(total / keystrokes.size()).count();
    const auto maximum_ms = std::chrono::duration_cast<std::chrono::milliseconds>(maximum).count();
    std::cout << "fuzzy_find: 50000 candidates, 8 keystrokes, average " << average_ms << " ms, max "
              << maximum_ms << " ms, comparisons " << comparison_floor << '\n';
    check(maximum < std::chrono::seconds(1),
          "one keystroke ranks fifty thousand cached paths in under one second");
}

void test_rank_cancellation_stops_inside_the_corpus() {
    auto mutable_corpus = std::make_shared<std::vector<FuzzyCandidate>>();
    for (int index = 0; index < 1000; ++index) {
        const std::string name = "candidate-" + std::to_string(index);
        mutable_corpus->emplace_back(name, fs::path("/tree") / name, name, EntryKind::File);
    }
    const auto corpus = std::const_pointer_cast<const std::vector<FuzzyCandidate>>(mutable_corpus);
    std::size_t cancellationChecks = 0;
    const FuzzyRankSummary summary = odysea::core::rank_fuzzy_candidates(
        corpus, "candidate", 100, [&cancellationChecks] { return cancellationChecks++ >= 25; });
    check(summary.cancelled, "ranking records cancellation");
    check(summary.work.candidates_examined >= 20,
          "the cancelled rank request proves it performed real work");
    check(summary.work.candidates_examined < corpus->size() / 4,
          "per-candidate cancellation leaves most of the corpus unexamined");
}

} // namespace

int main() {
    test_rank_prefers_name_quality_then_path();
    test_result_limit_is_total_and_stable();
    test_scan_indexes_tree_once_without_following_links();
    test_hidden_option_indexes_hidden_subtrees();
    test_cancellation_stops_inside_a_large_tree();
    test_large_corpus_cost_is_bounded_per_keystroke();
    test_rank_cancellation_stops_inside_the_corpus();
    return odysea::test::report("fuzzy_find");
}

// Minimal shared harness for the headless core tests.
//
// Deliberately dependency-free: a few assertion helpers and a temporary-tree
// fixture rather than a test framework, so the core stays verifiable anywhere
// without extra packages.
#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace odysea::test {

inline int failures = 0;

inline void check(bool condition, std::string_view what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(what.size()), what.data());
        ++failures;
    }
}

inline int report(std::string_view suite) {
    if (failures == 0) {
        std::printf("%.*s: all checks passed\n", static_cast<int>(suite.size()), suite.data());
        return 0;
    }
    std::fprintf(stderr, "%.*s: %d check(s) failed\n", static_cast<int>(suite.size()), suite.data(),
                 failures);
    return 1;
}

/// A temporary directory that removes itself. Synthetic paths only: fixtures
/// never touch real user data.
class TemporaryTree {
  public:
    explicit TemporaryTree(std::string_view label) {
        root_ = std::filesystem::temp_directory_path() /
                ("odysea_" + std::string(label) + "_" + std::to_string(::getpid()));
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_, ec);
    }

    TemporaryTree(const TemporaryTree&) = delete;
    TemporaryTree& operator=(const TemporaryTree&) = delete;
    TemporaryTree(TemporaryTree&&) = delete;
    TemporaryTree& operator=(TemporaryTree&&) = delete;

    ~TemporaryTree() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

    std::filesystem::path directory(std::string_view relative) const {
        const std::filesystem::path path = root_ / relative;
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return path;
    }

    std::filesystem::path file(std::string_view relative, std::string_view contents = "x") const {
        const std::filesystem::path path = root_ / relative;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << contents;
        return path;
    }

  private:
    std::filesystem::path root_;
};

inline std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

} // namespace odysea::test

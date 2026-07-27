// Headless tests for odysea::core::read_directory.
//
// Deliberately dependency-free: a tiny assertion harness rather than a test
// framework, so the core can be verified anywhere without extra packages. Under
// the "asan" preset these exercises run with AddressSanitizer active.
#include "odysea/core/directory_model.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// Create an isolated temporary tree the tests fully control.
fs::path make_fixture() {
    const fs::path root =
        fs::temp_directory_path() / fs::path("odysea_test_" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "subdir");
    std::ofstream(root / "beta.txt") << "b";
    std::ofstream(root / "Alpha.txt") << "aa";
    std::ofstream(root / ".hidden") << "h";
    return root;
}

} // namespace

int main() {
    const fs::path root = make_fixture();
    const odysea::core::ListOptions visible{.show_hidden = false};

    std::error_code ec;
    auto entries = odysea::core::read_directory(root, visible, ec);
    check(!ec, "read_directory should succeed on a valid directory");

    // Hidden entries filtered by default.
    const bool has_hidden =
        std::ranges::any_of(entries, [](const auto& e) { return e.name == ".hidden"; });
    check(!has_hidden, "dotfiles should be filtered when show_hidden is false");
    check(entries.size() == 3, "expected subdir + 2 visible files");

    // Directories sort before files.
    check(!entries.empty() && entries.front().is_directory(),
          "directories should sort before files");

    // Case-insensitive name ordering among files (Alpha before beta).
    auto alpha = std::ranges::find_if(entries, [](const auto& e) { return e.name == "Alpha.txt"; });
    auto beta = std::ranges::find_if(entries, [](const auto& e) { return e.name == "beta.txt"; });
    check(alpha != entries.end() && beta != entries.end() && alpha < beta,
          "file names should order case-insensitively");

    // show_hidden reveals the dotfile.
    auto all = odysea::core::read_directory(root, {.show_hidden = true}, ec);
    check(all.size() == 4, "show_hidden should include the dotfile");

    // Error path: a nonexistent directory reports an error, returns empty.
    auto missing = odysea::core::read_directory(root / "does_not_exist", visible, ec);
    check(static_cast<bool>(ec), "missing directory should set the error_code");
    check(missing.empty(), "missing directory should yield no entries");

    fs::remove_all(root);

    if (failures == 0) {
        std::puts("all core tests passed");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}

// Headless tests for odysea::core::read_directory.
//
// Deliberately dependency-free: a tiny assertion harness rather than a test
// framework, so the core can be verified anywhere without extra packages. Under
// the "asan" preset these exercises run with AddressSanitizer active.
#include "odysea/core/directory_model.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
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

// Every listing failure must surface through the error_code out-parameter. A
// throw escaping read_directory would violate its contract, so each hostile
// input is exercised inside a guard that reports an escape as a failure.
bool lists_without_throwing(const fs::path& path, std::error_code& error) {
    try {
        static_cast<void>(odysea::core::read_directory(path, {.show_hidden = true}, error));
        return true;
    } catch (...) {
        return false;
    }
}

void test_hostile_inputs_report_instead_of_throwing(const fs::path& root) {
    std::error_code ec;

    check(lists_without_throwing(root / "does_not_exist", ec),
          "a missing directory must not throw");
    check(ec == std::errc::no_such_file_or_directory, "a missing directory reports its cause");

    check(lists_without_throwing(root / "beta.txt", ec), "a plain file must not throw");
    check(ec == std::errc::not_a_directory, "a plain file reports that it is not a directory");

    std::error_code link_ec;
    fs::create_symlink(root / "loop", root / "loop", link_ec);
    if (!link_ec) {
        check(lists_without_throwing(root / "loop", ec), "a symlink loop must not throw");
        check(static_cast<bool>(ec), "a symlink loop reports an error");
        fs::remove(root / "loop", link_ec);
    }

    check(lists_without_throwing("", ec), "an empty path must not throw");
    check(static_cast<bool>(ec), "an empty path reports an error");
}

void test_a_successful_listing_clears_a_stale_error(const fs::path& root) {
    // Callers reuse one error_code across several listings; a success must not
    // leave the previous failure visible.
    std::error_code ec = std::make_error_code(std::errc::io_error);
    const auto entries = odysea::core::read_directory(root, {.show_hidden = false}, ec);
    check(!ec, "a successful listing clears a previously set error");
    check(!entries.empty(), "a successful listing still returns entries");
}

void test_listing_a_directory_being_modified_never_throws(const fs::path& root) {
    // Entries disappearing mid-iteration is ordinary on a live filesystem. The
    // listing must degrade to a partial result rather than throwing.
    const fs::path busy = root / "busy";
    fs::remove_all(busy);
    fs::create_directories(busy);
    constexpr int entry_count = 400;
    for (int index = 0; index < entry_count; ++index) {
        std::ofstream(busy / ("entry_" + std::to_string(index) + ".txt")) << "x";
    }

    std::atomic<bool> stop{false};
    std::thread mutator([&] {
        for (int index = 0; index < entry_count && !stop.load(); ++index) {
            std::error_code remove_ec;
            fs::remove(busy / ("entry_" + std::to_string(index) + ".txt"), remove_ec);
        }
    });

    std::error_code ec;
    const bool survived = lists_without_throwing(busy, ec);
    stop.store(true);
    mutator.join();

    check(survived, "listing a directory losing entries must not throw");

    // Whatever survived the race, the listing is still internally consistent.
    std::error_code final_ec;
    const auto entries = odysea::core::read_directory(busy, {.show_hidden = true}, final_ec);
    check(std::ranges::is_sorted(entries, odysea::core::entry_orders_before),
          "a listing is returned in presentation order");

    fs::remove_all(busy);
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
    check(alpha != entries.end() && alpha->device != 0 && alpha->inode != 0,
          "entries expose a stable filesystem identity");

    // show_hidden reveals the dotfile.
    auto all = odysea::core::read_directory(root, {.show_hidden = true}, ec);
    check(all.size() == 4, "show_hidden should include the dotfile");

    // Error path: a nonexistent directory reports an error, returns empty.
    auto missing = odysea::core::read_directory(root / "does_not_exist", visible, ec);
    check(static_cast<bool>(ec), "missing directory should set the error_code");
    check(missing.empty(), "missing directory should yield no entries");

    test_hostile_inputs_report_instead_of_throwing(root);
    test_a_successful_listing_clears_a_stale_error(root);
    test_listing_a_directory_being_modified_never_throws(root);

    fs::remove_all(root);

    if (failures == 0) {
        std::puts("all core tests passed");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
}

// Headless tests for odysea::core::EntryIdentity.
//
// Identity has two obligations and they pull in opposite directions. It must
// be DISTINCT, so that two different entries never compare equal and a
// consumer following an entry across a refresh cannot land on the wrong one.
// It must also be STABLE, so that an entry which merely moved or changed still
// compares equal to itself and a consumer does not lose track of it. A test
// suite that only covers one half would accept an identity that is unique but
// churns, or stable but ambiguous, and both break selection.
//
// Deliberately dependency-free, matching the rest of the core suite.
#include "odysea/core/directory_model.hpp"

#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

fs::path make_fixture() {
    const fs::path root =
        fs::temp_directory_path() / fs::path("odysea_identity_" + std::to_string(::getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

odysea::core::Entry entry_named(const std::vector<odysea::core::Entry>& entries,
                                const std::string& name) {
    const auto found = std::ranges::find(entries, name, &odysea::core::Entry::name);
    return found == entries.end() ? odysea::core::Entry{} : *found;
}

std::vector<odysea::core::Entry> list(const fs::path& directory) {
    std::error_code ec;
    return odysea::core::read_directory(directory, {.show_hidden = true}, ec);
}

// A device and inode pair that a Btrfs subvolume root would present. The pair
// is what the kernel recycles, so the fixture holds it fixed and varies only
// what the fix adds.
constexpr std::uint64_t recycled_device = 240;
constexpr std::uint64_t subvolume_root_inode = 256;

// The two halves of a creation time travel together in one named type. Passed
// as separate numeric parameters they would be adjacent arguments of
// convertible types, which is a transposition waiting to happen in a fixture
// whose whole point is that the two entries differ only here.
struct BirthTime {
    std::int64_t seconds = 0;
    std::uint32_t nanoseconds = 0;
};

odysea::core::Entry synthetic_entry(std::string name, BirthTime birth) {
    odysea::core::Entry entry;
    entry.name = std::move(name);
    entry.path = fs::path("/synthetic") / entry.name;
    entry.kind = odysea::core::EntryKind::Directory;
    entry.identity.device = recycled_device;
    entry.identity.inode = subvolume_root_inode;
    entry.identity.birth_known = true;
    entry.identity.birth_seconds = birth.seconds;
    entry.identity.birth_nanoseconds = birth.nanoseconds;
    return entry;
}

// DISTINCTNESS, at the model boundary.
//
// Reproduces the collision that a real filesystem produces but a test cannot
// provoke on demand: Btrfs hands a subvolume root an anonymous device number
// and inode 256, returns that device number to a pool when the subvolume is
// removed, and issues the same number to the next subvolume created. Two
// unrelated directories then present exactly the same device and inode pair.
// Feeding that pair directly proves the property of the identity function
// without requiring a Btrfs mount or the privilege to manipulate subvolumes.
void test_a_recycled_device_and_inode_pair_is_not_one_entry() {
    const odysea::core::Entry removed =
        synthetic_entry("alpha", BirthTime{.seconds = 1786139026, .nanoseconds = 430992308});
    const odysea::core::Entry created =
        synthetic_entry("gamma", BirthTime{.seconds = 1786139026, .nanoseconds = 432992322});

    check(removed.identity.device == created.identity.device &&
              removed.identity.inode == created.identity.inode,
          "the fixture holds the recycled device and inode pair identical");
    check(!odysea::core::same_identity(removed.identity, created.identity),
          "a recycled device and inode pair must not identify an unrelated entry");

    // Two milliseconds apart is the measured spacing between a subvolume
    // removal and the next creation, so second-resolution alone would not
    // separate them.
    check(removed.identity.birth_seconds == created.identity.birth_seconds,
          "the fixture keeps both creations inside one second");

    // Ambiguity has to be visible to a consumer, not just to equality. A
    // consumer that follows an entry requires exactly one match.
    const std::vector<odysea::core::Entry> listing{removed, created};
    check(odysea::core::count_identity(listing, removed.identity) == 1,
          "a listing holding both must report the earlier entry exactly once");
    check(odysea::core::count_identity(listing, created.identity) == 1,
          "a listing holding both must report the later entry exactly once");
}

// An unknown identity is not a value that matches other unknown identities.
void test_an_unknown_identity_never_matches() {
    const odysea::core::EntryIdentity unknown;
    const odysea::core::EntryIdentity also_unknown;
    check(!unknown.known(), "a default identity is unknown");
    check(unknown == also_unknown, "two unknown identities hold equal fields");
    check(!odysea::core::same_identity(unknown, also_unknown),
          "a failed metadata lookup must not make two entries the same entry");

    odysea::core::Entry blank;
    const std::vector<odysea::core::Entry> listing{blank, blank};
    check(odysea::core::count_identity(listing, unknown) == 0,
          "an unknown identity matches nothing in a listing");
}

// An identity that knows its creation time and one that does not are not the
// same entry. Refusing the match is the safe direction: it drops selection
// rather than moving it somewhere unintended.
void test_a_partially_known_identity_does_not_match() {
    odysea::core::EntryIdentity dated;
    dated.device = recycled_device;
    dated.inode = subvolume_root_inode;
    dated.birth_known = true;
    dated.birth_seconds = 1786139026;

    odysea::core::EntryIdentity undated;
    undated.device = recycled_device;
    undated.inode = subvolume_root_inode;

    check(!odysea::core::same_identity(dated, undated),
          "a known creation time must not match an absent one");
}

// STABILITY, against a real filesystem.
//
// The distinctness tests above would all pass for an identity that changed
// every time it was read, which would be just as broken. These pin the other
// half: an entry that is renamed, or whose contents change, keeps the identity
// it had, so a consumer can still follow it.
void test_identity_survives_rename_and_content_change(const fs::path& root) {
    const fs::path directory = root / "stability";
    fs::create_directories(directory);
    std::ofstream(directory / "before.txt") << "payload";
    fs::create_directories(directory / "folder");

    const std::vector<odysea::core::Entry> original = list(directory);
    const odysea::core::Entry file_before = entry_named(original, "before.txt");
    const odysea::core::Entry folder_before = entry_named(original, "folder");
    check(file_before.identity.known() && folder_before.identity.known(),
          "the fixture entries have a known identity");
    check(!odysea::core::same_identity(file_before.identity, folder_before.identity),
          "two entries in one directory have distinct identities");

    std::error_code ec;
    fs::rename(directory / "before.txt", directory / "after.txt", ec);
    check(!ec, "the fixture can rename an entry");
    fs::rename(directory / "folder", directory / "renamed-folder", ec);
    check(!ec, "the fixture can rename a directory");
    std::ofstream(directory / "after.txt", std::ios::app) << " more payload";

    const std::vector<odysea::core::Entry> refreshed = list(directory);
    const odysea::core::Entry file_after = entry_named(refreshed, "after.txt");
    const odysea::core::Entry folder_after = entry_named(refreshed, "renamed-folder");

    check(odysea::core::same_identity(file_before.identity, file_after.identity),
          "a renamed and rewritten file keeps its identity");
    check(odysea::core::same_identity(folder_before.identity, folder_after.identity),
          "a renamed directory keeps its identity");
    check(odysea::core::count_identity(refreshed, file_before.identity) == 1,
          "the renamed file is followable without ambiguity");

    fs::remove_all(directory, ec);
}

// A replacement at the same path is a different entry, even though the name
// and the path are unchanged. Without a creation time this depends entirely on
// the filesystem declining to reissue the inode number.
void test_a_replacement_at_the_same_path_is_a_different_entry(const fs::path& root) {
    const fs::path directory = root / "replacement";
    fs::create_directories(directory);
    const fs::path target = directory / "same-name.txt";

    std::ofstream(target) << "first";
    const odysea::core::Entry first = entry_named(list(directory), "same-name.txt");

    std::error_code ec;
    fs::remove(target, ec);
    std::ofstream(target) << "second";
    const odysea::core::Entry second = entry_named(list(directory), "same-name.txt");

    check(first.identity.known() && second.identity.known(),
          "both generations have a known identity");
    check(!odysea::core::same_identity(first.identity, second.identity),
          "a file replaced at the same path is not the file that was there before");

    fs::remove_all(directory, ec);
}

// Ask the filesystem directly whether it records a creation time for `path`.
//
// Independent of the code under test on purpose. Without it the exercise below
// could only tolerate a missing creation time, and tolerating it would let the
// identity quietly stop reading one at all while the suite stayed green.
bool filesystem_reports_a_creation_time(const fs::path& path) {
    struct ::statx details{};
    if (::statx(AT_FDCWD, path.c_str(), AT_SYMLINK_NOFOLLOW | AT_STATX_SYNC_AS_STAT, STATX_BTIME,
                &details) != 0) {
        return false;
    }
    return (details.stx_mask & STATX_BTIME) != 0;
}

// The creation time has to actually be read, or every distinctness guarantee
// above degrades silently to the device and inode pair on a filesystem that
// does report one.
void test_the_creation_time_is_recorded_where_the_filesystem_reports_one(const fs::path& root) {
    const fs::path directory = root / "birth";
    fs::create_directories(directory);
    const fs::path probe_path = directory / "probe.txt";
    std::ofstream(probe_path) << "payload";

    const odysea::core::Entry probe = entry_named(list(directory), "probe.txt");
    check(probe.identity.known(), "the probe entry has a known identity");

    // Not every filesystem records a creation time, so identity degrading to
    // the device and inode pair is a documented outcome. It is only acceptable
    // when the filesystem is the reason: the expectation is pinned to what the
    // filesystem itself reports, so a run on a filesystem that does record one
    // fails if identity stops carrying it.
    const bool available = filesystem_reports_a_creation_time(probe_path);
    check(probe.identity.birth_known == available,
          "identity records a creation time exactly when the filesystem reports one");
    if (available) {
        check(probe.identity.birth_seconds > 0,
              "a recorded creation time is a plausible timestamp");
    }
    std::fprintf(stderr, "note: temporary filesystem %s creation times\n",
                 available ? "reports" : "does not report");

    std::error_code ec;
    fs::remove_all(directory, ec);
}

// `statx` reports the device as separate major and minor numbers, so identity
// has to reassemble one from them. A wrong reassembly would be invisible to
// every exercise above: all entries would carry the same wrong device, stay
// self-consistent, and compare exactly as before. Pinning the value against
// the number `lstat` reports is what makes the reassembly checkable.
void test_the_device_number_matches_what_lstat_reports(const fs::path& root) {
    const fs::path directory = root / "device";
    fs::create_directories(directory);
    const fs::path probe_path = directory / "probe.txt";
    std::ofstream(probe_path) << "payload";

    const odysea::core::Entry probe = entry_named(list(directory), "probe.txt");
    check(probe.identity.known(), "the probe entry has a known identity");

    struct ::stat metadata{};
    check(::lstat(probe_path.c_str(), &metadata) == 0, "the fixture entry can be inspected");
    check(probe.identity.device == static_cast<std::uint64_t>(metadata.st_dev),
          "the identity device number matches the one lstat reports");
    check(probe.identity.inode == static_cast<std::uint64_t>(metadata.st_ino),
          "the identity inode number matches the one lstat reports");

    std::error_code ec;
    fs::remove_all(directory, ec);
}

} // namespace

int main() {
    const fs::path root = make_fixture();

    test_a_recycled_device_and_inode_pair_is_not_one_entry();
    test_an_unknown_identity_never_matches();
    test_a_partially_known_identity_does_not_match();
    test_identity_survives_rename_and_content_change(root);
    test_a_replacement_at_the_same_path_is_a_different_entry(root);
    test_the_creation_time_is_recorded_where_the_filesystem_reports_one(root);
    test_the_device_number_matches_what_lstat_reports(root);

    std::error_code ec;
    fs::remove_all(root, ec);

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("core entry identity tests passed");
    return 0;
}

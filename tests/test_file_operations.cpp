// Headless tests for the core copy, move, and rename primitives.
#include "odysea/core/file_operations.hpp"

#include "file_operations_internal.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;

namespace {

/// Inode of a path, for proving that a replacement happened by rename rather
/// than by writing through the existing destination.
::ino_t inode_of(const fs::path& path) {
    struct ::stat info{};
    if (::lstat(path.c_str(), &info) != 0) {
        return 0;
    }
    return info.st_ino;
}

/// The first entry in `directory` the core recognizes as its own with the given
/// role, or an empty path when there is none.
///
/// Found through the public classifier rather than by matching a spelling, so
/// the tests exercise the same interface a presentation layer would use and do
/// not depend on how the names are built.
fs::path working_entry(const fs::path& directory, WorkingEntryRole role) {
    std::error_code ec;
    fs::directory_iterator element(directory, ec);
    if (ec) {
        return {};
    }
    const fs::directory_iterator end;
    for (; element != end; ++element) {
        if (classify_working_entry(element->path().filename().string()) == role) {
            return element->path();
        }
    }
    return {};
}

bool holds_staging_entry(const fs::path& directory) {
    return !working_entry(directory, WorkingEntryRole::Prepared).empty();
}

bool holds_backup_entry(const fs::path& directory) {
    return !working_entry(directory, WorkingEntryRole::Replaced).empty();
}

/// No trace of an interrupted operation is left in the destination directory.
bool holds_no_working_entry(const fs::path& directory) {
    return !holds_staging_entry(directory) && !holds_backup_entry(directory);
}

void test_copying_a_file_onto_itself_preserves_it() {
    const odysea::test::TemporaryTree tree("copy_self_file");
    const fs::path source = tree.file("here/report.txt", "irreplaceable");

    const OperationOutcome outcome =
        copy_into(source, tree.root() / "here", {.conflict = ConflictPolicy::Overwrite});
    check(outcome.succeeded(), "copying a file into the directory it already occupies succeeds");
    check(outcome.destination == source, "the outcome names the entry that was already in place");
    check(fs::exists(source), "the file still exists after being copied onto itself");
    check(odysea::test::read_text(source) == "irreplaceable",
          "copying a file onto itself does not destroy its contents");
}

void test_copying_a_directory_onto_itself_preserves_it() {
    const odysea::test::TemporaryTree tree("copy_self_directory");
    tree.file("here/project/nested/keep.txt", "irreplaceable");
    const fs::path source = tree.root() / "here/project";

    const OperationOutcome outcome =
        copy_into(source, tree.root() / "here", {.conflict = ConflictPolicy::Overwrite});
    check(outcome.succeeded(), "copying a directory into its own parent succeeds");
    check(fs::exists(source), "the directory still exists after being copied onto itself");
    check(fs::exists(source / "nested/keep.txt"),
          "copying a directory onto itself does not destroy its children");
    check(odysea::test::read_text(source / "nested/keep.txt") == "irreplaceable",
          "the surviving children keep their contents");
}

void test_moving_an_entry_onto_itself_preserves_it() {
    const odysea::test::TemporaryTree tree("move_self");
    const fs::path file_source = tree.file("here/report.txt", "irreplaceable");
    tree.file("here/project/keep.txt", "nested");

    const OperationOutcome file_outcome =
        move_into(file_source, tree.root() / "here", {.conflict = ConflictPolicy::Overwrite});
    check(file_outcome.succeeded(), "moving a file into the directory it occupies succeeds");
    check(odysea::test::read_text(file_source) == "irreplaceable",
          "moving a file onto itself does not destroy it");

    const OperationOutcome directory_outcome =
        move_into(tree.root() / "here/project", tree.root() / "here",
                  {.conflict = ConflictPolicy::Overwrite});
    check(directory_outcome.succeeded(), "moving a directory into its own parent succeeds");
    check(odysea::test::read_text(tree.root() / "here/project/keep.txt") == "nested",
          "moving a directory onto itself does not destroy its children");
}

void test_renaming_to_the_current_name_preserves_the_entry() {
    const odysea::test::TemporaryTree tree("rename_self");
    const fs::path source = tree.file("report.txt", "irreplaceable");

    const OperationOutcome outcome =
        rename_entry(source, "report.txt", {.conflict = ConflictPolicy::Overwrite});
    check(outcome.succeeded(), "renaming an entry to its current name succeeds");
    check(fs::exists(source), "the entry survives a rename to its own name");
    check(odysea::test::read_text(source) == "irreplaceable",
          "a rename to the current name does not destroy contents");
}

void test_a_hard_link_is_recognised_as_the_same_entry() {
    const odysea::test::TemporaryTree tree("copy_hard_link");
    const fs::path source = tree.file("source/report.txt", "irreplaceable");
    const fs::path target_dir = tree.directory("target");

    std::error_code link_ec;
    fs::create_hard_link(source, target_dir / "report.txt", link_ec);
    check(!link_ec, "the fixture hard link should be creatable");

    const OperationOutcome outcome =
        copy_into(source, target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(outcome.succeeded(), "copying onto a hard link of the same entry succeeds");
    check(fs::exists(source) && fs::exists(target_dir / "report.txt"),
          "both names for the entry survive");
    check(odysea::test::read_text(source) == "irreplaceable",
          "an entry reached through another name is not destroyed");
}

void test_replacing_a_file_uses_an_atomic_rename() {
    const odysea::test::TemporaryTree tree("move_atomic");
    const fs::path source = tree.file("source/report.txt", "new");
    const fs::path target_dir = tree.directory("target");
    const fs::path destination = tree.file("target/report.txt", "old");

    const ::ino_t source_inode = inode_of(source);
    const ::ino_t destination_inode = inode_of(destination);
    check(source_inode != 0 && destination_inode != 0 && source_inode != destination_inode,
          "the fixture entries start out distinct");

    const OperationOutcome outcome =
        move_into(source, target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(outcome.succeeded(), "replacing a file by move succeeds");
    check(odysea::test::read_text(destination) == "new", "the replacement contents are in place");
    check(inode_of(destination) == source_inode,
          "the destination was replaced by rename rather than written through");
}

void test_a_failed_overwrite_preserves_both_entries() {
    const odysea::test::TemporaryTree tree("overwrite_failure");
    const fs::path source = tree.file("source/report.txt", "new");
    const fs::path target_dir = tree.directory("target");
    const fs::path destination = tree.file("target/report.txt", "old");

    if (::geteuid() == 0) {
        // A superuser ignores the permission bits this case depends on.
        std::puts("file_operations: skipping the read-only destination case for a superuser");
        return;
    }

    std::error_code permission_ec;
    fs::permissions(target_dir, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, permission_ec);
    check(!permission_ec, "the fixture destination should be able to become read-only");

    const OperationOutcome copy_outcome =
        copy_into(source, target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(!copy_outcome.succeeded(), "a copy into a read-only directory fails");

    const OperationOutcome move_outcome =
        move_into(source, target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(!move_outcome.succeeded(), "a move into a read-only directory fails");

    fs::permissions(target_dir, fs::perms::owner_all, fs::perm_options::replace, permission_ec);

    check(fs::exists(source) && odysea::test::read_text(source) == "new",
          "a failed overwrite leaves the source intact");
    check(fs::exists(destination) && odysea::test::read_text(destination) == "old",
          "a failed overwrite leaves the destination intact");
}

void test_a_failed_directory_copy_leaves_the_destination_intact() {
    const odysea::test::TemporaryTree tree("copy_failure");
    tree.file("source/project/readable.txt", "new");
    const fs::path blocked = tree.directory("source/project/blocked");
    tree.file("source/project/blocked/hidden.txt", "unreadable");
    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/existing.txt", "old");

    if (::geteuid() == 0) {
        std::puts("file_operations: skipping the unreadable-subtree case for a superuser");
        return;
    }

    std::error_code permission_ec;
    fs::permissions(blocked, fs::perms::none, fs::perm_options::replace, permission_ec);
    check(!permission_ec, "the fixture subtree should be able to become unreadable");

    const OperationOutcome outcome = copy_into(tree.root() / "source/project", target_dir,
                                               {.conflict = ConflictPolicy::Overwrite});

    fs::permissions(blocked, fs::perms::owner_all, fs::perm_options::replace, permission_ec);

    check(!outcome.succeeded(), "a copy that cannot read the whole source fails");
    check(fs::exists(target_dir / "project/existing.txt"),
          "a failed directory copy leaves the existing destination in place");
    check(odysea::test::read_text(target_dir / "project/existing.txt") == "old",
          "the surviving destination keeps its contents");

    check(holds_no_working_entry(target_dir),
          "a failed copy removes the working entries it created");
}

/// A rename step that reports a filesystem boundary when the source entry is
/// relocated, and behaves normally otherwise.
///
/// Every move that cannot be completed by a single rename takes the same route,
/// whether the boundary is real or reported here, so the fallback can be driven
/// deterministically on one filesystem. Only relocating the source can cross a
/// boundary; the remaining steps rename within one directory, so they are left
/// to the filesystem.
void rename_across_devices(detail::RenameKind kind, const fs::path& from, const fs::path& to,
                           std::error_code& error) {
    if (kind == detail::RenameKind::Relocate) {
        error = std::make_error_code(std::errc::cross_device_link);
        return;
    }
    detail::rename_with_filesystem(kind, from, to, error);
}

/// A rename step that fails the given kinds and performs every other kind.
///
/// Installing the prepared entry, and putting a moved-aside destination back
/// afterwards, are the steps a test cannot provoke without privileged control
/// of the mount. Failing them here makes the recovery paths reachable.
detail::RenameStep failing_step(std::initializer_list<detail::RenameKind> failing) {
    const std::vector<detail::RenameKind> kinds(failing);
    return [kinds](detail::RenameKind kind, const fs::path& from, const fs::path& to,
                   std::error_code& error) {
        if (std::find(kinds.begin(), kinds.end(), kind) != kinds.end()) {
            error = std::make_error_code(std::errc::io_error);
            return;
        }
        detail::rename_with_filesystem(kind, from, to, error);
    };
}

/// A named pipe cannot be copied, so a source containing one fails the fallback
/// copy on every machine, with no dependence on permissions or on the user.
bool make_fifo(const fs::path& path) {
    return ::mkfifo(path.c_str(), S_IRUSR | S_IWUSR) == 0;
}

/// A directory on a filesystem other than the one holding `neighbour`, or an
/// empty path when the machine offers none that is writable.
fs::path second_filesystem_directory(const fs::path& neighbour) {
    struct ::stat neighbour_info{};
    if (::stat(neighbour.c_str(), &neighbour_info) != 0) {
        return {};
    }

    for (const char* candidate : {"/dev/shm", "/run/user"}) {
        struct ::stat candidate_info{};
        if (::stat(candidate, &candidate_info) != 0) {
            continue;
        }
        if (candidate_info.st_dev == neighbour_info.st_dev) {
            continue;
        }

        const fs::path scratch =
            fs::path(candidate) / ("odysea_cross_device_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(scratch, ec);
        fs::create_directories(scratch, ec);
        if (!ec && fs::exists(scratch)) {
            return scratch;
        }
    }
    return {};
}

void test_a_cross_device_move_replaces_a_file() {
    const odysea::test::TemporaryTree tree("cross_device_file");
    const fs::path source = tree.file("source/report.txt", "new");
    const fs::path target_dir = tree.directory("target");
    const fs::path destination = tree.file("target/report.txt", "old");

    const OperationOutcome outcome = detail::move_into_using(
        source, target_dir, {.conflict = ConflictPolicy::Overwrite}, &rename_across_devices);
    check(outcome.succeeded(), "a move across filesystems replaces an existing file");
    check(odysea::test::read_text(destination) == "new",
          "the replacement contents are in place after the fallback copy");
    check(!fs::exists(source), "the source is removed once the copy is installed");
    check(holds_no_working_entry(target_dir),
          "a completed cross-filesystem move leaves no working entry behind");
}

void test_a_cross_device_move_replaces_a_directory() {
    const odysea::test::TemporaryTree tree("cross_device_directory");
    tree.file("source/project/kept.txt", "kept");
    const fs::path source = tree.root() / "source/project";
    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/stale.txt", "stale");

    const OperationOutcome outcome = detail::move_into_using(
        source, target_dir, {.conflict = ConflictPolicy::Overwrite}, &rename_across_devices);
    check(outcome.succeeded(), "a move across filesystems replaces an existing directory");
    check(odysea::test::read_text(target_dir / "project/kept.txt") == "kept",
          "the moved tree is installed at the destination");
    check(!fs::exists(target_dir / "project/stale.txt"),
          "the replaced directory is gone rather than merged into");
    check(!fs::exists(source), "the source tree is removed once the copy is installed");
    check(holds_no_working_entry(target_dir),
          "a completed cross-filesystem directory move leaves no working entry behind");
}

void test_a_failed_cross_device_move_preserves_the_destination_tree() {
    const odysea::test::TemporaryTree tree("cross_device_failure");
    tree.file("source/project/kept.txt", "kept");
    const fs::path source = tree.root() / "source/project";
    check(make_fifo(source / "pipe"), "the fixture source should be able to hold a named pipe");

    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/irreplaceable.txt", "old");

    const OperationOutcome outcome = detail::move_into_using(
        source, target_dir, {.conflict = ConflictPolicy::Overwrite}, &rename_across_devices);
    check(!outcome.succeeded(), "a move across filesystems fails when the copy cannot complete");
    check(fs::exists(target_dir / "project/irreplaceable.txt"),
          "a failed cross-filesystem move leaves the destination tree in place");
    check(odysea::test::read_text(target_dir / "project/irreplaceable.txt") == "old",
          "the surviving destination keeps its contents");
    check(fs::exists(source / "kept.txt"), "a failed cross-filesystem move leaves the source tree");
    check(holds_no_working_entry(target_dir),
          "a failed cross-filesystem move leaves no working entry behind");
}

void test_a_failed_cross_device_move_preserves_a_replaced_file() {
    const odysea::test::TemporaryTree tree("cross_device_file_failure");
    const fs::path source_dir = tree.directory("source");
    const fs::path source = source_dir / "report.txt";
    check(make_fifo(source), "the fixture source should be able to be a named pipe");
    const fs::path target_dir = tree.directory("target");
    const fs::path destination = tree.file("target/report.txt", "old");

    const OperationOutcome outcome = detail::move_into_using(
        source, target_dir, {.conflict = ConflictPolicy::Overwrite}, &rename_across_devices);
    check(!outcome.succeeded(), "a cross-filesystem move of an uncopyable entry fails");
    check(odysea::test::read_text(destination) == "old",
          "the destination file survives a failed cross-filesystem move");
    check(fs::exists(source), "the source survives a failed cross-filesystem move");
    check(holds_no_working_entry(target_dir),
          "a failed cross-filesystem file move leaves no working entry behind");
}

/// The same behaviour over a real filesystem boundary, when the machine has a
/// second writable filesystem. Confirms the injected step above models what the
/// kernel actually reports.
void test_a_move_over_a_real_filesystem_boundary() {
    const odysea::test::TemporaryTree tree("real_cross_device");
    const fs::path target_dir = tree.directory("target");
    const fs::path scratch = second_filesystem_directory(target_dir);
    if (scratch.empty()) {
        std::puts(
            "file_operations: skipping the real cross-filesystem case (no second filesystem)");
        return;
    }

    std::error_code ec;
    fs::create_directories(scratch / "project", ec);
    {
        std::ofstream kept(scratch / "project/kept.txt", std::ios::binary | std::ios::trunc);
        kept << "kept";
    }
    tree.file("target/project/irreplaceable.txt", "old");
    check(make_fifo(scratch / "project/pipe"),
          "the fixture source should be able to hold a named pipe");

    const OperationOutcome failed =
        move_into(scratch / "project", target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(!failed.succeeded(), "a real cross-filesystem move fails when the copy cannot complete");
    check(odysea::test::read_text(target_dir / "project/irreplaceable.txt") == "old",
          "a failed real cross-filesystem move leaves the destination tree in place");
    check(holds_no_working_entry(target_dir),
          "a failed real cross-filesystem move leaves no working entry behind");

    fs::remove(scratch / "project/pipe", ec);
    const OperationOutcome moved =
        move_into(scratch / "project", target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(moved.succeeded(), "a real cross-filesystem move replaces the destination");
    check(odysea::test::read_text(target_dir / "project/kept.txt") == "kept",
          "the moved tree is installed across a real filesystem boundary");
    check(!fs::exists(scratch / "project"), "the source is removed after a real move succeeds");
    check(holds_no_working_entry(target_dir),
          "a completed real move leaves no working entry behind");

    fs::remove_all(scratch, ec);
}

void test_a_failed_install_preserves_a_copied_over_directory() {
    const odysea::test::TemporaryTree tree("copy_install_failure");
    tree.file("source/project/kept.txt", "kept");
    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/irreplaceable.txt", "old");

    const OperationOutcome outcome = detail::copy_into_using(
        tree.root() / "source/project", target_dir, {.conflict = ConflictPolicy::Overwrite},
        failing_step({detail::RenameKind::Install}));
    check(!outcome.succeeded(), "a copy whose install fails reports the failure");
    check(odysea::test::read_text(target_dir / "project/irreplaceable.txt") == "old",
          "a failed install leaves the replaced directory in place");
    check(odysea::test::read_text(tree.root() / "source/project/kept.txt") == "kept",
          "a failed install leaves the copied source untouched");
    check(holds_no_working_entry(target_dir),
          "a failed install leaves no working entry behind after a copy");
}

void test_a_failed_install_preserves_a_copied_over_file() {
    const odysea::test::TemporaryTree tree("copy_file_install_failure");
    const fs::path source = tree.file("source/report.txt", "new");
    const fs::path target_dir = tree.directory("target");
    const fs::path destination = tree.file("target/report.txt", "old");

    const OperationOutcome outcome =
        detail::copy_into_using(source, target_dir, {.conflict = ConflictPolicy::Overwrite},
                                failing_step({detail::RenameKind::Install}));
    check(!outcome.succeeded(), "a file copy whose install fails reports the failure");
    check(odysea::test::read_text(destination) == "old",
          "a failed install leaves the replaced file in place");
    check(odysea::test::read_text(source) == "new",
          "a failed install leaves the copied file untouched");
    check(holds_no_working_entry(target_dir),
          "a failed file-copy install leaves no working entry behind");
}

void test_a_failed_install_preserves_a_moved_over_directory() {
    const odysea::test::TemporaryTree tree("move_install_failure");
    tree.file("source/project/kept.txt", "kept");
    const fs::path source = tree.root() / "source/project";
    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/irreplaceable.txt", "old");

    const OperationOutcome outcome =
        detail::move_into_using(source, target_dir, {.conflict = ConflictPolicy::Overwrite},
                                failing_step({detail::RenameKind::Install}));
    check(!outcome.succeeded(), "a move whose install fails reports the failure");
    check(odysea::test::read_text(target_dir / "project/irreplaceable.txt") == "old",
          "a failed install leaves the replaced directory in place");
    check(odysea::test::read_text(source / "kept.txt") == "kept",
          "a failed install puts the moved source back under its own name");
    check(holds_no_working_entry(target_dir),
          "a failed install leaves no working entry behind after a move");
}

void test_a_failed_install_preserves_a_renamed_over_directory() {
    const odysea::test::TemporaryTree tree("rename_install_failure");
    tree.file("project/kept.txt", "kept");
    tree.file("archive/irreplaceable.txt", "old");
    const fs::path source = tree.root() / "project";

    const OperationOutcome outcome =
        detail::rename_entry_using(source, "archive", {.conflict = ConflictPolicy::Overwrite},
                                   failing_step({detail::RenameKind::Install}));
    check(!outcome.succeeded(), "a rename whose install fails reports the failure");
    check(odysea::test::read_text(tree.root() / "archive/irreplaceable.txt") == "old",
          "a failed rename install leaves the replaced directory in place");
    check(odysea::test::read_text(source / "kept.txt") == "kept",
          "a failed rename install leaves the source under its original name");
    check(holds_no_working_entry(tree.root()),
          "a failed rename install leaves no working entry behind");
}

void test_a_failed_backup_leaves_both_entries_untouched() {
    const odysea::test::TemporaryTree tree("move_backup_failure");
    tree.file("source/project/kept.txt", "kept");
    const fs::path source = tree.root() / "source/project";
    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/irreplaceable.txt", "old");

    const OperationOutcome outcome =
        detail::move_into_using(source, target_dir, {.conflict = ConflictPolicy::Overwrite},
                                failing_step({detail::RenameKind::Backup}));
    check(!outcome.succeeded(), "a move whose destination cannot be moved aside reports it");
    check(odysea::test::read_text(target_dir / "project/irreplaceable.txt") == "old",
          "a destination that could not be moved aside is still where it was");
    check(odysea::test::read_text(source / "kept.txt") == "kept",
          "the source is put back when the destination cannot be moved aside");
    check(holds_no_working_entry(target_dir), "a failed backup leaves no working entry behind");
}

/// Recovery can fail too. When it does the data stays under the name it was
/// parked at instead of being cleaned up, because a misplaced entry can be
/// recovered and a removed one cannot.
void test_a_failed_restore_retains_the_replaced_destination() {
    const odysea::test::TemporaryTree tree("move_restore_failure");
    tree.file("source/project/kept.txt", "kept");
    const fs::path source = tree.root() / "source/project";
    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/irreplaceable.txt", "old");

    const OperationOutcome outcome = detail::move_into_using(
        source, target_dir, {.conflict = ConflictPolicy::Overwrite},
        failing_step({detail::RenameKind::Install, detail::RenameKind::Restore}));
    check(!outcome.succeeded(), "a move whose install and recovery both fail reports the failure");
    check(odysea::test::read_text(source / "kept.txt") == "kept",
          "the source is still put back when the destination cannot be restored");

    const fs::path retained = working_entry(target_dir, WorkingEntryRole::Replaced);
    check(!retained.empty(), "the replaced destination is retained rather than removed");
    check(!retained.empty() && odysea::test::read_text(retained / "irreplaceable.txt") == "old",
          "the retained destination keeps its contents");
    check(!holds_staging_entry(target_dir),
          "only the unrecoverable destination is left behind, not the staged source");
}

/// The same guarantee for the other side: a source that cannot be put back is
/// left under its staging name rather than removed.
void test_a_failed_unwind_retains_the_source() {
    const odysea::test::TemporaryTree tree("move_unwind_failure");
    tree.file("source/project/kept.txt", "kept");
    const fs::path source = tree.root() / "source/project";
    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/irreplaceable.txt", "old");

    const OperationOutcome outcome = detail::move_into_using(
        source, target_dir, {.conflict = ConflictPolicy::Overwrite},
        failing_step({detail::RenameKind::Install, detail::RenameKind::Unwind}));
    check(!outcome.succeeded(), "a move whose install and source recovery both fail reports it");
    check(odysea::test::read_text(target_dir / "project/irreplaceable.txt") == "old",
          "the destination is restored even when the source cannot be put back");

    const fs::path retained = working_entry(target_dir, WorkingEntryRole::Prepared);
    check(!retained.empty(), "a source that cannot be put back is retained rather than removed");
    check(!retained.empty() && odysea::test::read_text(retained / "kept.txt") == "kept",
          "the retained source keeps its contents");
    check(!holds_backup_entry(target_dir), "the restored destination leaves no backup behind");
}

/// The longest single name the filesystem holding `directory` accepts.
///
/// Queried rather than assumed so the tests exercise the real limit wherever
/// they run, with the common 255-byte limit as the fallback.
std::size_t longest_name(const fs::path& directory) {
    const long limit = ::pathconf(directory.c_str(), _PC_NAME_MAX);
    if (limit <= 0) {
        return 255;
    }
    return static_cast<std::size_t>(limit);
}

/// A name of exactly the maximum length, ending in `suffix`.
std::string maximal_name(const fs::path& directory, std::string_view suffix) {
    const std::size_t limit = longest_name(directory);
    std::string name(limit - suffix.size(), 'n');
    name += suffix;
    return name;
}

void test_maximal_names_transfer_into_a_free_destination() {
    const odysea::test::TemporaryTree tree("max_name_free");
    const fs::path source_dir = tree.directory("source");
    const fs::path target_dir = tree.directory("target");
    const std::string name = maximal_name(target_dir, ".txt");
    check(name.size() == longest_name(target_dir), "the fixture name is exactly at the limit");

    const fs::path copied = tree.file("source/" + name, "contents");
    const OperationOutcome copy_outcome = copy_into(copied, target_dir, OperationOptions{});
    check(copy_outcome.succeeded(), "a name at the length limit can be copied");
    check(odysea::test::read_text(target_dir / name) == "contents",
          "a copy of a maximal name arrives with its contents");
    check(holds_no_working_entry(target_dir), "a maximal-name copy leaves no working entry");

    const std::string moved_name = maximal_name(target_dir, ".dat");
    const fs::path moved = tree.file("source/" + moved_name, "moved");
    const OperationOutcome move_outcome = move_into(moved, target_dir, OperationOptions{});
    check(move_outcome.succeeded(), "a name at the length limit can be moved");
    check(odysea::test::read_text(target_dir / moved_name) == "moved",
          "a move of a maximal name arrives with its contents");
    check(!fs::exists(moved), "a maximal-name move removes the source");

    const std::string renamed_name = maximal_name(source_dir, ".bin");
    const fs::path to_rename = tree.file("source/short.txt", "renamed");
    const OperationOutcome rename_outcome = rename_entry(to_rename, renamed_name, {});
    check(rename_outcome.succeeded(), "an entry can be renamed to a name at the length limit");
    check(odysea::test::read_text(source_dir / renamed_name) == "renamed",
          "a rename to a maximal name keeps the contents");
    check(holds_no_working_entry(source_dir), "a maximal-name rename leaves no working entry");
}

void test_maximal_names_replace_an_existing_destination() {
    const odysea::test::TemporaryTree tree("max_name_overwrite");
    const fs::path target_dir = tree.directory("target");
    const std::string name = maximal_name(target_dir, ".txt");

    const fs::path copied = tree.file("source/" + name, "new");
    tree.file("target/" + name, "old");
    const OperationOutcome copy_outcome =
        copy_into(copied, target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(copy_outcome.succeeded(), "a maximal name can be copied over an existing entry");
    check(odysea::test::read_text(target_dir / name) == "new",
          "the replacement contents are installed under a maximal name");
    check(holds_no_working_entry(target_dir), "a maximal-name overwrite leaves no working entry");

    const OperationOutcome move_outcome =
        move_into(copied, target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(move_outcome.succeeded(), "a maximal name can be moved over an existing entry");
    check(odysea::test::read_text(target_dir / name) == "new",
          "the moved contents are installed under a maximal name");
    check(!fs::exists(copied), "the source is gone after a maximal-name move");

    // Directories on both sides take the staged route, which is where the
    // working names are created.
    const std::string directory_name = maximal_name(target_dir, ".d");
    tree.file("source/" + directory_name + "/kept.txt", "kept");
    tree.file("target/" + directory_name + "/stale.txt", "stale");
    const OperationOutcome directory_outcome =
        copy_into(tree.root() / "source" / directory_name, target_dir,
                  {.conflict = ConflictPolicy::Overwrite});
    check(directory_outcome.succeeded(),
          "a maximal-name directory can be copied over an existing one");
    check(odysea::test::read_text(target_dir / directory_name / "kept.txt") == "kept",
          "the replacement tree is installed under a maximal name");
    check(!fs::exists(target_dir / directory_name / "stale.txt"),
          "the replaced tree is gone rather than merged into");
    check(holds_no_working_entry(target_dir),
          "a maximal-name directory overwrite leaves no working entry");
}

/// Whether every byte of `name` belongs to a well-formed UTF-8 character.
bool is_valid_utf8(std::string_view name) {
    std::size_t index = 0;
    while (index < name.size()) {
        const auto lead = static_cast<unsigned char>(name[index]);
        std::size_t width = 0;
        if (lead < 0x80) {
            width = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            width = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            width = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            width = 4;
        } else {
            return false;
        }
        if (index + width > name.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < width; ++offset) {
            if ((static_cast<unsigned char>(name[index + offset]) & 0xC0) != 0x80) {
                return false;
            }
        }
        index += width;
    }
    return true;
}

void test_auto_rename_shortens_a_maximal_name_to_fit_the_number() {
    const odysea::test::TemporaryTree tree("auto_rename_max");
    const fs::path target_dir = tree.directory("target");
    const std::size_t limit = longest_name(target_dir);
    const std::string name = maximal_name(target_dir, ".txt");

    const fs::path source = tree.file("source/" + name, "new");
    tree.file("target/" + name, "old");

    const OperationOutcome outcome =
        copy_into(source, target_dir, {.conflict = ConflictPolicy::AutoRename});
    check(outcome.succeeded(), "auto-rename resolves a collision on a name at the length limit");

    const std::string resolved = outcome.destination.filename().string();
    check(resolved.size() <= limit, "the resolved name fits within the filesystem limit");
    check(resolved != name, "the resolved name differs from the one already taken");
    check(resolved.ends_with(" (2).txt"), "the number and extension survive the shortening");
    check(fs::exists(outcome.destination), "the resolved name is the one that was created");
    check(odysea::test::read_text(outcome.destination) == "new",
          "the copy under the resolved name holds the source contents");
    check(odysea::test::read_text(target_dir / name) == "old",
          "auto-rename leaves the entry it collided with untouched");
}

void test_auto_rename_shortens_a_maximal_name_for_a_long_extension() {
    const odysea::test::TemporaryTree tree("auto_rename_long_extension");
    const fs::path target_dir = tree.directory("target");
    const std::size_t limit = longest_name(target_dir);

    // Almost the whole name is the extension, so the number cannot fit without
    // shortening the extension itself.
    std::string name = "a.";
    name += std::string(limit - name.size(), 'e');
    check(name.size() == limit, "the fixture name is exactly at the limit");

    const fs::path source = tree.file("source/" + name, "new");
    tree.file("target/" + name, "old");

    const OperationOutcome outcome =
        copy_into(source, target_dir, {.conflict = ConflictPolicy::AutoRename});
    check(outcome.succeeded(), "auto-rename resolves a collision on a name that is nearly all "
                               "extension");

    const std::string resolved = outcome.destination.filename().string();
    check(resolved.size() <= limit, "the resolved long-extension name fits within the limit");
    check(resolved.starts_with("a (2)."), "the stem and number are kept ahead of the extension");
    check(fs::exists(outcome.destination), "the resolved long-extension name was created");
    check(odysea::test::read_text(target_dir / name) == "old",
          "the entry it collided with is untouched");
}

void test_auto_rename_resolves_repeated_collisions_at_the_limit() {
    const odysea::test::TemporaryTree tree("auto_rename_repeated");
    const fs::path target_dir = tree.directory("target");
    const std::size_t limit = longest_name(target_dir);
    const std::string name = maximal_name(target_dir, ".txt");

    const fs::path source = tree.file("source/" + name, "new");
    tree.file("target/" + name, "old");

    std::vector<std::string> resolved;
    for (unsigned round = 0; round < 12; ++round) {
        const OperationOutcome outcome =
            copy_into(source, target_dir, {.conflict = ConflictPolicy::AutoRename});
        check(outcome.succeeded(), "each repeated collision at the limit resolves");
        if (!outcome.succeeded()) {
            return;
        }
        resolved.push_back(outcome.destination.filename().string());
    }

    bool all_fit = true;
    bool all_created = true;
    for (const std::string& variant : resolved) {
        if (variant.size() > limit) {
            all_fit = false;
        }
        if (!fs::exists(target_dir / variant)) {
            all_created = false;
        }
    }
    check(all_fit, "every repeated variant fits within the limit");
    check(all_created, "every repeated variant was created under its reported name");

    std::vector<std::string> unique = resolved;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    check(unique.size() == resolved.size(), "repeated collisions resolve to distinct names");

    // The number widens from " (2)" to " (10)", which has to come out of the
    // shortened stem rather than push the name past the limit.
    check(resolved.back().ends_with(" (13).txt"), "the numbering keeps counting past nine");
}

void test_auto_rename_does_not_split_a_utf8_character() {
    const odysea::test::TemporaryTree tree("auto_rename_utf8");
    const fs::path target_dir = tree.directory("target");
    const std::size_t limit = longest_name(target_dir);

    // A name of three-byte characters, padded so it ends exactly at the limit.
    // Shortening it by the width of " (2)" cannot land on a character boundary,
    // so a naive byte cut would leave an invalid sequence.
    const std::string_view character = "\xe6\x96\x87";
    std::string name;
    while (name.size() + character.size() + 4 <= limit) {
        name += character;
    }
    name += std::string(limit - name.size(), 'z');
    check(name.size() == limit, "the fixture name is exactly at the limit");
    check(is_valid_utf8(name), "the fixture name is valid UTF-8");

    const fs::path source = tree.file("source/" + name, "new");
    tree.file("target/" + name, "old");

    const OperationOutcome outcome =
        copy_into(source, target_dir, {.conflict = ConflictPolicy::AutoRename});
    check(outcome.succeeded(), "auto-rename resolves a collision on a maximal UTF-8 name");

    const std::string resolved = outcome.destination.filename().string();
    check(resolved.size() <= limit, "the resolved UTF-8 name fits within the limit");
    check(is_valid_utf8(resolved), "shortening a UTF-8 name does not split a character");
    check(fs::exists(outcome.destination), "the resolved UTF-8 name was created");
}

void test_auto_rename_reports_the_name_it_created() {
    const odysea::test::TemporaryTree tree("auto_rename_reported");
    const fs::path target_dir = tree.directory("target");
    const std::string name = maximal_name(target_dir, ".txt");
    tree.file("target/" + name, "old");

    const OperationOutcome preview =
        resolve_destination(target_dir, name, {.conflict = ConflictPolicy::AutoRename});
    check(preview.succeeded(), "previewing a maximal-name collision succeeds");
    check(!fs::exists(preview.destination), "previewing a name creates nothing");

    const fs::path source = tree.file("source/" + name, "new");
    const OperationOutcome outcome =
        move_into(source, target_dir, {.conflict = ConflictPolicy::AutoRename});
    check(outcome.succeeded(), "a move resolves a maximal-name collision by numbering");
    check(outcome.destination == preview.destination,
          "the preview and the operation agree on the resolved name");
    check(odysea::test::read_text(outcome.destination) == "new",
          "the reported destination is where the entry actually went");
    check(!fs::exists(source), "the source is gone after a numbered move");
}

/// A working entry that outlives its operation can hold the only copy of the
/// caller's data whichever role it carries, so neither role may be treated as
/// disposable. Prepared is the role that looks disposable and is not.
void test_a_retained_prepared_entry_can_hold_the_only_copy() {
    const odysea::test::TemporaryTree tree("retained_prepared_role");
    tree.file("source/project/kept.txt", "kept");
    const fs::path source = tree.root() / "source/project";
    const fs::path target_dir = tree.directory("target");
    tree.file("target/project/irreplaceable.txt", "old");

    const OperationOutcome outcome = detail::move_into_using(
        source, target_dir, {.conflict = ConflictPolicy::Overwrite},
        failing_step({detail::RenameKind::Install, detail::RenameKind::Unwind}));
    check(!outcome.succeeded(), "the arranged move fails");
    check(!fs::exists(source), "the source is no longer under its original name");

    const fs::path retained = working_entry(target_dir, WorkingEntryRole::Prepared);
    check(!retained.empty(), "the only copy of the source is retained as a working entry");
    if (retained.empty()) {
        return;
    }
    check(classify_working_entry(retained.filename().string()) == WorkingEntryRole::Prepared,
          "the retained sole copy carries the Prepared role");
    check(is_working_entry(retained.filename().string()),
          "the retained sole copy is recognized as a working entry");
    check(odysea::test::read_text(retained / "kept.txt") == "kept",
          "the retained sole copy still holds the caller's data");
}

void test_a_maximal_name_rename_replaces_a_directory() {
    const odysea::test::TemporaryTree tree("max_name_rename");
    const fs::path root = tree.root();
    const std::string name = maximal_name(root, ".d");
    tree.file("project/kept.txt", "kept");
    tree.file(name + "/stale.txt", "stale");

    const OperationOutcome outcome =
        rename_entry(root / "project", name, {.conflict = ConflictPolicy::Overwrite});
    check(outcome.succeeded(), "a directory can be renamed over one with a maximal name");
    check(odysea::test::read_text(root / name / "kept.txt") == "kept",
          "the renamed tree is installed under a maximal name");
    check(!fs::exists(root / name / "stale.txt"), "the replaced tree is gone");
    check(holds_no_working_entry(root), "a maximal-name rename leaves no working entry");
}

void test_a_failed_maximal_name_install_loses_nothing() {
    const odysea::test::TemporaryTree tree("max_name_install_failure");
    const fs::path target_dir = tree.directory("target");
    const std::string name = maximal_name(target_dir, ".d");
    tree.file("source/" + name + "/kept.txt", "kept");
    tree.file("target/" + name + "/irreplaceable.txt", "old");

    const OperationOutcome outcome = detail::move_into_using(
        tree.root() / "source" / name, target_dir, {.conflict = ConflictPolicy::Overwrite},
        failing_step({detail::RenameKind::Install}));
    check(!outcome.succeeded(), "a maximal-name move whose install fails reports the failure");
    check(odysea::test::read_text(target_dir / name / "irreplaceable.txt") == "old",
          "a failed maximal-name install leaves the destination in place");
    check(odysea::test::read_text(tree.root() / "source" / name / "kept.txt") == "kept",
          "a failed maximal-name install puts the source back");
    check(holds_no_working_entry(target_dir),
          "a failed maximal-name install leaves no working entry behind");
}

/// Create entries occupying the working names the core would reserve next.
///
/// Derived from a name the core actually produced — its trailing serial is
/// advanced — rather than from any spelling this test knows in advance, so the
/// reservation retry path is driven without duplicating the naming scheme.
std::vector<fs::path> squat_upcoming_names(const fs::path& observed, unsigned count) {
    const fs::path directory = observed.parent_path();
    const std::string name = observed.filename().string();
    const std::size_t split = name.rfind('-');
    const std::string stem = name.substr(0, split + 1);
    const unsigned long long serial = std::stoull(name.substr(split + 1));

    std::vector<fs::path> created;
    for (unsigned step = 1; step <= count; ++step) {
        fs::path squatter = directory / (stem + std::to_string(serial + step));
        std::ofstream stream(squatter, std::ios::binary | std::ios::trunc);
        stream << "squatter";
        created.push_back(std::move(squatter));
    }
    return created;
}

void test_reservation_skips_names_that_are_already_taken() {
    const odysea::test::TemporaryTree tree("squatted_names");
    const fs::path target_dir = tree.directory("target");

    // One staged operation, purely to observe a working name this process
    // produced. Its install is failed so nothing is disturbed.
    tree.file("probe/kept.txt", "kept");
    tree.file("target/probe/old.txt", "old");
    fs::path observed;
    const OperationOutcome probe = detail::move_into_using(
        tree.root() / "probe", target_dir, {.conflict = ConflictPolicy::Overwrite},
        [&observed](detail::RenameKind kind, const fs::path& from, const fs::path& to,
                    std::error_code& error) {
            if (kind == detail::RenameKind::Relocate) {
                observed = to;
            }
            failing_step({detail::RenameKind::Install})(kind, from, to, error);
        });
    check(!probe.succeeded(), "the observing operation fails as arranged");
    check(!observed.empty() &&
              classify_working_entry(observed.filename().string()) == WorkingEntryRole::Prepared,
          "the observed name is one the core recognizes as its own");

    const std::vector<fs::path> squatters = squat_upcoming_names(observed, 8);
    check(squatters.size() == 8, "the fixture occupies the next working names");

    tree.file("source/project/kept.txt", "kept");
    tree.file("target/project/stale.txt", "stale");
    const OperationOutcome outcome = copy_into(tree.root() / "source/project", target_dir,
                                               {.conflict = ConflictPolicy::Overwrite});
    check(outcome.succeeded(), "an operation succeeds when the next working names are taken");
    check(odysea::test::read_text(target_dir / "project/kept.txt") == "kept",
          "the replacement is installed despite the taken names");

    bool squatters_intact = true;
    for (const fs::path& squatter : squatters) {
        if (!fs::exists(squatter) || odysea::test::read_text(squatter) != "squatter") {
            squatters_intact = false;
        }
    }
    check(squatters_intact, "entries that were already in the way are left untouched");
}

void test_copy_file() {
    const odysea::test::TemporaryTree tree("copy_file");
    const fs::path source = tree.file("source/note.txt", "contents");
    const fs::path target_dir = tree.directory("target");

    const OperationOutcome outcome = copy_into(source, target_dir, OperationOptions{});
    check(outcome.succeeded(), "copying a file into an empty directory should succeed");
    check(outcome.destination == target_dir / "note.txt", "copy keeps the source file name");
    check(fs::exists(source), "copy leaves the source in place");
    check(odysea::test::read_text(outcome.destination) == "contents", "copy preserves contents");
}

void test_copy_directory_recursively() {
    const odysea::test::TemporaryTree tree("copy_tree");
    tree.file("source/nested/deep.txt", "deep");
    const fs::path source = tree.root() / "source";
    const fs::path target_dir = tree.directory("target");

    const OperationOutcome outcome = copy_into(source, target_dir, OperationOptions{});
    check(outcome.succeeded(), "copying a directory should succeed");
    check(fs::exists(target_dir / "source/nested/deep.txt"), "directory copy is recursive");
}

void test_copy_preserves_symlinks() {
    const odysea::test::TemporaryTree tree("copy_symlink");
    const fs::path target_file = tree.file("source/real.txt", "real");
    std::error_code ec;
    fs::create_symlink(target_file, tree.root() / "source/link.txt", ec);
    check(!ec, "fixture symlink should be creatable");

    const fs::path destination = tree.directory("target");
    const OperationOutcome outcome =
        copy_into(tree.root() / "source", destination, OperationOptions{});
    check(outcome.succeeded(), "copying a tree containing a symlink should succeed");
    check(fs::is_symlink(fs::symlink_status(destination / "source/link.txt")),
          "symlinks are copied as symlinks rather than followed");
}

void test_conflict_policies() {
    const odysea::test::TemporaryTree tree("conflicts");
    const fs::path source = tree.file("source/note.txt", "new");
    const fs::path target_dir = tree.directory("target");
    tree.file("target/note.txt", "old");

    const OperationOutcome fail = copy_into(source, target_dir, OperationOptions{});
    check(fail.error == std::errc::file_exists, "the default policy refuses to clobber");
    check(odysea::test::read_text(target_dir / "note.txt") == "old",
          "a refused copy leaves the destination untouched");

    const OperationOutcome renamed =
        copy_into(source, target_dir, {.conflict = ConflictPolicy::AutoRename});
    check(renamed.succeeded(), "auto-rename resolves a collision");
    check(renamed.destination == target_dir / "note (2).txt",
          "auto-rename numbers the name before the extension");
    check(odysea::test::read_text(target_dir / "note.txt") == "old",
          "auto-rename leaves the original destination untouched");

    const OperationOutcome overwritten =
        copy_into(source, target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(overwritten.succeeded(), "overwrite replaces the destination");
    check(odysea::test::read_text(target_dir / "note.txt") == "new",
          "overwrite installs the source contents");
}

void test_overwrite_replaces_rather_than_merges() {
    const odysea::test::TemporaryTree tree("overwrite_tree");
    tree.file("source/tree/kept.txt", "kept");
    tree.file("target/tree/stale.txt", "stale");

    const OperationOutcome outcome = copy_into(tree.root() / "source/tree", tree.root() / "target",
                                               {.conflict = ConflictPolicy::Overwrite});
    check(outcome.succeeded(), "overwriting a directory should succeed");
    check(fs::exists(tree.root() / "target/tree/kept.txt"), "overwrite installs the new tree");
    check(!fs::exists(tree.root() / "target/tree/stale.txt"),
          "overwrite replaces the destination instead of merging into it");
}

void test_move_file() {
    const odysea::test::TemporaryTree tree("move_file");
    const fs::path source = tree.file("source/note.txt", "contents");
    const fs::path target_dir = tree.directory("target");

    const OperationOutcome outcome = move_into(source, target_dir, OperationOptions{});
    check(outcome.succeeded(), "moving a file should succeed");
    check(!fs::exists(source), "move removes the source");
    check(odysea::test::read_text(target_dir / "note.txt") == "contents",
          "move preserves contents");
}

void test_rejects_moving_a_directory_into_itself() {
    const odysea::test::TemporaryTree tree("self_move");
    const fs::path source = tree.directory("source");
    const fs::path nested = tree.directory("source/nested");

    check(copy_into(source, source, OperationOptions{}).error == std::errc::invalid_argument,
          "a directory cannot be copied into itself");
    check(move_into(source, nested, OperationOptions{}).error == std::errc::invalid_argument,
          "a directory cannot be moved into its own descendant");
}

void test_rename() {
    const odysea::test::TemporaryTree tree("rename");
    const fs::path source = tree.file("note.txt", "contents");

    const OperationOutcome outcome = rename_entry(source, "renamed.txt", {});
    check(outcome.succeeded(), "renaming should succeed");
    check(outcome.destination == tree.root() / "renamed.txt", "rename stays in the same directory");
    check(!fs::exists(source), "the old name is gone after a rename");

    check(rename_entry(outcome.destination, "", {}).error == std::errc::invalid_argument,
          "an empty name is rejected");
    check(rename_entry(outcome.destination, "..", {}).error == std::errc::invalid_argument,
          "the parent-directory special is rejected");
    check(rename_entry(outcome.destination, "sub/name.txt", {}).error ==
              std::errc::invalid_argument,
          "a name containing a separator is rejected");
    check(rename_entry(tree.root() / "absent.txt", "any.txt", {}).error ==
              std::errc::no_such_file_or_directory,
          "renaming a missing entry reports a missing source");
}

void test_missing_inputs() {
    const odysea::test::TemporaryTree tree("missing");
    const fs::path target_dir = tree.directory("target");

    check(copy_into(tree.root() / "absent", target_dir, OperationOptions{}).error ==
              std::errc::no_such_file_or_directory,
          "copying a missing source reports a missing source");
    check(move_into(tree.file("real.txt"), tree.root() / "absent_dir", OperationOptions{}).error ==
              std::errc::not_a_directory,
          "a missing destination directory is reported");
}

void test_resolve_destination_previews_names() {
    const odysea::test::TemporaryTree tree("resolve");
    tree.file("report.txt");

    const OperationOutcome free_name = resolve_destination(tree.root(), "fresh.txt", {});
    check(free_name.destination == tree.root() / "fresh.txt", "a free name resolves unchanged");

    const OperationOutcome taken =
        resolve_destination(tree.root(), "report.txt", {.conflict = ConflictPolicy::AutoRename});
    check(taken.destination == tree.root() / "report (2).txt",
          "a taken name resolves to the next free variant");
    check(!fs::exists(taken.destination), "previewing a name creates nothing");
}

} // namespace

int main() {
    test_copying_a_file_onto_itself_preserves_it();
    test_copying_a_directory_onto_itself_preserves_it();
    test_moving_an_entry_onto_itself_preserves_it();
    test_renaming_to_the_current_name_preserves_the_entry();
    test_a_hard_link_is_recognised_as_the_same_entry();
    test_replacing_a_file_uses_an_atomic_rename();
    test_a_failed_overwrite_preserves_both_entries();
    test_a_failed_directory_copy_leaves_the_destination_intact();
    test_a_cross_device_move_replaces_a_file();
    test_a_cross_device_move_replaces_a_directory();
    test_a_failed_cross_device_move_preserves_the_destination_tree();
    test_a_failed_cross_device_move_preserves_a_replaced_file();
    test_a_move_over_a_real_filesystem_boundary();
    test_a_failed_install_preserves_a_copied_over_directory();
    test_a_failed_install_preserves_a_copied_over_file();
    test_a_failed_install_preserves_a_moved_over_directory();
    test_a_failed_install_preserves_a_renamed_over_directory();
    test_a_failed_backup_leaves_both_entries_untouched();
    test_a_failed_restore_retains_the_replaced_destination();
    test_a_failed_unwind_retains_the_source();
    test_maximal_names_transfer_into_a_free_destination();
    test_maximal_names_replace_an_existing_destination();
    test_auto_rename_shortens_a_maximal_name_to_fit_the_number();
    test_auto_rename_shortens_a_maximal_name_for_a_long_extension();
    test_auto_rename_resolves_repeated_collisions_at_the_limit();
    test_auto_rename_does_not_split_a_utf8_character();
    test_auto_rename_reports_the_name_it_created();
    test_a_retained_prepared_entry_can_hold_the_only_copy();
    test_a_maximal_name_rename_replaces_a_directory();
    test_a_failed_maximal_name_install_loses_nothing();
    test_reservation_skips_names_that_are_already_taken();
    test_copy_file();
    test_copy_directory_recursively();
    test_copy_preserves_symlinks();
    test_conflict_policies();
    test_overwrite_replaces_rather_than_merges();
    test_move_file();
    test_rejects_moving_a_directory_into_itself();
    test_rename();
    test_missing_inputs();
    test_resolve_destination_previews_names();
    return odysea::test::report("file_operations");
}

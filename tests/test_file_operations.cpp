// Headless tests for the core copy, move, and rename primitives.
#include "odysea/core/file_operations.hpp"

#include "file_operations_internal.hpp"
#include "test_support.hpp"

#include <algorithm>
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

/// The first entry in `directory` whose name starts with `prefix`, or an empty
/// path when there is none. Both working-entry prefixes are internal to the
/// core, so the tests name them literally rather than depending on the
/// constants they assert about.
fs::path working_entry(const fs::path& directory, std::string_view prefix) {
    std::error_code ec;
    fs::directory_iterator element(directory, ec);
    if (ec) {
        return {};
    }
    const fs::directory_iterator end;
    for (; element != end; ++element) {
        if (element->path().filename().string().starts_with(prefix)) {
            return element->path();
        }
    }
    return {};
}

bool holds_staging_entry(const fs::path& directory) {
    return !working_entry(directory, ".odysea-staging-").empty();
}

bool holds_backup_entry(const fs::path& directory) {
    return !working_entry(directory, ".odysea-replaced-").empty();
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

    const fs::path retained = working_entry(target_dir, ".odysea-replaced-");
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

    const fs::path retained = working_entry(target_dir, ".odysea-staging-");
    check(!retained.empty(), "a source that cannot be put back is retained rather than removed");
    check(!retained.empty() && odysea::test::read_text(retained / "kept.txt") == "kept",
          "the retained source keeps its contents");
    check(!holds_backup_entry(target_dir), "the restored destination leaves no backup behind");
}

void test_copy_file() {
    const odysea::test::TemporaryTree tree("copy_file");
    const fs::path source = tree.file("source/note.txt", "contents");
    const fs::path target_dir = tree.directory("target");

    const OperationOutcome outcome = copy_into(source, target_dir, {});
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

    const OperationOutcome outcome = copy_into(source, target_dir, {});
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
    const OperationOutcome outcome = copy_into(tree.root() / "source", destination, {});
    check(outcome.succeeded(), "copying a tree containing a symlink should succeed");
    check(fs::is_symlink(fs::symlink_status(destination / "source/link.txt")),
          "symlinks are copied as symlinks rather than followed");
}

void test_conflict_policies() {
    const odysea::test::TemporaryTree tree("conflicts");
    const fs::path source = tree.file("source/note.txt", "new");
    const fs::path target_dir = tree.directory("target");
    tree.file("target/note.txt", "old");

    const OperationOutcome fail = copy_into(source, target_dir, {});
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

    const OperationOutcome outcome = move_into(source, target_dir, {});
    check(outcome.succeeded(), "moving a file should succeed");
    check(!fs::exists(source), "move removes the source");
    check(odysea::test::read_text(target_dir / "note.txt") == "contents",
          "move preserves contents");
}

void test_rejects_moving_a_directory_into_itself() {
    const odysea::test::TemporaryTree tree("self_move");
    const fs::path source = tree.directory("source");
    const fs::path nested = tree.directory("source/nested");

    check(copy_into(source, source, {}).error == std::errc::invalid_argument,
          "a directory cannot be copied into itself");
    check(move_into(source, nested, {}).error == std::errc::invalid_argument,
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

    check(copy_into(tree.root() / "absent", target_dir, {}).error ==
              std::errc::no_such_file_or_directory,
          "copying a missing source reports a missing source");
    check(move_into(tree.file("real.txt"), tree.root() / "absent_dir", {}).error ==
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

// Headless tests for the core copy, move, and rename primitives.
#include "odysea/core/file_operations.hpp"

#include "file_operations_internal.hpp"
#include "test_support.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

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

    bool staging_left_behind = false;
    for (const fs::directory_entry& element : fs::directory_iterator(target_dir)) {
        if (element.path().filename().string().starts_with(".odysea-staging-")) {
            staging_left_behind = true;
        }
    }
    check(!staging_left_behind, "a failed copy removes the staging entry it created");
}

/// A rename step that always reports a filesystem boundary.
///
/// Every move that cannot be completed by a single rename takes the same route,
/// whether the boundary is real or reported here, so the fallback can be driven
/// deterministically on one filesystem.
void rename_across_devices(const fs::path& /*from*/, const fs::path& /*to*/,
                           std::error_code& error) {
    error = std::make_error_code(std::errc::cross_device_link);
}

/// A named pipe cannot be copied, so a source containing one fails the fallback
/// copy on every machine, with no dependence on permissions or on the user.
bool make_fifo(const fs::path& path) {
    return ::mkfifo(path.c_str(), S_IRUSR | S_IWUSR) == 0;
}

bool holds_staging_entry(const fs::path& directory) {
    std::error_code ec;
    fs::directory_iterator element(directory, ec);
    if (ec) {
        return false;
    }
    const fs::directory_iterator end;
    for (; element != end; ++element) {
        if (element->path().filename().string().starts_with(".odysea-staging-")) {
            return true;
        }
    }
    return false;
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
    check(!holds_staging_entry(target_dir),
          "a completed cross-filesystem move leaves no staging entry");
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
    check(!holds_staging_entry(target_dir),
          "a completed cross-filesystem directory move leaves no staging entry");
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
    check(!holds_staging_entry(target_dir), "a failed cross-filesystem move discards its staging");
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
    check(!holds_staging_entry(target_dir),
          "a failed cross-filesystem file move discards its staging");
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
    check(!holds_staging_entry(target_dir),
          "a failed real cross-filesystem move discards its staging");

    fs::remove(scratch / "project/pipe", ec);
    const OperationOutcome moved =
        move_into(scratch / "project", target_dir, {.conflict = ConflictPolicy::Overwrite});
    check(moved.succeeded(), "a real cross-filesystem move replaces the destination");
    check(odysea::test::read_text(target_dir / "project/kept.txt") == "kept",
          "the moved tree is installed across a real filesystem boundary");
    check(!fs::exists(scratch / "project"), "the source is removed after a real move succeeds");
    check(!holds_staging_entry(target_dir), "a completed real move leaves no staging entry");

    fs::remove_all(scratch, ec);
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

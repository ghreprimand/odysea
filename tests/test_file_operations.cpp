// Headless tests for the core copy, move, and rename primitives.
#include "odysea/core/file_operations.hpp"

#include "test_support.hpp"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;

namespace {

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

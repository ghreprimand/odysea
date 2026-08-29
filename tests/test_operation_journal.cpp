// Headless tests for the reversible operation journal.
//
// Every scenario is listed in one table and the suite fails by name when it
// runs fewer scenarios than the table holds, so a scenario that stops being
// called cannot read as a pass. The two scenarios that need a second
// filesystem say so when the machine offers none, rather than disappearing.
//
// The tests redirect XDG_DATA_HOME into a temporary tree, so nothing touches a
// real desktop trash directory.
#include "odysea/core/operation_journal.hpp"

#include "operation_journal_internal.hpp"

#include "test_support.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;
using odysea::test::check;
using namespace odysea::core;

namespace {

/// How many scenarios could not run. Held inside a function so the suite has
/// no mutable state at namespace scope.
std::size_t& decline_count() {
    static std::size_t count = 0;
    return count;
}

/// Say that a scenario could not run and why. A scenario that cannot run says
/// so; it never simply returns.
void decline(const std::string& scenario, const std::string& precondition) {
    std::fputs(("operation journal: " + scenario + " declined, " + precondition + "\n").c_str(),
               stdout);
    ++decline_count();
}

/// A directory on a filesystem other than the one holding `neighbour`, or an
/// empty path when the machine offers none that is writable. Kept local to
/// this suite so the shared harness stays free of machine probing.
fs::path other_filesystem_directory(const fs::path& neighbour, const std::string& label) {
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
            fs::path(candidate) / ("odysea_journal_" + label + "_" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(scratch, ec);
        fs::create_directories(scratch, ec);
        if (!ec && fs::exists(scratch)) {
            return scratch;
        }
    }
    return {};
}

/// Removes a scratch directory that lives outside the temporary tree.
class ScratchDirectory {
  public:
    explicit ScratchDirectory(fs::path path) : path_(std::move(path)) {}
    ScratchDirectory(const ScratchDirectory&) = delete;
    ScratchDirectory& operator=(const ScratchDirectory&) = delete;
    ScratchDirectory(ScratchDirectory&&) = delete;
    ScratchDirectory& operator=(ScratchDirectory&&) = delete;
    ~ScratchDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

  private:
    fs::path path_;
};

/// Push a path's modification time forward, so a rewrite is distinguishable
/// from the write that created it however coarse the clock is.
void age_forward(const fs::path& path) {
    std::error_code ec;
    const fs::file_time_type when = fs::last_write_time(path, ec);
    if (ec) {
        return;
    }
    fs::last_write_time(path, when + std::chrono::seconds(30), ec);
}

/// Rewrite a path's contents and put its modification time back where it was,
/// so a change of size is the only thing left to notice.
void rewrite_keeping_the_time(const fs::path& path, const std::string& contents) {
    std::error_code ec;
    const fs::file_time_type when = fs::last_write_time(path, ec);
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream << contents;
    }
    if (!ec) {
        fs::last_write_time(path, when, ec);
    }
}

constexpr OperationOptions fail_on_conflict{.conflict = ConflictPolicy::Fail};
constexpr OperationOptions overwrite{.conflict = ConflictPolicy::Overwrite};
constexpr OperationOptions auto_rename{.conflict = ConflictPolicy::AutoRename};

// --- recording and history ------------------------------------------------

void test_a_completed_move_is_recorded_as_reversible() {
    const odysea::test::TemporaryTree tree("journal_record_move");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    check(journal.empty(), "a new journal holds nothing");
    check(!journal.can_undo(), "an empty journal offers no reversal");

    const OperationOutcome outcome = journal.move_into(source, target, fail_on_conflict);
    check(outcome.succeeded(), "the move should succeed");
    check(journal.size() == 1, "a completed move is recorded");
    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->kind == OperationKind::Move, "the record names the move");
    check(record != nullptr && record->reversible(), "a plain move is reversible");
    check(record != nullptr && record->original_path == source, "the record keeps the origin");
    check(record != nullptr && record->result_path == target / "report.txt",
          "the record keeps the result");
    check(journal.can_undo(), "a reversible record is offered");
}

void test_a_failed_operation_is_not_recorded() {
    const odysea::test::TemporaryTree tree("journal_failed");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");
    tree.file("to/report.txt", "occupant");

    OperationJournal journal;
    const OperationOutcome outcome = journal.move_into(source, target, fail_on_conflict);
    check(!outcome.succeeded(), "a colliding move under Fail should not succeed");
    check(journal.empty(), "an operation that did not happen is not recorded");
    check(!journal.can_undo(), "nothing is offered for reversal");
}

void test_history_is_bounded_by_capacity() {
    const odysea::test::TemporaryTree tree("journal_bounded");
    const fs::path entry = tree.file("names/first.txt", "contents");

    OperationJournal journal(2);
    check(journal.capacity() == 2, "the capacity is what was asked for");
    check(journal.rename_entry(entry, "second.txt", fail_on_conflict).succeeded(), "rename one");
    check(journal.rename_entry(tree.root() / "names/second.txt", "third.txt", fail_on_conflict)
              .succeeded(),
          "rename two");
    check(journal.rename_entry(tree.root() / "names/third.txt", "fourth.txt", fail_on_conflict)
              .succeeded(),
          "rename three");

    check(journal.size() == 2, "the history holds no more than its capacity");
    check(journal.at(0).result_path == tree.root() / "names/fourth.txt",
          "index zero is the newest record");
    check(journal.at(1).result_path == tree.root() / "names/third.txt",
          "the record below it is the one before");
}

void test_a_zero_bound_is_raised_to_one() {
    const OperationJournal journal(0, 0);
    check(journal.capacity() == 1, "a zero capacity is raised to one");
    check(journal.created_tree_limit() == 1, "a zero tree limit is raised to one");
}

void test_forgetting_and_clearing_reverse_nothing() {
    const odysea::test::TemporaryTree tree("journal_forget");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    check(journal.move_into(source, target, fail_on_conflict).succeeded(), "the move should run");
    check(journal.forget_newest(), "the newest record is dropped");
    check(journal.empty(), "the history is empty afterwards");
    check(!journal.forget_newest(), "dropping from an empty history reports nothing to drop");
    check(fs::exists(target / "report.txt"), "forgetting a record moves nothing back");
    check(!fs::exists(source), "forgetting a record restores nothing");

    check(journal.move_into(target / "report.txt", source.parent_path(), fail_on_conflict)
              .succeeded(),
          "the entry can be moved again");
    journal.clear();
    check(journal.empty(), "clearing empties the history");
    check(fs::exists(source), "clearing moves nothing");
}

void test_an_empty_history_reports_nothing_to_reverse() {
    OperationJournal journal;
    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::HistoryEmpty, "an empty history says so");
    check(!outcome.succeeded(), "nothing was reversed");
}

// --- move -----------------------------------------------------------------

void test_a_move_is_returned_to_where_it_came_from() {
    const odysea::test::TemporaryTree tree("journal_undo_move");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    check(journal.move_into(source, target, fail_on_conflict).succeeded(), "the move should run");

    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the move is reversed");
    check(outcome.restored_path == source, "the entry is reported back at its origin");
    check(fs::exists(source), "the entry is back where it started");
    check(odysea::test::read_text(source) == "contents", "the contents came back with it");
    check(!fs::exists(target / "report.txt"), "nothing is left at the destination");
    check(journal.empty(), "a reversed record leaves the history");
}

void test_a_numbered_move_is_returned_under_its_original_name() {
    const odysea::test::TemporaryTree tree("journal_undo_numbered");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");
    tree.file("to/report.txt", "occupant");

    OperationJournal journal;
    const OperationOutcome moved = journal.move_into(source, target, auto_rename);
    check(moved.succeeded(), "a numbered move should succeed");
    check(moved.destination == target / "report (2).txt", "the collision was numbered");

    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the numbered move is reversed");
    check(outcome.restored_path == source, "the entry returns under the name it had");
    check(odysea::test::read_text(source) == "contents", "the returned entry is the moved one");
    check(!fs::exists(target / "report (2).txt"), "the numbered name is vacated");
    check(odysea::test::read_text(target / "report.txt") == "occupant",
          "the entry that caused the collision is untouched");
}

void test_a_reversal_refuses_an_occupied_origin() {
    const odysea::test::TemporaryTree tree("journal_occupied");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    check(journal.move_into(source, target, fail_on_conflict).succeeded(), "the move should run");
    const fs::path replacement = tree.file("from/report.txt", "newer");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::OriginOccupied, "an occupied origin refuses the reversal");
    check(odysea::test::read_text(replacement) == "newer", "the newer entry is not overwritten");
    check(fs::exists(target / "report.txt"), "the moved entry stays where it is");
    check(journal.size() == 1, "a refused reversal keeps its record");
}

void test_a_reversal_refuses_a_result_that_is_no_longer_the_result() {
    const odysea::test::TemporaryTree tree("journal_result_changed");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    check(journal.move_into(source, target, fail_on_conflict).succeeded(), "the move should run");

    // Same path, different entry: exactly the case a path comparison would
    // wave through and an identity comparison catches.
    std::error_code ec;
    fs::remove(target / "report.txt", ec);
    tree.file("to/report.txt", "impostor");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::ResultChanged, "a replaced result refuses the reversal");
    check(odysea::test::read_text(target / "report.txt") == "impostor",
          "the entry now at that path is left alone");
    check(!fs::exists(source), "nothing was moved back");
    check(journal.size() == 1, "a refused reversal keeps its record");
}

void test_a_move_that_discarded_a_destination_can_never_be_reversed() {
    const odysea::test::TemporaryTree tree("journal_move_overwrite");
    const fs::path source = tree.file("from/report.txt", "new");
    const fs::path target = tree.directory("to");
    tree.file("to/report.txt", "discarded");

    OperationJournal journal;
    check(journal.move_into(source, target, overwrite).succeeded(), "the move should replace");

    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->barrier == ReversalBarrier::ReplacedEntryDiscarded,
          "the record names the discarded destination");
    check(!record->reversible(), "the record is not reversible");
    check(!journal.can_undo(), "an unreversible record is never offered");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::Barred, "the reversal is refused outright");
    check(outcome.barrier == ReversalBarrier::ReplacedEntryDiscarded, "the reason is reported");
    check(odysea::test::read_text(target / "report.txt") == "new", "nothing was moved");
    check(!fs::exists(source), "the origin was not repopulated");
    check(journal.size() == 1, "the record stays until it is dropped deliberately");
}

void test_a_move_that_changed_nothing_is_never_reversed() {
    const odysea::test::TemporaryTree tree("journal_move_noop");
    const fs::path source = tree.file("here/report.txt", "contents");

    OperationJournal journal;
    check(journal.move_into(source, source.parent_path(), overwrite).succeeded(),
          "moving an entry where it already is reports success");

    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->barrier == ReversalBarrier::NothingChanged,
          "the record says nothing changed");
    check(journal.undo().status == UndoStatus::Barred, "there is nothing to reverse");
    check(fs::exists(source), "the entry is still there");
    check(odysea::test::read_text(source) == "contents", "and still holds its contents");
}

// --- rename ---------------------------------------------------------------

void test_a_rename_is_returned_to_its_former_name() {
    const odysea::test::TemporaryTree tree("journal_undo_rename");
    const fs::path source = tree.file("names/report.txt", "contents");

    OperationJournal journal;
    const OperationOutcome renamed = journal.rename_entry(source, "summary.txt", fail_on_conflict);
    check(renamed.succeeded(), "the rename should run");
    check(fs::exists(renamed.destination), "the new name exists");

    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the rename is reversed");
    check(outcome.restored_path == source, "the former name is reported");
    check(odysea::test::read_text(source) == "contents", "the entry is back under its old name");
    check(!fs::exists(renamed.destination), "the new name is vacated");
}

void test_a_rename_that_discarded_a_destination_can_never_be_reversed() {
    const odysea::test::TemporaryTree tree("journal_rename_overwrite");
    const fs::path source = tree.file("names/report.txt", "new");
    tree.file("names/summary.txt", "discarded");

    OperationJournal journal;
    check(journal.rename_entry(source, "summary.txt", overwrite).succeeded(),
          "the rename should replace");

    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->barrier == ReversalBarrier::ReplacedEntryDiscarded,
          "the record names the discarded destination");
    check(journal.undo().status == UndoStatus::Barred, "the reversal is refused outright");
    check(odysea::test::read_text(tree.root() / "names/summary.txt") == "new", "nothing moved");
    check(!fs::exists(source), "the former name stays vacant");
}

void test_a_rename_to_the_same_name_is_never_reversed() {
    const odysea::test::TemporaryTree tree("journal_rename_noop");
    const fs::path source = tree.file("names/report.txt", "contents");

    OperationJournal journal;
    check(journal.rename_entry(source, "report.txt", overwrite).succeeded(),
          "renaming to the current name reports success");
    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->barrier == ReversalBarrier::NothingChanged,
          "the record says nothing changed");
    check(journal.undo().status == UndoStatus::Barred, "there is nothing to reverse");
    check(odysea::test::read_text(source) == "contents", "the entry is untouched");
}

// --- trash ----------------------------------------------------------------

void test_a_trashed_entry_is_restored_and_its_record_removed() {
    const odysea::test::TemporaryTree tree("journal_undo_trash");
    const fs::path source = tree.file("documents/report.txt", "contents");

    OperationJournal journal;
    const TrashOutcome trashed = journal.move_to_trash(source);
    check(trashed.succeeded(), "the entry should reach the trash");
    check(!fs::exists(source), "the origin is vacated");
    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->kind == OperationKind::Trash, "the record names the trash");
    check(record != nullptr && record->trash_record_path == trashed.info_path,
          "the record keeps the trash record path");

    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the trash is reversed");
    check(!outcome.error, "the trash record was removed cleanly");
    check(outcome.restored_path == source, "the entry is reported back at its origin");
    check(odysea::test::read_text(source) == "contents", "the entry is back with its contents");
    check(!fs::exists(trashed.trashed_path), "the trash no longer holds the entry");
    check(!fs::exists(trashed.info_path), "the trash record is gone with it");
}

void test_a_trash_reversal_refuses_an_occupied_origin() {
    const odysea::test::TemporaryTree tree("journal_trash_occupied");
    const fs::path source = tree.file("documents/report.txt", "contents");

    OperationJournal journal;
    const TrashOutcome trashed = journal.move_to_trash(source);
    check(trashed.succeeded(), "the entry should reach the trash");
    tree.file("documents/report.txt", "newer");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::OriginOccupied, "an occupied origin refuses the reversal");
    check(odysea::test::read_text(source) == "newer", "the newer entry is not overwritten");
    check(fs::exists(trashed.trashed_path), "the trashed entry stays in the trash");
    check(fs::exists(trashed.info_path), "its record stays with it");
}

void test_a_trash_reversal_refuses_once_the_trash_no_longer_holds_the_entry() {
    const odysea::test::TemporaryTree tree("journal_trash_emptied");
    const fs::path source = tree.file("documents/report.txt", "contents");

    OperationJournal journal;
    const TrashOutcome trashed = journal.move_to_trash(source);
    check(trashed.succeeded(), "the entry should reach the trash");

    std::error_code ec;
    fs::remove(trashed.trashed_path, ec);
    check(!ec, "the trash can be emptied of the entry");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::ResultChanged, "an emptied trash refuses the reversal");
    check(!fs::exists(source), "nothing was invented at the origin");
    check(journal.size() == 1, "a refused reversal keeps its record");
}

// --- copy -----------------------------------------------------------------

void test_a_copy_is_undone_by_removing_what_it_created() {
    const odysea::test::TemporaryTree tree("journal_undo_copy");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    const OperationOutcome copied = journal.copy_into(source, target, fail_on_conflict);
    check(copied.succeeded(), "the copy should run");

    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the copy is reversed");
    check(outcome.restored_path.empty(), "a reversed copy returns nothing");
    check(!fs::exists(copied.destination), "what the copy created is gone");
    check(odysea::test::read_text(source) == "contents", "what was copied is untouched");
}

void test_a_copied_tree_is_undone_whole() {
    const odysea::test::TemporaryTree tree("journal_undo_copy_tree");
    tree.file("from/project/notes.txt", "notes");
    tree.file("from/project/sub/deep.txt", "deep");
    const fs::path source = tree.root() / "from/project";
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    const OperationOutcome copied = journal.copy_into(source, target, fail_on_conflict);
    check(copied.succeeded(), "the tree copy should run");
    check(fs::exists(copied.destination / "sub/deep.txt"), "the whole tree was copied");
    check(journal.can_undo(), "a copied tree within the limit is reversible");

    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the tree copy is reversed");
    check(!fs::exists(copied.destination), "the created tree is gone");
    check(odysea::test::read_text(source / "sub/deep.txt") == "deep", "the original tree remains");
}

void test_a_copy_that_discarded_a_destination_can_never_be_reversed() {
    const odysea::test::TemporaryTree tree("journal_copy_overwrite");
    const fs::path source = tree.file("from/report.txt", "new");
    const fs::path target = tree.directory("to");
    tree.file("to/report.txt", "discarded");

    OperationJournal journal;
    check(journal.copy_into(source, target, overwrite).succeeded(), "the copy should replace");

    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->barrier == ReversalBarrier::ReplacedEntryDiscarded,
          "the record names the discarded destination");
    check(!journal.can_undo(), "an unreversible record is never offered");
    check(journal.undo().status == UndoStatus::Barred, "the reversal is refused outright");
    check(fs::exists(target / "report.txt"),
          "the entry at the destination is not removed as if the copy had created it");
    check(odysea::test::read_text(target / "report.txt") == "new",
          "and is left as the copy left it");
}

void test_a_copy_onto_itself_is_never_reversed() {
    const odysea::test::TemporaryTree tree("journal_copy_noop");
    const fs::path source = tree.file("here/report.txt", "contents");

    OperationJournal journal;
    const OperationOutcome copied = journal.copy_into(source, source.parent_path(), overwrite);
    check(copied.succeeded(), "copying an entry over itself reports success");
    check(copied.destination == source, "the destination resolved to the source");

    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->barrier == ReversalBarrier::NothingChanged,
          "the record says nothing was created");
    check(journal.undo().status == UndoStatus::Barred, "the reversal is refused outright");
    check(fs::exists(source), "the only copy of the entry still exists");
    check(odysea::test::read_text(source) == "contents", "with its contents intact");
}

void test_a_numbered_copy_removes_only_what_it_created() {
    const odysea::test::TemporaryTree tree("journal_copy_numbered");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");
    const fs::path occupant = tree.file("to/report.txt", "occupant");

    OperationJournal journal;
    const OperationOutcome copied = journal.copy_into(source, target, auto_rename);
    check(copied.succeeded(), "a numbered copy should succeed");
    check(copied.destination == target / "report (2).txt", "the collision was numbered");

    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the numbered copy is reversed");
    check(!fs::exists(copied.destination), "the created entry is gone");
    check(odysea::test::read_text(occupant) == "occupant",
          "the entry that was already there is untouched");
    check(odysea::test::read_text(source) == "contents", "what was copied is untouched");
}

void test_a_copy_is_not_removed_once_its_tree_has_gained_an_entry() {
    const odysea::test::TemporaryTree tree("journal_copy_grown");
    tree.file("from/project/sub/deep.txt", "deep");
    const fs::path source = tree.root() / "from/project";
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    const OperationOutcome copied = journal.copy_into(source, target, fail_on_conflict);
    check(copied.succeeded(), "the tree copy should run");

    // Added below the root, so the root's own modification time is unchanged
    // and only the record of the tree can notice.
    const fs::path added = copied.destination / "sub/added.txt";
    tree.file(fs::relative(added, tree.root()).string(), "added");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::ResultChanged, "a grown tree refuses the reversal");
    check(fs::exists(added), "the added entry is not destroyed");
    check(fs::exists(copied.destination / "sub/deep.txt"), "nor is the rest of the tree");
    check(journal.size() == 1, "a refused reversal keeps its record");
}

void test_a_copy_is_not_removed_once_it_has_been_rewritten() {
    const odysea::test::TemporaryTree tree("journal_copy_rewritten");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    const OperationOutcome copied = journal.copy_into(source, target, fail_on_conflict);
    check(copied.succeeded(), "the copy should run");

    // The same number of bytes, so the modification time is the only thing
    // that can notice the rewrite.
    tree.file("to/report.txt", "CONTENTS");
    age_forward(copied.destination);
    check(fs::file_size(copied.destination) == fs::file_size(source),
          "the rewrite left the size alone");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::ResultChanged, "a rewritten copy refuses the reversal");
    check(odysea::test::read_text(copied.destination) == "CONTENTS",
          "the newer contents are not destroyed");
    check(journal.size() == 1, "a refused reversal keeps its record");
}

void test_a_copy_is_not_removed_once_it_has_changed_size() {
    const odysea::test::TemporaryTree tree("journal_copy_resized");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    const OperationOutcome copied = journal.copy_into(source, target, fail_on_conflict);
    check(copied.succeeded(), "the copy should run");

    // The modification time is put back, so the size is the only thing that
    // can notice the rewrite.
    rewrite_keeping_the_time(copied.destination, "contents and rather more besides");
    check(fs::file_size(copied.destination) != fs::file_size(source),
          "the rewrite changed the size");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::ResultChanged, "a resized copy refuses the reversal");
    check(odysea::test::read_text(copied.destination) == "contents and rather more besides",
          "the newer contents are not destroyed");
    check(journal.size() == 1, "a refused reversal keeps its record");
}

void test_a_copied_tree_beyond_the_limit_can_never_be_reversed() {
    const odysea::test::TemporaryTree tree("journal_copy_too_large");
    tree.file("from/project/one.txt", "one");
    tree.file("from/project/two.txt", "two");
    const fs::path source = tree.root() / "from/project";
    const fs::path target = tree.directory("to");

    OperationJournal journal(OperationJournal::default_capacity, 1);
    check(journal.created_tree_limit() == 1, "the tree limit is what was asked for");
    const OperationOutcome copied = journal.copy_into(source, target, fail_on_conflict);
    check(copied.succeeded(), "the copy itself is unaffected by the limit");

    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->barrier == ReversalBarrier::CreatedTreeTooLarge,
          "a tree past the limit is recorded as unreversible");
    check(!journal.can_undo(), "an unreversible record is never offered");
    check(journal.undo().status == UndoStatus::Barred, "the reversal is refused outright");
    check(fs::exists(copied.destination / "one.txt"), "the copied tree is left alone");
}

// --- ordering and partial reversal ---------------------------------------

void test_a_barrier_is_not_stepped_over_to_reach_an_older_record() {
    const odysea::test::TemporaryTree tree("journal_ordering");
    const fs::path renamed_source = tree.file("names/report.txt", "contents");
    const fs::path copy_source = tree.file("from/data.txt", "new");
    const fs::path target = tree.directory("to");
    tree.file("to/data.txt", "discarded");

    OperationJournal journal;
    check(journal.rename_entry(renamed_source, "summary.txt", fail_on_conflict).succeeded(),
          "the rename should run");
    check(journal.copy_into(copy_source, target, overwrite).succeeded(),
          "the replacing copy should run");

    check(journal.undo().status == UndoStatus::Barred,
          "the newest record bars the reversal rather than the older one running");
    check(fs::exists(tree.root() / "names/summary.txt"),
          "the older operation was not reversed out of order");

    check(journal.forget_newest(), "the barred record is dropped deliberately");
    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the older record is reversed once it is newest");
    check(odysea::test::read_text(renamed_source) == "contents", "the rename came back");
}

void test_a_reversal_that_fails_its_second_step_keeps_its_record_and_can_be_retried() {
    const odysea::test::TemporaryTree tree("journal_partial");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");
    tree.file("to/report.txt", "occupant");

    OperationJournal journal;
    const OperationOutcome moved = journal.move_into(source, target, auto_rename);
    check(moved.succeeded(), "a numbered move should succeed");

    // The reversal is two relocations: bring the entry home, then put its name
    // back. Failing only the second reaches the partial state a real
    // filesystem would produce and nothing else can provoke.
    int relocations = 0;
    const detail::RenameStep failing_second =
        [&relocations](detail::RenameKind kind, const fs::path& from, const fs::path& to,
                       std::error_code& error) {
            ++relocations;
            if (relocations == 2) {
                error = std::make_error_code(std::errc::permission_denied);
                return;
            }
            detail::rename_with_filesystem(kind, from, to, error);
        };

    const UndoOutcome partial = detail::JournalReversal::undo_using(journal, failing_second);
    check(relocations == 2, "both steps of the reversal were reached");
    check(partial.status == UndoStatus::Failed, "the reversal reports the step that failed");
    check(partial.error == std::errc::permission_denied, "and what the filesystem said");
    check(journal.size() == 1, "a partial reversal keeps its record");

    const fs::path halfway = source.parent_path() / "report (2).txt";
    check(fs::exists(halfway), "the entry is home under the name it still had");
    check(journal.newest() != nullptr && journal.newest()->result_path == halfway,
          "the record points at where the entry now is");

    const UndoOutcome retried = journal.undo();
    check(retried.succeeded(), "the reversal completes when it is tried again");
    check(retried.restored_path == source, "the entry ends up under its original name");
    check(odysea::test::read_text(source) == "contents", "with its contents");
    check(!fs::exists(halfway), "and nothing is left halfway");
    check(journal.empty(), "the completed reversal leaves the history");
}

void test_a_reversal_refuses_when_the_name_it_travels_under_is_occupied() {
    const odysea::test::TemporaryTree tree("journal_intermediate");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");
    tree.file("to/report.txt", "occupant");

    OperationJournal journal;
    const OperationOutcome moved = journal.move_into(source, target, auto_rename);
    check(moved.succeeded(), "a numbered move should succeed");

    // The origin itself is free, but the name the entry currently carries is
    // taken in the origin directory. The reversal brings the entry home under
    // that name before restoring the original one, so it has to refuse.
    const fs::path blocker = tree.file("from/report (2).txt", "blocker");

    const UndoOutcome outcome = journal.undo();
    check(outcome.status == UndoStatus::OriginOccupied,
          "an occupied travelling name refuses the reversal");
    check(odysea::test::read_text(blocker) == "blocker", "the blocking entry is not overwritten");
    check(fs::exists(target / "report (2).txt"), "the moved entry stays where it is");
    check(!fs::exists(source), "the origin is left as it was");
    check(journal.size() == 1, "a refused reversal keeps its record");
}

void test_a_copy_reversal_reports_a_removal_the_filesystem_refused() {
    if (::geteuid() == 0) {
        decline("a copy reversal reporting a refused removal",
                "a privileged process is not refused a removal");
        return;
    }

    const odysea::test::TemporaryTree tree("journal_remove_refused");
    const fs::path source = tree.file("from/report.txt", "contents");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    const OperationOutcome copied = journal.copy_into(source, target, fail_on_conflict);
    check(copied.succeeded(), "the copy should run");

    // Removing an entry needs write permission on the directory holding it.
    std::error_code ec;
    fs::permissions(target, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, ec);
    check(!ec, "the destination directory can be made unwritable");

    const UndoOutcome outcome = journal.undo();
    fs::permissions(target, fs::perms::owner_all, fs::perm_options::replace, ec);

    check(outcome.status == UndoStatus::Failed, "a refused removal is reported as a failure");
    check(outcome.error == std::errc::permission_denied, "and carries what the filesystem said");
    check(fs::exists(copied.destination), "the copy is still there");
    check(journal.size() == 1, "a failed reversal keeps its record");
}

// --- a second filesystem --------------------------------------------------

void test_a_cross_filesystem_move_of_a_single_name_is_reversible() {
    const odysea::test::TemporaryTree tree("journal_cross_single");
    const fs::path elsewhere = other_filesystem_directory(tree.root(), "single");
    if (elsewhere.empty()) {
        decline("a cross-filesystem move of a single name",
                "no writable second filesystem is available");
        return;
    }
    const ScratchDirectory scratch(elsewhere);

    const fs::path source = scratch.path() / "report.txt";
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream << "contents";
    }
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    check(journal.move_into(source, target, fail_on_conflict).succeeded(),
          "the move across filesystems should succeed");
    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->reversible(),
          "a crossing move of a singly named entry is reversible");

    const UndoOutcome outcome = journal.undo();
    check(outcome.succeeded(), "the crossing move is reversed");
    check(outcome.restored_path == source, "the entry is back where it started");
    check(odysea::test::read_text(source) == "contents", "with its contents");
    check(!fs::exists(target / "report.txt"), "and nothing is left behind");
}

void test_a_cross_filesystem_move_of_a_shared_entry_can_never_be_reversed() {
    const odysea::test::TemporaryTree tree("journal_cross_linked");
    const fs::path elsewhere = other_filesystem_directory(tree.root(), "linked");
    if (elsewhere.empty()) {
        decline("a cross-filesystem move of a shared entry",
                "no writable second filesystem is available");
        return;
    }
    const ScratchDirectory scratch(elsewhere);

    const fs::path source = scratch.path() / "report.txt";
    {
        std::ofstream stream(source, std::ios::binary | std::ios::trunc);
        stream << "contents";
    }
    const fs::path other_name = scratch.path() / "also-report.txt";
    std::error_code ec;
    fs::create_hard_link(source, other_name, ec);
    check(!ec, "a second name for the entry can be made");
    const fs::path target = tree.directory("to");

    OperationJournal journal;
    check(journal.move_into(source, target, fail_on_conflict).succeeded(),
          "the move across filesystems should succeed");

    const OperationRecord* record = journal.newest();
    check(record != nullptr && record->barrier == ReversalBarrier::HardLinksNotRestorable,
          "a crossing move of a shared entry is recorded as unreversible");
    check(!journal.can_undo(), "an unreversible record is never offered");
    check(journal.undo().status == UndoStatus::Barred, "the reversal is refused outright");
    check(fs::exists(target / "report.txt"), "the moved entry stays where the move put it");
    check(fs::exists(other_name), "the name that still holds the data is untouched");
}

struct Scenario {
    const char* name;
    void (*run)();
};

const Scenario scenarios[] = {
    {.name = "a completed move is recorded as reversible",
     .run = test_a_completed_move_is_recorded_as_reversible},
    {.name = "a failed operation is not recorded", .run = test_a_failed_operation_is_not_recorded},
    {.name = "history is bounded by capacity", .run = test_history_is_bounded_by_capacity},
    {.name = "a zero bound is raised to one", .run = test_a_zero_bound_is_raised_to_one},
    {.name = "forgetting and clearing reverse nothing",
     .run = test_forgetting_and_clearing_reverse_nothing},
    {.name = "an empty history reports nothing to reverse",
     .run = test_an_empty_history_reports_nothing_to_reverse},
    {.name = "a move is returned to where it came from",
     .run = test_a_move_is_returned_to_where_it_came_from},
    {.name = "a numbered move is returned under its original name",
     .run = test_a_numbered_move_is_returned_under_its_original_name},
    {.name = "a reversal refuses an occupied origin",
     .run = test_a_reversal_refuses_an_occupied_origin},
    {.name = "a reversal refuses a result that is no longer the result",
     .run = test_a_reversal_refuses_a_result_that_is_no_longer_the_result},
    {.name = "a move that discarded a destination can never be reversed",
     .run = test_a_move_that_discarded_a_destination_can_never_be_reversed},
    {.name = "a move that changed nothing is never reversed",
     .run = test_a_move_that_changed_nothing_is_never_reversed},
    {.name = "a rename is returned to its former name",
     .run = test_a_rename_is_returned_to_its_former_name},
    {.name = "a rename that discarded a destination can never be reversed",
     .run = test_a_rename_that_discarded_a_destination_can_never_be_reversed},
    {.name = "a rename to the same name is never reversed",
     .run = test_a_rename_to_the_same_name_is_never_reversed},
    {.name = "a trashed entry is restored and its record removed",
     .run = test_a_trashed_entry_is_restored_and_its_record_removed},
    {.name = "a trash reversal refuses an occupied origin",
     .run = test_a_trash_reversal_refuses_an_occupied_origin},
    {.name = "a trash reversal refuses once the trash no longer holds the entry",
     .run = test_a_trash_reversal_refuses_once_the_trash_no_longer_holds_the_entry},
    {.name = "a copy is undone by removing what it created",
     .run = test_a_copy_is_undone_by_removing_what_it_created},
    {.name = "a copied tree is undone whole", .run = test_a_copied_tree_is_undone_whole},
    {.name = "a copy that discarded a destination can never be reversed",
     .run = test_a_copy_that_discarded_a_destination_can_never_be_reversed},
    {.name = "a copy onto itself is never reversed",
     .run = test_a_copy_onto_itself_is_never_reversed},
    {.name = "a numbered copy removes only what it created",
     .run = test_a_numbered_copy_removes_only_what_it_created},
    {.name = "a copy is not removed once its tree has gained an entry",
     .run = test_a_copy_is_not_removed_once_its_tree_has_gained_an_entry},
    {.name = "a copy is not removed once it has been rewritten",
     .run = test_a_copy_is_not_removed_once_it_has_been_rewritten},
    {.name = "a copy is not removed once it has changed size",
     .run = test_a_copy_is_not_removed_once_it_has_changed_size},
    {.name = "a copied tree beyond the limit can never be reversed",
     .run = test_a_copied_tree_beyond_the_limit_can_never_be_reversed},
    {.name = "a barrier is not stepped over to reach an older record",
     .run = test_a_barrier_is_not_stepped_over_to_reach_an_older_record},
    {.name = "a reversal that fails its second step keeps its record and can be retried",
     .run = test_a_reversal_that_fails_its_second_step_keeps_its_record_and_can_be_retried},
    {.name = "a reversal refuses when the name it travels under is occupied",
     .run = test_a_reversal_refuses_when_the_name_it_travels_under_is_occupied},
    {.name = "a copy reversal reports a removal the filesystem refused",
     .run = test_a_copy_reversal_reports_a_removal_the_filesystem_refused},
    {.name = "a cross-filesystem move of a single name is reversible",
     .run = test_a_cross_filesystem_move_of_a_single_name_is_reversible},
    {.name = "a cross-filesystem move of a shared entry can never be reversed",
     .run = test_a_cross_filesystem_move_of_a_shared_entry_can_never_be_reversed},
};

/// The scenario count this suite is expected to run. A scenario removed from
/// the table, or left out of it, fails here rather than passing quietly.
constexpr std::size_t expected_scenarios = 33;

/// The two scenarios that need a second writable filesystem, and the one that
/// needs an unprivileged process, are the only ones allowed to decline.
constexpr std::size_t maximum_declines = 3;

} // namespace

int main() {
    // Redirect the desktop data directory into a synthetic temporary tree, so
    // the trash scenarios never touch a real desktop trash.
    const odysea::test::TemporaryTree data_home("journal_data_home");
    const fs::path redirected = data_home.directory("data_home");
    ::setenv("XDG_DATA_HOME", redirected.c_str(), 1);

    std::size_t ran = 0;
    for (const Scenario& scenario : scenarios) {
        scenario.run();
        ++ran;
    }

    check(std::size(scenarios) == expected_scenarios,
          "the scenario table holds every scenario the suite claims");
    check(ran == std::size(scenarios), "every listed scenario ran");
    check(decline_count() <= maximum_declines, "only the cross-filesystem scenarios may decline");
    std::fputs(("operation journal: " + std::to_string(ran) + " scenarios ran, " +
                std::to_string(decline_count()) + " declined\n")
                   .c_str(),
               stdout);
    return odysea::test::report("operation journal");
}

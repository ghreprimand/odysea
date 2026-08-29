#include "odysea/core/operation_journal.hpp"

#include "entry_metadata.hpp"
#include "file_operations_internal.hpp"
#include "operation_journal_internal.hpp"

#include <algorithm>
#include <ranges>
#include <tuple>
#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

using detail::EntryMetadata;

/// Whether anything at all occupies `path`, a broken symbolic link included.
bool path_present(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    return !error && fs::exists(status);
}

/// The absolute path of the entry `path` names.
///
/// A reversal needs the name of the entry as well as the directory holding it,
/// so a path written as a directory — with a trailing separator, or a trailing
/// "." — is reduced to the entry it refers to.
fs::path entry_path(const fs::path& path) {
    std::error_code error;
    fs::path resolved = fs::absolute(path, error);
    if (error) {
        resolved = path;
    }
    resolved = resolved.lexically_normal();
    if (resolved.has_relative_path() && resolved.filename().empty()) {
        resolved = resolved.parent_path();
    }
    return resolved;
}

/// Options that refuse to disturb anything already in the way. Every step of a
/// reversal uses these: a reversal that had to displace an entry to complete
/// would be a new operation rather than the undoing of an old one.
constexpr OperationOptions undisturbing{.conflict = ConflictPolicy::Fail};

UndoOutcome undo_status(UndoStatus status) {
    return UndoOutcome{.status = status, .barrier = {}, .error = {}, .restored_path = {}};
}

UndoOutcome undo_failure(const std::error_code& error) {
    // A step that refused because something was already in the way is reported
    // as an occupied origin rather than a generic failure: a caller can offer
    // to clear the way, which it cannot do for anything else.
    if (error == std::errc::file_exists) {
        return undo_status(UndoStatus::OriginOccupied);
    }
    return UndoOutcome{
        .status = UndoStatus::Failed, .barrier = {}, .error = error, .restored_path = {}};
}

/// Whether `before` and `after` describe one entry that never moved.
bool unchanged_entry(const EntryMetadata& before, const EntryMetadata& after) {
    return before.known && after.known && same_identity(before.identity, after.identity);
}

/// What every recorded operation is asked about its own result, in the order
/// the answers rule each other out.
struct Observation {
    /// The result could be examined once the operation had finished.
    bool result_known = false;
    /// The operation resolved to the entry it was given.
    bool nothing_changed = false;
    /// The destination the operation was expected to reach is the one it
    /// reached, so what was seen there beforehand describes the right path.
    bool prediction_held = false;
    /// Something occupied that destination before the operation ran.
    bool destination_occupied = false;
};

/// The barrier every kind of operation shares.
///
/// Each question is asked separately and answered by returning, rather than as
/// a chain of alternatives, because two of the answers are the same barrier
/// reached for different reasons and a chain would hide that they are distinct
/// questions.
ReversalBarrier shared_barrier(const Observation& observation) {
    if (!observation.result_known) {
        return ReversalBarrier::ResultNotIdentified;
    }
    if (observation.nothing_changed) {
        return ReversalBarrier::NothingChanged;
    }
    if (!observation.prediction_held) {
        // What occupied the destination beforehand was never observed, so
        // whether the operation created its result or replaced something is
        // not known.
        return ReversalBarrier::ResultNotIdentified;
    }
    if (observation.destination_occupied) {
        return ReversalBarrier::ReplacedEntryDiscarded;
    }
    return ReversalBarrier::None;
}

} // namespace

OperationJournal::OperationJournal(std::size_t capacity, std::size_t created_tree_limit)
    : capacity_(std::max<std::size_t>(capacity, 1)),
      created_tree_limit_(std::max<std::size_t>(created_tree_limit, 1)) {}

void OperationJournal::push(Slot slot) {
    slots_.push_back(std::move(slot));
    while (slots_.size() > capacity_) {
        slots_.pop_front();
    }
}

const OperationRecord& OperationJournal::at(std::size_t index) const {
    return slots_[slots_.size() - 1 - index].record;
}

const OperationRecord* OperationJournal::newest() const noexcept {
    if (slots_.empty()) {
        return nullptr;
    }
    return &slots_.back().record;
}

bool OperationJournal::can_undo() const noexcept {
    return !slots_.empty() && slots_.back().record.reversible();
}

bool OperationJournal::forget_newest() {
    if (slots_.empty()) {
        return false;
    }
    slots_.pop_back();
    return true;
}

void OperationJournal::clear() noexcept {
    slots_.clear();
}

OperationOutcome OperationJournal::copy_into(const fs::path& source,
                                             const fs::path& destination_directory,
                                             const OperationOptions& options) {
    const EntryMetadata before = detail::read_entry_metadata(source);
    const OperationOutcome predicted =
        resolve_destination(destination_directory, source.filename().string(), options);
    const bool predicted_occupied = predicted.succeeded() && path_present(predicted.destination);

    OperationOutcome outcome = core::copy_into(source, destination_directory, options);
    if (!outcome.succeeded()) {
        return outcome;
    }

    Slot slot;
    slot.record.kind = OperationKind::Copy;
    slot.record.original_path = entry_path(source);
    slot.record.result_path = entry_path(outcome.destination);

    const EntryMetadata after = detail::read_entry_metadata(outcome.destination);
    slot.verification.identity = after.identity;
    slot.verification.modified_seconds = after.modified_seconds;
    slot.verification.size = after.apparent_bytes;
    slot.verification.result_is_directory = after.known && detail::mode_is_directory(after.mode);

    // A copy leaves its source in place, so what says nothing changed is the
    // result being the source: removing it would destroy the entry copied.
    slot.record.barrier = shared_barrier(Observation{
        .result_known = after.known,
        .nothing_changed =
            unchanged_entry(before, after) || slot.record.result_path == slot.record.original_path,
        .prediction_held = predicted.succeeded() && predicted.destination == outcome.destination,
        .destination_occupied = predicted_occupied});

    if (slot.record.barrier == ReversalBarrier::None && slot.verification.result_is_directory) {
        using TreeScan = detail::JournalReversal::TreeScan;
        switch (detail::JournalReversal::collect_created(outcome.destination, created_tree_limit_,
                                                         slot.verification.created)) {
        case TreeScan::Complete:
            break;
        case TreeScan::TooLarge:
            slot.record.barrier = ReversalBarrier::CreatedTreeTooLarge;
            break;
        case TreeScan::Unreadable:
            slot.record.barrier = ReversalBarrier::ResultNotIdentified;
            break;
        }
    }

    push(std::move(slot));
    return outcome;
}

OperationOutcome OperationJournal::move_into(const fs::path& source,
                                             const fs::path& destination_directory,
                                             const OperationOptions& options) {
    const EntryMetadata before = detail::read_entry_metadata(source);
    const OperationOutcome predicted =
        resolve_destination(destination_directory, source.filename().string(), options);
    const bool predicted_occupied = predicted.succeeded() && path_present(predicted.destination);

    OperationOutcome outcome = core::move_into(source, destination_directory, options);
    if (!outcome.succeeded()) {
        return outcome;
    }

    Slot slot;
    slot.record.kind = OperationKind::Move;
    slot.record.original_path = entry_path(source);
    slot.record.result_path = entry_path(outcome.destination);

    const EntryMetadata after = detail::read_entry_metadata(outcome.destination);
    const EntryMetadata source_after = detail::read_entry_metadata(source);
    slot.verification.identity = after.identity;
    slot.verification.modified_seconds = after.modified_seconds;
    slot.verification.size = after.apparent_bytes;
    slot.verification.result_is_directory = after.known && detail::mode_is_directory(after.mode);

    // A move takes its source with it, so what says nothing changed is the
    // source still being where it was.
    slot.record.barrier = shared_barrier(Observation{
        .result_known = after.known,
        .nothing_changed = unchanged_entry(before, source_after) ||
                           slot.record.result_path == slot.record.original_path,
        .prediction_held = predicted.succeeded() && predicted.destination == outcome.destination,
        .destination_occupied = predicted_occupied});

    if (slot.record.barrier == ReversalBarrier::None && before.known &&
        before.identity.device != after.identity.device && before.link_count > 1) {
        // The data was copied to another filesystem and the original removed.
        // The names that shared the original still hold it; the entry that
        // arrived is a different one, and moving it back does not rejoin them.
        slot.record.barrier = ReversalBarrier::HardLinksNotRestorable;
    }

    push(std::move(slot));
    return outcome;
}

OperationOutcome OperationJournal::rename_entry(const fs::path& source, std::string_view new_name,
                                                const OperationOptions& options) {
    const EntryMetadata before = detail::read_entry_metadata(source);
    const fs::path parent = source.has_parent_path() ? source.parent_path() : fs::path(".");
    const OperationOutcome predicted = resolve_destination(parent, new_name, options);
    const bool predicted_occupied = predicted.succeeded() && path_present(predicted.destination);

    OperationOutcome outcome = core::rename_entry(source, new_name, options);
    if (!outcome.succeeded()) {
        return outcome;
    }

    Slot slot;
    slot.record.kind = OperationKind::Rename;
    slot.record.original_path = entry_path(source);
    slot.record.result_path = entry_path(outcome.destination);

    const EntryMetadata after = detail::read_entry_metadata(outcome.destination);
    const EntryMetadata source_after = detail::read_entry_metadata(source);
    slot.verification.identity = after.identity;
    slot.verification.modified_seconds = after.modified_seconds;
    slot.verification.size = after.apparent_bytes;
    slot.verification.result_is_directory = after.known && detail::mode_is_directory(after.mode);

    // Both names live in one directory, so a rename cannot cross a filesystem
    // and nothing here answers to HardLinksNotRestorable.
    slot.record.barrier = shared_barrier(Observation{
        .result_known = after.known,
        .nothing_changed = unchanged_entry(before, source_after) ||
                           slot.record.result_path == slot.record.original_path,
        .prediction_held = predicted.succeeded() && predicted.destination == outcome.destination,
        .destination_occupied = predicted_occupied});

    push(std::move(slot));
    return outcome;
}

TrashOutcome OperationJournal::move_to_trash(const fs::path& source) {
    TrashOutcome outcome = core::move_to_trash(source);
    if (!outcome.succeeded()) {
        return outcome;
    }

    Slot slot;
    slot.record.kind = OperationKind::Trash;
    slot.record.original_path = entry_path(source);
    slot.record.result_path = entry_path(outcome.trashed_path);
    slot.record.trash_record_path = entry_path(outcome.info_path);

    const EntryMetadata after = detail::read_entry_metadata(outcome.trashed_path);
    slot.verification.identity = after.identity;
    slot.verification.modified_seconds = after.modified_seconds;
    slot.verification.size = after.apparent_bytes;
    slot.verification.result_is_directory = after.known && detail::mode_is_directory(after.mode);

    // The trash serving an entry is on the entry's own filesystem, so trashing
    // is a rename: the entry that lands in the trash is the entry that was
    // there, with the names that shared it intact. Only a result that cannot
    // be identified bars the reversal.
    if (!after.known) {
        slot.record.barrier = ReversalBarrier::ResultNotIdentified;
    }

    push(std::move(slot));
    return outcome;
}

UndoOutcome OperationJournal::undo() {
    return detail::JournalReversal::undo_using(*this, &detail::rename_with_filesystem);
}

namespace detail {

JournalReversal::TreeScan
JournalReversal::collect_created(const fs::path& root, std::size_t limit,
                                 std::vector<OperationJournal::CreatedEntry>& collected) {
    std::error_code error;
    fs::recursive_directory_iterator iterator(root, fs::directory_options::none, error);
    if (error) {
        return TreeScan::Unreadable;
    }
    const fs::recursive_directory_iterator end;
    while (iterator != end) {
        const EntryMetadata metadata = read_entry_metadata(iterator->path());
        if (!metadata.known) {
            return TreeScan::Unreadable;
        }
        if (collected.size() >= limit) {
            return TreeScan::TooLarge;
        }
        collected.push_back(OperationJournal::CreatedEntry{
            .identity = metadata.identity, .modified_seconds = metadata.modified_seconds});
        iterator.increment(error);
        if (error) {
            return TreeScan::Unreadable;
        }
    }
    return TreeScan::Complete;
}

bool JournalReversal::same_tree(std::vector<OperationJournal::CreatedEntry> recorded,
                                std::vector<OperationJournal::CreatedEntry> current) {
    if (recorded.size() != current.size()) {
        return false;
    }

    // A total order over recorded entries, so two records of one tree compare
    // equal whatever order the filesystem reported them in.
    const auto key = [](const OperationJournal::CreatedEntry& entry) {
        return std::tuple(entry.identity.device, entry.identity.inode, entry.identity.birth_seconds,
                          entry.identity.birth_nanoseconds, entry.identity.birth_known,
                          entry.modified_seconds);
    };
    const auto order = [&key](const OperationJournal::CreatedEntry& left,
                              const OperationJournal::CreatedEntry& right) {
        return key(left) < key(right);
    };
    std::ranges::sort(recorded, order);
    std::ranges::sort(current, order);
    return std::ranges::equal(
        recorded, current,
        [&key](const OperationJournal::CreatedEntry& left,
               const OperationJournal::CreatedEntry& right) { return key(left) == key(right); });
}

UndoOutcome JournalReversal::undo_copy(const OperationJournal::Slot& slot) {
    const EntryMetadata now = read_entry_metadata(slot.record.result_path);
    if (now.modified_seconds != slot.verification.modified_seconds ||
        now.apparent_bytes != slot.verification.size) {
        return undo_status(UndoStatus::ResultChanged);
    }

    if (slot.verification.result_is_directory) {
        std::vector<OperationJournal::CreatedEntry> current;
        // One more than was recorded, so a tree that has grown without bound is
        // not walked to its end. The bound and the comparison below both refuse
        // a grown tree; the bound decides only which of them reports it.
        const TreeScan scan =
            collect_created(slot.record.result_path, slot.verification.created.size() + 1, current);
        if (scan != TreeScan::Complete || !same_tree(slot.verification.created, current)) {
            return undo_status(UndoStatus::ResultChanged);
        }
    }

    std::error_code error;
    fs::remove_all(slot.record.result_path, error);
    if (error) {
        return undo_failure(error);
    }
    return undo_status(UndoStatus::Reversed);
}

UndoOutcome JournalReversal::undo_relocation(OperationJournal::Slot& slot,
                                             const RenameStep& rename_step) {
    const fs::path& from = slot.record.result_path;
    const fs::path& to = slot.record.original_path;

    // Nothing here checks first whether the origin is free. Every step below
    // refuses a name that is taken and says so, and a separate look beforehand
    // would answer the same question a moment earlier without being able to
    // change any answer.
    fs::path landed;
    if (from.parent_path() == to.parent_path()) {
        // One directory, so one rename, which the filesystem does atomically.
        const OperationOutcome outcome =
            rename_entry_using(from, to.filename().string(), undisturbing, rename_step);
        if (!outcome.succeeded()) {
            return undo_failure(outcome.error);
        }
        landed = outcome.destination;
    } else {
        // Two directories. The entry is brought home under whatever name it
        // currently has and renamed afterwards, so a failure part-way leaves it
        // nearer its origin rather than further from it. The name it currently
        // has must therefore be free in the origin directory as well as the
        // name it is going back to.
        const OperationOutcome moved =
            move_into_using(from, to.parent_path(), undisturbing, rename_step);
        if (!moved.succeeded()) {
            return undo_failure(moved.error);
        }
        landed = moved.destination;
        if (landed != to) {
            const OperationOutcome renamed =
                rename_entry_using(landed, to.filename().string(), undisturbing, rename_step);
            if (!renamed.succeeded()) {
                // The first step happened. Point the record at where the entry
                // is now, so a second attempt starts from where it is rather
                // than from a path that no longer holds anything.
                slot.record.result_path = landed;
                return undo_failure(renamed.error);
            }
            landed = renamed.destination;
        }
    }

    UndoOutcome outcome = undo_status(UndoStatus::Reversed);
    outcome.restored_path = landed;
    if (slot.record.kind == OperationKind::Trash && !slot.record.trash_record_path.empty()) {
        // The entry is out of the trash, so its record describes nothing. The
        // record is removed second on purpose: one removed before a restore
        // that then failed would leave the trash holding an entry whose origin
        // nothing remembers. A failure to remove it is reported without
        // disputing that the entry is back.
        std::error_code error;
        fs::remove(slot.record.trash_record_path, error);
        if (error) {
            outcome.error = error;
        }
    }
    return outcome;
}

UndoOutcome JournalReversal::undo_using(OperationJournal& journal, const RenameStep& rename_step) {
    if (journal.slots_.empty()) {
        return undo_status(UndoStatus::HistoryEmpty);
    }

    OperationJournal::Slot& slot = journal.slots_.back();
    if (!slot.record.reversible()) {
        UndoOutcome outcome = undo_status(UndoStatus::Barred);
        outcome.barrier = slot.record.barrier;
        return outcome;
    }

    // Every reversal begins the same way: what the operation produced has to
    // still be that entry. Identity rather than the path, because a path can
    // be occupied by something else that looks exactly like it.
    const EntryMetadata now = read_entry_metadata(slot.record.result_path);
    if (!now.known || !same_identity(now.identity, slot.verification.identity)) {
        return undo_status(UndoStatus::ResultChanged);
    }

    const UndoOutcome outcome = slot.record.kind == OperationKind::Copy
                                    ? undo_copy(slot)
                                    : undo_relocation(slot, rename_step);
    if (outcome.succeeded()) {
        journal.slots_.pop_back();
    }
    return outcome;
}

} // namespace detail
} // namespace odysea::core

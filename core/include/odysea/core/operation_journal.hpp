// OdySea core: the reversible operation journal.
//
// Toolkit-agnostic. No Qt, no GUI types. Records completed filesystem
// mutations and reverses the most recent one.
//
// The journal performs the operations itself rather than being told about
// them afterwards. That is the only way it can know what it needs to know: it
// observes the destination before the operation runs and the result
// immediately after, so it can tell an entry the operation created from one
// that was already there. A caller that reports its own operations could
// report anything, and the difference between those two cases is the whole
// safety argument for reversing a copy.
//
// A reversal never guesses. Every record carries the identity of what the
// operation produced, and a reversal that cannot confirm the result is still
// what the operation produced refuses and changes nothing. Refusing is the
// designed answer; a reversal that quietly overwrites newer data would be
// worse than no reversal at all.
//
// What cannot be reversed is decided when the operation is recorded, not when
// a reversal is attempted, and it is carried in the record as a barrier. An
// operation that discarded an entry it replaced can never be reversed, and it
// is recorded as such the moment it completes, so it is never offered as
// reversible and then refused.
//
// What a reversal checks, and what it cannot check. Returning an entry to
// where it came from requires the entry to still be the entry that moved, and
// the place it is going back to to be free. Removing what a copy created is
// held to a stricter test, because it destroys: every entry the copy made must
// still carry the identity, the modification time, and the size it had when
// the copy finished. That detects an entry added, removed, or written since,
// and it is applied to the root of the result and to every entry beneath it
// alike, because a reversal removes the whole tree and what protects an entry
// cannot depend on how deep in the copy it sits.
//
// It does not detect a rewrite that left both the modification time and the
// size unchanged, and modification times are compared in whole seconds, so a
// same-size rewrite within the same second as the copy is invisible to it.
// Nothing available to a filesystem consumer closes that window; a reversal is
// a convenience over a filesystem that other programs share, not a
// transaction.
//
// Identity carries one further limit, and it is recorded here because this is
// where its consequence is destructive rather than merely confusing. An
// identity is only as distinct as the filesystem makes it: where no creation
// time is reported, identity degrades to a device and inode pair, and such a
// pair can be reissued to an unrelated entry once the original is gone. A
// reversal that matched a recycled pair would remove an entry the operation
// never created. The modification time and size must also match for that to
// happen, which makes it narrow, but narrow is not impossible and the cost is
// data.
#pragma once

#include "odysea/core/directory_model.hpp"
#include "odysea/core/file_operations.hpp"
#include "odysea/core/transfer.hpp"
#include "odysea/core/trash.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

namespace odysea::core {

namespace detail {
struct JournalReversal;
} // namespace detail

/// Which mutation a record describes.
enum class OperationKind : std::uint8_t {
    Copy,
    Move,
    Rename,
    Trash,
};

/// Why a completed operation can never be reversed.
///
/// A barrier is a property of what the operation did, so it is settled when
/// the record is made and never changes afterwards. It is not a failure: the
/// operation itself succeeded. It says that undoing it would either destroy
/// something or restore something less than what was there.
enum class ReversalBarrier : std::uint8_t {
    /// No barrier. The operation can be reversed if its result is still
    /// intact when a reversal is attempted.
    None,
    /// The operation resolved to the entry it was given and changed nothing.
    ///
    /// Copying an entry over itself, renaming it to the name it already has,
    /// and moving it into the directory it already occupies all report success
    /// without touching the filesystem. There is no result to remove and no
    /// earlier state to restore, and treating the source as something the
    /// operation created would delete the only copy.
    NothingChanged,
    /// The operation replaced an entry that was discarded once the
    /// replacement was in place.
    ///
    /// The replacement can be put back where it came from, but what it
    /// replaced is gone, so the reversal would restore half of a state the
    /// filesystem never held. The journal refuses the whole reversal rather
    /// than performing the half it can.
    ReplacedEntryDiscarded,
    /// The result could not be identified when the record was made.
    ///
    /// Without the identity of what the operation produced, a later reversal
    /// could not tell that entry from a different one that had since taken
    /// its place.
    ResultNotIdentified,
    /// A copy created more entries than the journal will remember.
    ///
    /// Reversing a copy removes what the copy created, and that is only safe
    /// while the journal can confirm every one of those entries is still the
    /// entry the copy made. The record of that is bounded, and a copy larger
    /// than the bound is recorded as unreversible rather than reversed
    /// against a partial record.
    CreatedTreeTooLarge,
    /// A move crossed a filesystem boundary and the entry had more than one
    /// name.
    ///
    /// A move between filesystems copies the data and removes the original,
    /// so the entry that arrives is a new one. When the original was the only
    /// name for its data, moving it back restores it. When other names shared
    /// that data, they kept it, and nothing the journal can do makes the
    /// returned entry the same entry those names refer to again.
    HardLinksNotRestorable,
};

/// One completed operation.
///
/// The paths are absolute. A record describes what happened, not what is
/// currently true: whether the result is still intact is decided when a
/// reversal is attempted.
struct OperationRecord {
    OperationKind kind = OperationKind::Copy;
    /// Where the entry was before the operation. For a copy, the entry that
    /// was copied, which the copy left in place.
    std::filesystem::path original_path;
    /// What the operation produced: the moved, renamed, or trashed entry, or
    /// the entry a copy created.
    std::filesystem::path result_path;
    /// The trash record describing where a trashed entry came from. Empty for
    /// every other kind.
    std::filesystem::path trash_record_path;
    /// Why this operation can never be reversed, or None.
    ReversalBarrier barrier = ReversalBarrier::None;

    /// Whether a reversal may be attempted at all. A record without a barrier
    /// can still refuse at the time of the reversal, because the result may
    /// have changed since.
    [[nodiscard]] bool reversible() const noexcept { return barrier == ReversalBarrier::None; }
};

/// What an attempted reversal did.
enum class UndoStatus : std::uint8_t {
    /// The operation was reversed and its record removed from the history.
    Reversed,
    /// There is no record to reverse.
    HistoryEmpty,
    /// The newest record carries a barrier and can never be reversed.
    ///
    /// The history is left alone rather than skipping past it to an older
    /// record: reversing an older operation while a newer one still stands
    /// would produce a state the filesystem never held.
    Barred,
    /// What the operation produced is no longer what it produced. Nothing was
    /// changed.
    ResultChanged,
    /// The place the entry would return to is occupied. Nothing was changed.
    OriginOccupied,
    /// The reversal was attempted and the filesystem refused a step of it.
    ///
    /// A reversal is at most two relocations, and each of them either happens
    /// or does not. When the first succeeds and the second fails the entry is
    /// left where the first put it, the record is updated to point at it, and
    /// the record stays in the history so the reversal can be tried again.
    Failed,
};

/// The result of one reversal attempt.
struct UndoOutcome {
    UndoStatus status = UndoStatus::HistoryEmpty;
    /// Why the record can never be reversed. Meaningful for Barred.
    ReversalBarrier barrier = ReversalBarrier::None;
    /// What the filesystem reported for a Failed reversal.
    ///
    /// Also set on a reversed trash whose trash record could not be removed
    /// afterwards. The entry is back either way, so that is reported here
    /// rather than by contradicting the status.
    std::error_code error;
    /// Where the entry ended up. Set when a reversal returned an entry, and
    /// left empty by the reversal of a copy, which returns nothing and instead
    /// removes what the copy created.
    std::filesystem::path restored_path;

    [[nodiscard]] bool succeeded() const noexcept { return status == UndoStatus::Reversed; }
};

/// A bounded history of completed operations, newest first.
///
/// Not thread-safe: one journal belongs to one thread, the same way the
/// operations it records are issued from one place.
class OperationJournal {
  public:
    /// How many operations are remembered before the oldest is forgotten.
    static constexpr std::size_t default_capacity = 100;
    /// How many entries a copied directory may contain and still be
    /// reversible. Beyond this the copy is recorded with
    /// ReversalBarrier::CreatedTreeTooLarge.
    static constexpr std::size_t default_created_tree_limit = 4096;

    /// A capacity or a tree limit of zero would make the journal silently
    /// useless, so both are raised to one.
    explicit OperationJournal(std::size_t capacity = default_capacity,
                              std::size_t created_tree_limit = default_created_tree_limit);

    /// Copy `source` into `destination_directory` and record the result.
    ///
    /// Reversing a copy removes exactly the entry the copy created. A copy
    /// that replaced an existing entry, or that resolved to the source
    /// itself, is recorded with the barrier that says so.
    [[nodiscard]] OperationOutcome copy_into(const std::filesystem::path& source,
                                             const std::filesystem::path& destination_directory,
                                             const OperationOptions& options);

    /// The same copy, reported and controllable.
    ///
    /// A cancelled or failed transfer installs nothing, so nothing is
    /// recorded: the history holds completed operations only, and there is no
    /// state in which a reversal would undo part of one.
    [[nodiscard]] OperationOutcome copy_into(const std::filesystem::path& source,
                                             const std::filesystem::path& destination_directory,
                                             const TransferOptions& transfer);

    /// Move `source` into `destination_directory` and record the result.
    ///
    /// Reversing a move returns the entry to the path it came from, under the
    /// name it had, which is not always the name it now has: a move that
    /// resolved a collision by numbering the name still reverses to the
    /// original name.
    [[nodiscard]] OperationOutcome move_into(const std::filesystem::path& source,
                                             const std::filesystem::path& destination_directory,
                                             const OperationOptions& options);

    /// The same move, reported and controllable. Records nothing when it does
    /// not complete, for the reason given on the copy above.
    [[nodiscard]] OperationOutcome move_into(const std::filesystem::path& source,
                                             const std::filesystem::path& destination_directory,
                                             const TransferOptions& transfer);

    /// Rename `source` to `new_name` and record the result.
    [[nodiscard]] OperationOutcome rename_entry(const std::filesystem::path& source,
                                                std::string_view new_name,
                                                const OperationOptions& options);

    /// Move `source` to the trash and record the result.
    ///
    /// Reversing a trash returns the entry from the trash to the path it came
    /// from and removes the trash record. It depends on the trash still
    /// holding the entry: emptying the trash, or restoring the entry by some
    /// other means, makes the record refuse rather than act.
    [[nodiscard]] TrashOutcome move_to_trash(const std::filesystem::path& source);

    /// How many records are held.
    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }
    [[nodiscard]] bool empty() const noexcept { return slots_.empty(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t created_tree_limit() const noexcept { return created_tree_limit_; }

    /// Record `index` counting back from the newest, which is index zero.
    /// Behaviour is undefined for an index at or beyond size().
    [[nodiscard]] const OperationRecord& at(std::size_t index) const;

    /// The newest record, or nullptr when the history is empty.
    [[nodiscard]] const OperationRecord* newest() const noexcept;

    /// Whether a reversal may be attempted: there is a record and it carries
    /// no barrier. A caller that offers an undo action asks this, so an
    /// operation that can never be reversed is never offered as one.
    [[nodiscard]] bool can_undo() const noexcept;

    /// Reverse the newest operation.
    [[nodiscard]] UndoOutcome undo();

    /// Drop the newest record without reversing anything.
    ///
    /// The one way past a barrier. A record that can never be reversed would
    /// otherwise block every older record forever, because reversing them out
    /// of order is not offered. Discarding is a decision about data that
    /// cannot be recovered, so it is never taken by the journal itself.
    /// Reports whether there was a record to drop.
    bool forget_newest();

    /// Forget every record. Reverses nothing.
    void clear() noexcept;

  private:
    friend struct detail::JournalReversal;

    /// One entry of a copied tree, as the copy left it.
    ///
    /// Carries everything the root of a result carries, because a reversal
    /// removes the whole tree and every entry in it is equally destroyed. An
    /// entry recorded with less than the root would be protected with less
    /// than the root, and what protects a file cannot depend on how deep in a
    /// copy it happens to sit.
    struct CreatedEntry {
        EntryIdentity identity;
        std::int64_t modified_seconds = 0;
        std::uintmax_t size = 0;
    };

    /// What a reversal checks before it acts.
    struct Verification {
        EntryIdentity identity;
        std::int64_t modified_seconds = 0;
        std::uintmax_t size = 0;
        bool result_is_directory = false;
        /// Everything below the root of a copied tree. Empty for every other
        /// kind, and for a copy of a single entry.
        std::vector<CreatedEntry> created;
    };

    struct Slot {
        OperationRecord record;
        Verification verification;
    };

    void push(Slot slot);

    std::deque<Slot> slots_;
    std::size_t capacity_ = default_capacity;
    std::size_t created_tree_limit_ = default_created_tree_limit;
};

} // namespace odysea::core

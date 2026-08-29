// Internal seam for the operation journal. Not part of the public API and not
// installed: only operation_journal.cpp and the headless tests include it.
//
// A reversal across two directories is two relocations, and the interesting
// behaviour is what happens when the second one fails after the first has
// already happened. That cannot be provoked on a real filesystem without
// privileged control of the mount, so the reversal routes its relocations
// through the same injectable step the mutation primitives use. Failing one
// step by name makes the partial reversal reachable, so what the journal does
// with it can be asserted rather than assumed.
#pragma once

#include "file_operations_internal.hpp"

#include "odysea/core/operation_journal.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace odysea::core::detail {

/// The journal's reversal, and the recording step that reads what a copy
/// created.
///
/// The journal keeps what it remembers about a result private, so everything
/// that reads or writes that state is gathered behind one friendship here
/// rather than spread across several.
struct JournalReversal {
    /// How completely a copied tree could be recorded.
    enum class TreeScan : std::uint8_t {
        /// Every entry below the root was recorded.
        Complete,
        /// The tree holds more entries than the caller allowed.
        TooLarge,
        /// Some part of the tree could not be examined.
        Unreadable,
    };

    /// Record every entry below `root`, refusing rather than truncating.
    [[nodiscard]] static TreeScan
    collect_created(const std::filesystem::path& root, std::size_t limit,
                    std::vector<OperationJournal::CreatedEntry>& collected);

    /// Whether two records describe the same set of entries, each unchanged.
    /// Order is not significant: a directory may be read in any order.
    [[nodiscard]] static bool same_tree(std::vector<OperationJournal::CreatedEntry> recorded,
                                        std::vector<OperationJournal::CreatedEntry> current);

    /// Remove what a copy created, once every part of it is confirmed to still
    /// be what the copy made.
    [[nodiscard]] static UndoOutcome undo_copy(const OperationJournal::Slot& slot);

    /// Return a moved, renamed, or trashed entry to the path it came from.
    [[nodiscard]] static UndoOutcome undo_relocation(OperationJournal::Slot& slot,
                                                     const RenameStep& rename_step);

    /// OperationJournal::undo with the relocation step supplied by the caller.
    [[nodiscard]] static UndoOutcome undo_using(OperationJournal& journal,
                                                const RenameStep& rename_step);
};

} // namespace odysea::core::detail

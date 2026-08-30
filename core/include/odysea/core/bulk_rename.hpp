// OdySea core: bulk rename planning and application.
//
// Toolkit-agnostic. No Qt, no GUI types.
//
// Renaming many entries at once is two separate things, and keeping them
// separate is the whole safety argument. Planning answers what every new name
// would be and what is wrong with any of them, and it does that without
// writing anything: a caller renders the plan as a live preview and the user
// sees the outcome before the filesystem is touched. Application takes a plan
// and performs it.
//
// A plan that reports a problem is never applied. The check that matters is
// not the one a user notices — two entries given the same new name is obvious
// in a preview — but the ones a preview cannot show: a name already held by an
// entry outside the batch, and a name held by another member of the same batch.
//
// Two separate guarantees keep the second of those safe, and they are worth
// naming separately because each covers what the other does not. Every rename
// this engine issues refuses an occupied name rather than replacing it, so a
// batch performed in a bad order stops rather than destroying anything; the
// filesystem primitive underneath replaces by default, so that refusal is a
// decision and not a property of renaming. Sequencing is what then lets such a
// batch finish at all: steps are ordered so that nothing is asked to take a
// name until the entry holding it has left.
//
// Shifting a run of names along by one is an ordinary thing to want, and it is
// exactly that shape. Planning marks such a step as deferred rather than
// refusing it; only a name held by something that is not leaving blocks the
// batch.
//
// What application does not promise. A batch is not a transaction. The plan is
// checked as a whole and refused as a whole, so a batch never begins against a
// known collision, but the renames themselves happen one at a time against a
// filesystem other programs share. A step can still fail partway through, and
// when it does, the renames already performed have happened. They are reported
// exactly, oldest first, rather than being described by an absolute the
// filesystem cannot hold up.
#pragma once

#include "odysea/core/directory_model.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace odysea::core {

/// How the pattern is interpreted.
enum class RenameMatch : std::uint8_t {
    /// The pattern is a plain substring. The replacement is used exactly as
    /// written: a replacement containing "$1" produces the characters "$1",
    /// because there are no capture groups to refer to.
    Literal,
    /// The pattern is an ECMAScript regular expression. The replacement may
    /// refer to capture groups as "$1" through "$9", to the whole match as
    /// "$&", and to a literal dollar sign as "$$".
    Regex,
};

/// How many occurrences within one name are replaced.
enum class RenameScope : std::uint8_t {
    First,
    All,
};

/// What to rewrite, and what to rewrite it with.
struct RenameRule {
    std::string pattern;
    std::string replacement;
    RenameMatch match = RenameMatch::Literal;
    RenameScope scope = RenameScope::All;
    /// Whether the pattern distinguishes case while matching. It never
    /// changes the case of anything it does not replace.
    bool case_sensitive = true;
};

/// Why one proposed name cannot be used.
///
/// A step carries at most one of these. Where a name is wrong in more than one
/// way the first that applies is reported, because a caller shows the user a
/// reason rather than a list of reasons.
enum class RenameProblem : std::uint8_t {
    /// Nothing is wrong with this step.
    None,
    /// Nothing stands at the supplied path. A rule cannot be applied to an
    /// entry that is not there, and a batch that started anyway would report
    /// the failure partway through instead of before writing anything.
    SourceMissing,
    /// The rule could not be applied to this name. A regular expression that
    /// compiled can still fail on a particular input, by exhausting the
    /// matcher rather than by being malformed.
    RuleNotApplied,
    /// The rule produced an empty name.
    EmptyName,
    /// The proposed name contains a path separator, so it names a location
    /// rather than an entry. Renaming does not move entries between
    /// directories.
    NameHasSeparator,
    /// The proposed name is "." or "..".
    ReservedName,
    /// The proposed name is longer than the filesystem accepts for one entry.
    NameTooLong,
    /// The same entry was supplied more than once in the batch. The first
    /// occurrence is planned and every later one carries this, so a rename is
    /// never performed twice against one entry.
    DuplicateSource,
    /// Another entry in the batch proposes this same name.
    DuplicateTarget,
    /// The name is already taken by something that is not leaving: an entry
    /// that is not in the batch, or one that is in the batch but whose name
    /// the rule did not change.
    TargetExists,
    /// The name is not present in the directory as spelled, but the
    /// filesystem resolves it to an entry anyway, which it does when it does
    /// not distinguish case. Renaming to it would overwrite that entry rather
    /// than create a new one.
    ///
    /// Reported separately from TargetExists because a preview cannot explain
    /// it otherwise: the name the user is being warned about is not visible in
    /// the listing they are looking at.
    CaseOnlyTargetExists,
};

/// When a step may be performed relative to the rest of the batch.
enum class RenameSequencing : std::uint8_t {
    /// The target is free before the batch starts.
    Immediate,
    /// The target is the current name of another entry in this batch that is
    /// itself being renamed away. The step is safe, but only after that entry
    /// has left.
    Deferred,
};

/// One entry's place in the plan, whether or not its name changes.
///
/// Unchanged entries are kept so a caller can render every supplied entry in
/// one preview, and because an entry whose name did not change still occupies
/// that name against the rest of the batch.
struct RenamePlanStep {
    /// Absolute path as the entry stood when the plan was made.
    std::filesystem::path source;
    /// Identity of that entry when the plan was made. An application refuses
    /// the whole batch when a source no longer resolves to the entry the plan
    /// was built from, so a rule previewed against one set of entries is never
    /// applied to a different one.
    EntryIdentity source_identity;
    std::string current_name;
    /// What the rule produced. Equal to `current_name` when the rule matched
    /// nothing, and left as the rule produced it when the step has a problem,
    /// so a preview can show what was proposed and why it was refused.
    std::string proposed_name;
    RenameProblem problem = RenameProblem::None;
    RenameSequencing sequencing = RenameSequencing::Immediate;

    [[nodiscard]] bool blocked() const noexcept { return problem != RenameProblem::None; }
    /// Whether performing this step would change anything.
    [[nodiscard]] bool changes_name() const noexcept { return proposed_name != current_name; }
};

/// The complete old-to-new mapping for one batch.
struct RenamePlan {
    /// The rule the plan was made from, carried so the plan can be rechecked
    /// against the filesystem at the moment it is applied rather than trusted
    /// to still describe it.
    RenameRule rule;
    /// One step per supplied entry, in the order supplied.
    std::vector<RenamePlanStep> steps;
    /// Set when the rule itself is unusable, in which case no step was
    /// computed. An empty pattern and a malformed regular expression are both
    /// reported as std::errc::invalid_argument.
    std::error_code rule_error;

    /// Whether applying this plan is allowed to begin.
    [[nodiscard]] bool usable() const noexcept;
    /// How many steps carry a problem.
    [[nodiscard]] std::size_t blocked_count() const noexcept;
    /// How many steps would change a name.
    [[nodiscard]] std::size_t change_count() const noexcept;
    /// Whether any step must be sequenced behind another. Such a batch may
    /// require an entry to pass through a working name, which has a
    /// consequence for reversal recorded on the journal entry point.
    [[nodiscard]] bool needs_sequencing() const noexcept;
    /// Whether the batch contains a closed cycle of names, which cannot be
    /// performed by renames alone and requires a working name to break.
    [[nodiscard]] bool needs_working_name() const noexcept;
};

/// Produce the full mapping for `entries` under `rule` without writing
/// anything.
///
/// The filesystem is read: whether a proposed name is already taken cannot be
/// answered any other way. Nothing is created, removed, or renamed.
///
/// Entries may span directories. Names are compared within a directory, so two
/// entries in different directories proposing the same name do not collide.
[[nodiscard]] RenamePlan plan_bulk_rename(const std::vector<std::filesystem::path>& entries,
                                          const RenameRule& rule);

/// What an attempted application did.
enum class RenameApplyStatus : std::uint8_t {
    /// Every step that changes a name was performed.
    Applied,
    /// The plan changes no name. Nothing was written.
    NothingToDo,
    /// The plan carries a rule error or a blocked step. Nothing was written.
    PlanRejected,
    /// The filesystem no longer matches the plan: an entry has moved or
    /// disappeared, or a name the plan found free has since been taken.
    /// Nothing was written.
    PlanStale,
    /// A step failed after earlier steps had been performed. What was
    /// performed is reported in `applied`.
    Interrupted,
};

/// One rename that completed.
struct AppliedRename {
    /// Where the entry was when the batch started.
    std::filesystem::path source;
    /// Where it is now.
    std::filesystem::path result;
};

/// The result of one application.
struct RenameApplication {
    RenameApplyStatus status = RenameApplyStatus::NothingToDo;
    /// Every rename that completed, oldest first. A caller that has to explain
    /// a partial batch reads this rather than inferring it from the plan.
    std::vector<AppliedRename> applied;
    /// Index into the plan's steps of the step that failed. Meaningful for
    /// Interrupted only.
    std::size_t failed_step = 0;
    /// What the filesystem reported for an Interrupted application.
    std::error_code error;
    /// Set when a batch was interrupted while an entry was standing under a
    /// working name and could not be put back. The entry holds its data and
    /// is recognizable through classify_working_entry; it is reported rather
    /// than removed.
    std::filesystem::path stranded_path;

    [[nodiscard]] bool succeeded() const noexcept {
        return status == RenameApplyStatus::Applied || status == RenameApplyStatus::NothingToDo;
    }
};

/// Perform `plan`.
///
/// The plan is rechecked against the filesystem first, from the sources and
/// the rule the plan carries. Anything the recheck disagrees with stops the
/// batch before a single rename is issued, so a plan that was previewed
/// minutes ago is not applied to a directory that has changed underneath it.
///
/// Steps are sequenced so that no entry is ever written over one that has not
/// yet left. A closed cycle of names is broken by moving one entry to a
/// working name; that entry is renamed to its final name at the end of the
/// batch.
[[nodiscard]] RenameApplication apply_bulk_rename(const RenamePlan& plan);

} // namespace odysea::core

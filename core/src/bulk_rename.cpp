#include "bulk_rename_internal.hpp"

#include "entry_metadata.hpp"
#include "file_operations_internal.hpp"
#include "odysea/core/file_operations.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <optional>
#include <regex>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace odysea::core {
namespace fs = std::filesystem;

namespace {

std::error_code make_error(std::errc code) {
    return std::make_error_code(code);
}

/// The longest name any common Linux filesystem accepts for one entry. Used
/// when the filesystem declines to say, which is safer than accepting a name
/// the filesystem will later refuse.
constexpr std::size_t fallback_name_limit = 255;

char lowered(char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

bool same_char(char left, char right, bool case_sensitive) {
    return case_sensitive ? left == right : lowered(left) == lowered(right);
}

/// Find `needle` in `haystack` at or after `from`, honouring case sensitivity.
std::size_t find_literal(const std::string& haystack, const std::string& needle, std::size_t from,
                         bool case_sensitive) {
    if (case_sensitive) {
        return haystack.find(needle, from);
    }
    if (needle.size() > haystack.size()) {
        return std::string::npos;
    }
    const auto found =
        std::search(haystack.begin() + static_cast<std::string::difference_type>(from),
                    haystack.end(), needle.begin(), needle.end(),
                    [](char left, char right) { return same_char(left, right, false); });
    if (found == haystack.end()) {
        return std::string::npos;
    }
    return static_cast<std::size_t>(found - haystack.begin());
}

/// Substitute a plain substring.
///
/// The replacement is copied exactly. A literal rule has no capture groups, so
/// a replacement containing "$1" has to produce those two characters rather
/// than being read as a reference to something that does not exist.
std::string replace_literal(const std::string& name, const RenameRule& rule) {
    std::string result;
    std::size_t position = 0;
    while (position <= name.size()) {
        const std::size_t found = find_literal(name, rule.pattern, position, rule.case_sensitive);
        if (found == std::string::npos) {
            break;
        }
        result.append(name, position, found - position);
        result.append(rule.replacement);
        position = found + rule.pattern.size();
        if (rule.scope == RenameScope::First) {
            break;
        }
    }
    result.append(name, position, name.size() - position);
    return result;
}

/// Apply the rule to one name. Reports false when the rule could not be
/// applied to this particular name, which leaves the name unchanged.
bool apply_rule(const RenameRule& rule, const std::regex* compiled, const std::string& name,
                std::string& out) {
    if (rule.match == RenameMatch::Literal) {
        out = replace_literal(name, rule);
        return true;
    }
    if (compiled == nullptr) {
        return false;
    }
    auto flags = std::regex_constants::format_default;
    if (rule.scope == RenameScope::First) {
        flags |= std::regex_constants::format_first_only;
    }
    try {
        out = std::regex_replace(name, *compiled, rule.replacement, flags);
    } catch (const std::regex_error&) {
        // A pattern that compiles can still exhaust the matcher on a
        // particular input. That is a property of this name, not of the rule,
        // so it refuses this step rather than the whole batch.
        return false;
    }
    return true;
}

/// The problems a name carries on its own, before anything else in the batch
/// or on the filesystem is considered.
RenameProblem inspect_name(const std::string& name, std::size_t limit) {
    if (name.empty()) {
        return RenameProblem::EmptyName;
    }
    if (name.find('/') != std::string::npos) {
        return RenameProblem::NameHasSeparator;
    }
    if (name == "." || name == "..") {
        return RenameProblem::ReservedName;
    }
    if (name.size() > limit) {
        return RenameProblem::NameTooLong;
    }
    return RenameProblem::None;
}

/// Normalize a supplied entry path to the absolute path of a single entry.
fs::path normalize_entry(const fs::path& entry) {
    std::error_code ec;
    fs::path absolute = fs::absolute(entry, ec).lexically_normal();
    if (ec) {
        absolute = entry.lexically_normal();
    }
    // A path written with a trailing separator names the same entry, and
    // filename() reports nothing for it.
    if (absolute.filename().empty() && absolute.has_parent_path()) {
        absolute = absolute.parent_path();
    }
    return absolute;
}

/// Whether a step will vacate the name it currently holds.
///
/// A step already refused for its own name is not going to be performed, so it
/// is still holding its name as far as the rest of the batch is concerned.
bool leaves_its_name(const RenamePlanStep& step) {
    return !step.blocked() && step.changes_name();
}

/// Run the batch forward greedily and report which steps could never be
/// started. A step can start once nothing that has yet to move is standing on
/// its target, so whatever is left when progress stops is a closed cycle.
std::size_t unreachable_step_count(const std::vector<RenamePlanStep>& steps) {
    std::unordered_set<std::string> held;
    std::vector<std::size_t> pending;
    for (std::size_t index = 0; index < steps.size(); ++index) {
        if (!leaves_its_name(steps[index])) {
            continue;
        }
        pending.push_back(index);
        held.insert(steps[index].source.string());
    }

    bool progressed = true;
    while (progressed && !pending.empty()) {
        progressed = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            const RenamePlanStep& step = steps[*candidate];
            const std::string target = (step.source.parent_path() / step.proposed_name).string();
            if (held.contains(target)) {
                ++candidate;
                continue;
            }
            held.erase(step.source.string());
            candidate = pending.erase(candidate);
            progressed = true;
        }
    }
    return pending.size();
}

/// Everything the planner learns about one directory, read once.
struct DirectoryFacts {
    detail::DirectoryNames names;
    std::unordered_set<std::string> present;
    std::size_t name_limit = fallback_name_limit;
};

} // namespace

bool RenamePlan::usable() const noexcept {
    return !rule_error && blocked_count() == 0;
}

std::size_t RenamePlan::blocked_count() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(steps, [](const RenamePlanStep& s) { return s.blocked(); }));
}

std::size_t RenamePlan::change_count() const noexcept {
    return static_cast<std::size_t>(
        std::ranges::count_if(steps, [](const RenamePlanStep& s) { return s.changes_name(); }));
}

bool RenamePlan::needs_sequencing() const noexcept {
    return std::ranges::any_of(
        steps, [](const RenamePlanStep& s) { return s.sequencing == RenameSequencing::Deferred; });
}

bool RenamePlan::needs_working_name() const noexcept {
    return unreachable_step_count(steps) != 0;
}

namespace detail {

DirectoryNames read_directory_names(const fs::path& directory) {
    DirectoryNames result;
    std::error_code ec;
    fs::directory_iterator iterator(directory, ec);
    if (ec) {
        return result;
    }
    result.readable = true;
    const fs::directory_iterator end;
    for (; iterator != end; iterator.increment(ec)) {
        if (ec) {
            result.readable = false;
            return result;
        }
        result.names.push_back(iterator->path().filename().string());
    }
    return result;
}

ProbedName probe_name(const fs::path& path) {
    ProbedName result;
    const EntryMetadata metadata = read_entry_metadata(path);
    if (metadata.known) {
        result.exists = true;
        result.identity = metadata.identity;
        return result;
    }
    // An entry that exists but cannot be examined still occupies its name.
    // Only "nothing is there" makes a name free.
    result.exists = metadata.error_number != ENOENT && metadata.error_number != 0;
    return result;
}

std::size_t name_limit_for(const fs::path& directory) {
    const long reported = ::pathconf(directory.c_str(), _PC_NAME_MAX);
    if (reported <= 0) {
        return fallback_name_limit;
    }
    return static_cast<std::size_t>(reported);
}

/// Compile the rule, or report why it cannot be used at all.
///
/// A rule error is a property of the rule rather than of any one name, so it
/// stops the whole plan before a single entry is examined.
std::error_code compile_rule(const RenameRule& rule, std::optional<std::regex>& compiled) {
    // An empty pattern matches at every position, including the empty one, so
    // it has no meaning as an instruction to replace something.
    if (rule.pattern.empty()) {
        return make_error(std::errc::invalid_argument);
    }
    if (rule.match != RenameMatch::Regex) {
        return {};
    }
    auto syntax = std::regex::ECMAScript;
    if (!rule.case_sensitive) {
        syntax |= std::regex::icase;
    }
    try {
        compiled.emplace(rule.pattern, syntax);
    } catch (const std::regex_error&) {
        return make_error(std::errc::invalid_argument);
    }
    return {};
}

/// Read a directory's facts once and keep them for every entry in it.
const DirectoryFacts& facts_for(std::unordered_map<std::string, DirectoryFacts>& directories,
                                const fs::path& parent, const PlanProbes& probes) {
    const std::string key = parent.string();
    auto known = directories.find(key);
    if (known != directories.end()) {
        return known->second;
    }
    DirectoryFacts facts;
    facts.names = probes.read_directory(parent);
    facts.present.insert(facts.names.names.begin(), facts.names.names.end());
    facts.name_limit = probes.name_limit(parent);
    return directories.emplace(key, std::move(facts)).first->second;
}

/// What one entry's name becomes, and what is wrong with it on its own.
RenamePlanStep plan_one_step(const fs::path& entry, const RenameRule& rule,
                             const std::regex* compiled, const PlanProbes& probes,
                             std::unordered_set<std::string>& seen_sources,
                             std::unordered_map<std::string, DirectoryFacts>& directories) {
    RenamePlanStep step;
    step.source = normalize_entry(entry);
    step.current_name = step.source.filename().string();
    step.proposed_name = step.current_name;

    if (!seen_sources.insert(step.source.string()).second) {
        step.problem = RenameProblem::DuplicateSource;
        return step;
    }

    const ProbedName source_probe = probes.probe(step.source);
    if (!source_probe.exists) {
        step.problem = RenameProblem::SourceMissing;
        return step;
    }
    step.source_identity = source_probe.identity;

    // The parent is named rather than passed as a temporary: the reference
    // returned here refers into `directories` and outlives the call, but a
    // temporary argument makes that impossible for the compiler to see.
    const fs::path parent = step.source.parent_path();
    const DirectoryFacts& facts = facts_for(directories, parent, probes);

    std::string proposed;
    if (!apply_rule(rule, compiled, step.current_name, proposed)) {
        step.problem = RenameProblem::RuleNotApplied;
        return step;
    }
    step.proposed_name = std::move(proposed);
    step.problem = inspect_name(step.proposed_name, facts.name_limit);
    return step;
}

/// What the batch and the filesystem say about one step's target.
///
/// Split out so each answer is reached by returning rather than by falling
/// through the one before it: the questions rule each other out, and a chain
/// would hide that they are separate questions.
void resolve_target(RenamePlanStep& step, const RenamePlan& plan,
                    const std::unordered_map<std::string, DirectoryFacts>& directories,
                    const PlanProbes& probes,
                    const std::unordered_map<std::string, std::size_t>& batch_occupant,
                    const std::unordered_map<std::string, std::vector<std::size_t>>& proposed_by) {
    const fs::path parent = step.source.parent_path();
    const fs::path target = parent / step.proposed_name;
    const std::string target_key = target.string();

    // Two entries given the same new name. Every member of the group is
    // reported, because a preview has to show both rows as refused rather than
    // picking a winner.
    const auto claimants = proposed_by.find(target_key);
    if (claimants != proposed_by.end() && claimants->second.size() > 1) {
        step.problem = RenameProblem::DuplicateTarget;
        return;
    }

    // The target is currently held by another entry in this batch.
    const auto occupant = batch_occupant.find(target_key);
    if (occupant != batch_occupant.end()) {
        if (leaves_its_name(plan.steps[occupant->second])) {
            step.sequencing = RenameSequencing::Deferred;
            return;
        }
        step.problem = RenameProblem::TargetExists;
        return;
    }

    const DirectoryFacts& facts = directories.at(parent.string());
    if (facts.present.contains(step.proposed_name)) {
        step.problem = RenameProblem::TargetExists;
        return;
    }

    // The name is not in the listing. If the filesystem resolves it anyway it
    // folded the spelling onto an entry that is there, and renaming to it
    // would overwrite that entry.
    const ProbedName probed = probes.probe(target);
    if (!probed.exists) {
        return;
    }
    if (same_identity(probed.identity, step.source_identity)) {
        // The fold points at this entry itself: a change of case in place,
        // which the filesystem performs as an ordinary rename.
        return;
    }
    const auto folded_onto =
        std::ranges::find_if(plan.steps, [&probed](const RenamePlanStep& other) {
            return same_identity(other.source_identity, probed.identity);
        });
    if (folded_onto != plan.steps.end() && leaves_its_name(*folded_onto)) {
        step.sequencing = RenameSequencing::Deferred;
        return;
    }
    step.problem =
        facts.names.readable ? RenameProblem::CaseOnlyTargetExists : RenameProblem::TargetExists;
}

RenamePlan plan_bulk_rename_using(const std::vector<fs::path>& entries, const RenameRule& rule,
                                  const PlanProbes& probes) {
    RenamePlan plan;
    plan.rule = rule;

    std::optional<std::regex> compiled;
    plan.rule_error = compile_rule(rule, compiled);
    if (plan.rule_error) {
        return plan;
    }

    std::unordered_map<std::string, DirectoryFacts> directories;
    std::unordered_set<std::string> seen_sources;

    // First pass: what each name becomes, and what is wrong with it on its own.
    for (const fs::path& entry : entries) {
        plan.steps.push_back(plan_one_step(entry, rule, compiled ? &*compiled : nullptr, probes,
                                           seen_sources, directories));
    }

    // Second pass: what the batch and the filesystem say about each target.
    // Every step's own name has already been settled, so a step that is going
    // to vacate its name can be told from one that is not.
    std::unordered_map<std::string, std::size_t> batch_occupant;
    std::unordered_map<std::string, std::vector<std::size_t>> proposed_by;
    for (std::size_t index = 0; index < plan.steps.size(); ++index) {
        const RenamePlanStep& step = plan.steps[index];
        if (step.problem == RenameProblem::DuplicateSource ||
            step.problem == RenameProblem::SourceMissing) {
            continue;
        }
        batch_occupant.emplace(step.source.string(), index);
        if (!step.blocked() && step.changes_name()) {
            proposed_by[(step.source.parent_path() / step.proposed_name).string()].push_back(index);
        }
    }

    for (RenamePlanStep& step : plan.steps) {
        if (step.blocked() || !step.changes_name()) {
            continue;
        }
        resolve_target(step, plan, directories, probes, batch_occupant, proposed_by);
    }

    return plan;
}

void rename_with_filesystem_step(const fs::path& from, const fs::path& to, std::error_code& error) {
    const OperationOutcome outcome = rename_entry(from, to.filename().string(), OperationOptions{});
    error = outcome.error;
}

namespace {

/// Whether the fresh plan describes the same batch as the one being applied.
bool plans_agree(const RenamePlan& planned, const RenamePlan& fresh) {
    if (fresh.rule_error || planned.steps.size() != fresh.steps.size()) {
        return false;
    }
    for (std::size_t index = 0; index < planned.steps.size(); ++index) {
        const RenamePlanStep& left = planned.steps[index];
        const RenamePlanStep& right = fresh.steps[index];
        if (left.source != right.source || left.current_name != right.current_name ||
            left.proposed_name != right.proposed_name || left.problem != right.problem ||
            left.sequencing != right.sequencing ||
            !same_identity(left.source_identity, right.source_identity)) {
            return false;
        }
    }
    return true;
}

/// One entry of a batch that is under way.
struct PendingRename {
    std::size_t step = 0;
    /// Where the entry is right now, which is a working name once a cycle has
    /// been broken through it.
    std::filesystem::path current;
    std::filesystem::path target;
    bool on_working_name = false;
};

/// Whether any other entry that has still to move is standing on `path`.
bool held_by_another(const std::vector<PendingRename>& pending, std::size_t self,
                     const fs::path& path) {
    return std::ranges::any_of(pending, [&](const PendingRename& other) {
        return other.step != self && other.current == path;
    });
}

/// Settle an entry that was standing under a working name when the batch
/// stopped.
///
/// Put it back where it started when that name is free; report it when it is
/// not, because an entry under an unexpected name can be recovered and a
/// removed one cannot. It is never removed.
void settle_working_entries(const RenamePlan& plan, const std::vector<PendingRename>& pending,
                            const PlanProbes& probes, const BatchRenameStep& rename_step,
                            RenameApplication& result) {
    for (const PendingRename& entry : pending) {
        if (!entry.on_working_name) {
            continue;
        }
        const fs::path origin = plan.steps[entry.step].source;
        std::error_code restore_ec;
        if (!probes.probe(origin).exists) {
            rename_step(entry.current, origin, restore_ec);
            if (!restore_ec) {
                continue;
            }
        }
        result.stranded_path = entry.current;
    }
}

/// Whether the plan still describes the filesystem, checked immediately before
/// the first rename is issued.
bool plan_still_holds(const RenamePlan& plan, const PlanProbes& probes) {
    std::vector<fs::path> sources;
    sources.reserve(plan.steps.size());
    for (const RenamePlanStep& step : plan.steps) {
        sources.push_back(step.source);
    }
    const RenamePlan fresh = plan_bulk_rename_using(sources, plan.rule, probes);
    return fresh.usable() && plans_agree(plan, fresh);
}

/// The entries a plan will actually move, in the order the plan lists them.
std::vector<PendingRename> pending_renames(const RenamePlan& plan) {
    std::vector<PendingRename> pending;
    for (std::size_t index = 0; index < plan.steps.size(); ++index) {
        const RenamePlanStep& step = plan.steps[index];
        if (!step.changes_name()) {
            continue;
        }
        pending.push_back(PendingRename{.step = index,
                                        .current = step.source,
                                        .target = step.source.parent_path() / step.proposed_name,
                                        .on_working_name = false});
    }
    return pending;
}

} // namespace

RenameApplication
apply_bulk_rename_using(const RenamePlan& plan, const PlanProbes& probes,
                        const BatchRenameStep& rename_step,
                        const std::function<void(const AppliedRename&)>& on_applied) {
    RenameApplication application;

    if (!plan.usable()) {
        application.status = RenameApplyStatus::PlanRejected;
        return application;
    }
    if (plan.change_count() == 0) {
        application.status = RenameApplyStatus::NothingToDo;
        return application;
    }

    // A preview the user has been reading is not evidence about a directory
    // other programs share.
    if (!plan_still_holds(plan, probes)) {
        application.status = RenameApplyStatus::PlanStale;
        return application;
    }

    std::vector<PendingRename> pending = pending_renames(plan);
    application.status = RenameApplyStatus::Applied;

    const auto interrupt = [&](std::size_t step, const std::error_code& error) {
        application.status = RenameApplyStatus::Interrupted;
        application.failed_step = step;
        application.error = error;
        settle_working_entries(plan, pending, probes, rename_step, application);
    };

    while (!pending.empty()) {
        bool progressed = false;
        for (auto candidate = pending.begin(); candidate != pending.end();) {
            if (held_by_another(pending, candidate->step, candidate->target)) {
                ++candidate;
                continue;
            }
            std::error_code ec;
            rename_step(candidate->current, candidate->target, ec);
            if (ec) {
                interrupt(candidate->step, ec);
                return application;
            }
            const AppliedRename done{.source = plan.steps[candidate->step].source,
                                     .result = candidate->target};
            application.applied.push_back(done);
            if (on_applied) {
                on_applied(done);
            }
            candidate = pending.erase(candidate);
            progressed = true;
        }
        if (progressed || pending.empty()) {
            continue;
        }

        // Nothing can move: every remaining target is held by another entry
        // that is also waiting, which is a closed cycle. Moving one member to
        // a working name breaks it, and that member takes its final name once
        // its target is free.
        PendingRename& breaker = pending.front();
        std::error_code break_ec;
        const fs::path working = reserve_working_path(breaker.current.parent_path(),
                                                      WorkingEntryRole::Prepared, break_ec);
        if (!break_ec) {
            rename_step(breaker.current, working, break_ec);
        }
        if (break_ec) {
            interrupt(breaker.step, break_ec);
            return application;
        }
        breaker.current = working;
        breaker.on_working_name = true;
    }

    return application;
}

} // namespace detail

RenamePlan plan_bulk_rename(const std::vector<fs::path>& entries, const RenameRule& rule) {
    return detail::plan_bulk_rename_using(entries, rule, detail::PlanProbes{});
}

RenameApplication apply_bulk_rename(const RenamePlan& plan) {
    return detail::apply_bulk_rename_using(plan, detail::PlanProbes{},
                                           &detail::rename_with_filesystem_step, {});
}

} // namespace odysea::core

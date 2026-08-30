// Headless coverage for bulk rename planning and application.
//
// The four things a plan has to catch before anything is written are covered
// one at a time, and each has a case that fails by name when its check is
// removed: two entries given one name, a name already taken by something that
// is not leaving, a name held by another member of the batch that is leaving,
// and a name the filesystem will not accept or will fold onto an entry that is
// already there.
//
// The third of those is the one a preview cannot show, and two separate
// properties are asserted about it. Every rename refuses an occupied name, so a
// batch performed in a bad order stops rather than overwriting; sequencing is
// what lets such a batch finish. The cases read the file contents back rather
// than the listing, because a batch that stopped early and one that completed
// correctly are told apart by which entry holds which contents.

#include "bulk_rename_internal.hpp"
#include "odysea/core/bulk_rename.hpp"
#include "odysea/core/file_operations.hpp"
#include "odysea/core/operation_journal.hpp"
#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace odysea::core;
using odysea::test::check;
using odysea::test::read_text;
using odysea::test::TemporaryTree;

namespace {

RenameRule literal_rule(std::string pattern, std::string replacement) {
    RenameRule rule;
    rule.pattern = std::move(pattern);
    rule.replacement = std::move(replacement);
    rule.match = RenameMatch::Literal;
    return rule;
}

RenameRule regex_rule(std::string pattern, std::string replacement) {
    RenameRule rule;
    rule.pattern = std::move(pattern);
    rule.replacement = std::move(replacement);
    rule.match = RenameMatch::Regex;
    return rule;
}

std::string proposed_for(const RenamePlan& plan, std::string_view current) {
    for (const RenamePlanStep& step : plan.steps) {
        if (step.current_name == current) {
            return step.proposed_name;
        }
    }
    return "<no such step>";
}

const RenamePlanStep* step_for(const RenamePlan& plan, std::string_view current) {
    for (const RenamePlanStep& step : plan.steps) {
        if (step.current_name == current) {
            return &step;
        }
    }
    return nullptr;
}

RenameProblem problem_for(const RenamePlan& plan, std::string_view current) {
    const RenamePlanStep* step = step_for(plan, current);
    return step == nullptr ? RenameProblem::None : step->problem;
}

/// Names present in a directory, sorted, so a case can assert that planning
/// changed nothing and that an application changed exactly what it said.
std::vector<std::string> names_in(const fs::path& directory) {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        names.push_back(it->path().filename().string());
    }
    std::ranges::sort(names);
    return names;
}

std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        if (!out.empty()) {
            out += ',';
        }
        out += name;
    }
    return out;
}

std::string lowered(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

/// Answer as a filesystem that does not distinguish case would.
///
/// The filesystems these tests run on all distinguish case, so the check for a
/// name that folds onto an existing entry would never be reached by waiting
/// for a real fold: the case would report a pass for a check that never ran.
detail::ProbedName folding_probe(const fs::path& path) {
    const detail::ProbedName direct = detail::probe_name(path);
    if (direct.exists) {
        return direct;
    }
    const std::string wanted = lowered(path.filename().string());
    std::error_code ec;
    for (fs::directory_iterator it(path.parent_path(), ec), end; !ec && it != end;
         it.increment(ec)) {
        if (lowered(it->path().filename().string()) == wanted) {
            return detail::probe_name(it->path());
        }
    }
    return direct;
}

detail::PlanProbes folding_probes() {
    detail::PlanProbes probes;
    probes.probe = &folding_probe;
    return probes;
}

/// A rename step that fails on a chosen call, so the state a batch is left in
/// by a mid-batch failure can be asserted rather than assumed.
class FailingStep {
  public:
    explicit FailingStep(std::size_t fail_at) : fail_at_(fail_at) {}

    void operator()(const fs::path& from, const fs::path& to, std::error_code& error) {
        ++calls_;
        performed_.push_back(to);
        if (calls_ == fail_at_) {
            performed_.pop_back();
            error = std::make_error_code(std::errc::permission_denied);
            return;
        }
        detail::rename_with_filesystem_step(from, to, error);
    }

    [[nodiscard]] std::size_t calls() const noexcept { return calls_; }
    [[nodiscard]] const std::vector<fs::path>& performed() const noexcept { return performed_; }

  private:
    std::size_t fail_at_ = 0;
    std::size_t calls_ = 0;
    std::vector<fs::path> performed_;
};

// ---------------------------------------------------------------------------
// Rule application
// ---------------------------------------------------------------------------

void literal_replacement_rewrites_every_occurrence() {
    TemporaryTree tree("bulk_literal_all");
    const fs::path a = tree.file("draft-one-draft.txt");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("draft", "final"));
    check(!plan.rule_error, "literal rule compiles");
    check(proposed_for(plan, "draft-one-draft.txt") == "final-one-final.txt",
          "literal replacement rewrites every occurrence");
    check(plan.change_count() == 1, "literal replacement counts one change");
}

void literal_replacement_can_stop_after_the_first() {
    TemporaryTree tree("bulk_literal_first");
    const fs::path a = tree.file("draft-one-draft.txt");
    RenameRule rule = literal_rule("draft", "final");
    rule.scope = RenameScope::First;
    const RenamePlan plan = plan_bulk_rename({a}, rule);
    check(proposed_for(plan, "draft-one-draft.txt") == "final-one-draft.txt",
          "literal replacement stops after the first occurrence");
}

void literal_replacement_copies_a_dollar_reference_verbatim() {
    TemporaryTree tree("bulk_literal_dollar");
    const fs::path a = tree.file("report.txt");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("report", "$1-report"));
    check(proposed_for(plan, "report.txt") == "$1-report.txt",
          "a literal replacement has no capture groups and copies $1 verbatim");
}

void literal_replacement_can_ignore_case() {
    TemporaryTree tree("bulk_literal_icase");
    const fs::path a = tree.file("DRAFT-notes.txt");
    RenameRule rule = literal_rule("draft", "final");
    rule.case_sensitive = false;
    const RenamePlan plan = plan_bulk_rename({a}, rule);
    check(proposed_for(plan, "DRAFT-notes.txt") == "final-notes.txt",
          "a case-insensitive literal rule matches a differently cased name");
}

void regex_replacement_expands_capture_groups() {
    TemporaryTree tree("bulk_regex_groups");
    const fs::path a = tree.file("2024-05-photo.jpg");
    const RenamePlan plan =
        plan_bulk_rename({a}, regex_rule(R"(^(\d{4})-(\d{2})-(.*)$)", "$3-$1$2"));
    check(proposed_for(plan, "2024-05-photo.jpg") == "photo.jpg-202405",
          "a regular expression replacement expands capture references");
}

void regex_replacement_expands_the_whole_match_and_a_literal_dollar() {
    TemporaryTree tree("bulk_regex_specials");
    const fs::path a = tree.file("note.txt");
    const RenamePlan plan = plan_bulk_rename({a}, regex_rule("note", "$$-$&"));
    check(proposed_for(plan, "note.txt") == "$-note.txt",
          "a regular expression replacement expands $$ and $&");
}

void regex_replacement_can_stop_after_the_first() {
    TemporaryTree tree("bulk_regex_first");
    const fs::path a = tree.file("aXaXa.txt");
    RenameRule rule = regex_rule("X", "-");
    rule.scope = RenameScope::First;
    const RenamePlan plan = plan_bulk_rename({a}, rule);
    check(proposed_for(plan, "aXaXa.txt") == "a-aXa.txt",
          "a regular expression replacement stops after the first match");
}

void regex_replacement_can_ignore_case() {
    TemporaryTree tree("bulk_regex_icase");
    const fs::path a = tree.file("PHOTO.jpg");
    RenameRule rule = regex_rule("photo", "image");
    rule.case_sensitive = false;
    const RenamePlan plan = plan_bulk_rename({a}, rule);
    check(proposed_for(plan, "PHOTO.jpg") == "image.jpg",
          "a case-insensitive regular expression matches a differently cased name");
}

void an_empty_pattern_is_refused_as_a_rule() {
    TemporaryTree tree("bulk_empty_pattern");
    const fs::path a = tree.file("note.txt");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("", "x"));
    check(plan.rule_error == std::errc::invalid_argument, "an empty pattern is refused");
    check(plan.steps.empty(), "a refused rule computes no steps");
    check(!plan.usable(), "a plan with a rule error is not usable");
}

void a_malformed_expression_is_refused_as_a_rule() {
    TemporaryTree tree("bulk_bad_regex");
    const fs::path a = tree.file("note.txt");
    const RenamePlan plan = plan_bulk_rename({a}, regex_rule("([unclosed", "x"));
    check(plan.rule_error == std::errc::invalid_argument,
          "a malformed regular expression is refused");
    check(plan.steps.empty(), "a malformed expression computes no steps");
}

void a_rule_that_matches_nothing_changes_nothing() {
    TemporaryTree tree("bulk_no_match");
    const fs::path a = tree.file("note.txt");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("absent", "x"));
    check(plan.usable(), "a rule that matches nothing is still usable");
    check(plan.change_count() == 0, "a rule that matches nothing changes no name");
    const RenamePlanStep* step = step_for(plan, "note.txt");
    check(step != nullptr && !step->changes_name(), "the unchanged entry is still reported");
}

void planning_writes_nothing() {
    TemporaryTree tree("bulk_plan_pure");
    const fs::path a = tree.file("one.txt");
    const fs::path b = tree.file("two.txt");
    const std::vector<std::string> before = names_in(tree.root());
    const RenamePlan plan = plan_bulk_rename({a, b}, literal_rule("o", "0"));
    check(plan.steps.size() == 2, "planning reports one step per entry");
    check(joined(names_in(tree.root())) == joined(before), "planning writes nothing");
}

// ---------------------------------------------------------------------------
// Names a filesystem will not accept
// ---------------------------------------------------------------------------

void a_rule_producing_an_empty_name_is_refused() {
    TemporaryTree tree("bulk_empty_name");
    const fs::path a = tree.file("gone.txt");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("gone.txt", ""));
    check(problem_for(plan, "gone.txt") == RenameProblem::EmptyName,
          "a rule producing an empty name is refused");
    check(!plan.usable(), "a plan holding an empty name is not usable");
}

void a_rule_producing_a_separator_is_refused() {
    TemporaryTree tree("bulk_separator");
    const fs::path a = tree.file("note.txt");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("note", "sub/note"));
    check(problem_for(plan, "note.txt") == RenameProblem::NameHasSeparator,
          "a rule producing a path separator is refused");
}

void a_rule_producing_a_reserved_name_is_refused() {
    TemporaryTree tree("bulk_reserved");
    const fs::path a = tree.file("note.txt");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("note.txt", ".."));
    check(problem_for(plan, "note.txt") == RenameProblem::ReservedName,
          "a rule producing the parent-directory name is refused");

    const fs::path b = tree.file("other.txt");
    const RenamePlan self = plan_bulk_rename({b}, literal_rule("other.txt", "."));
    check(problem_for(self, "other.txt") == RenameProblem::ReservedName,
          "a rule producing the current-directory name is refused");
}

void a_rule_producing_an_overlong_name_is_refused() {
    TemporaryTree tree("bulk_too_long");
    const fs::path a = tree.file("note.txt");
    const std::size_t limit = detail::name_limit_for(tree.root());
    const RenamePlan plan =
        plan_bulk_rename({a}, literal_rule("note", std::string(limit + 8, 'n')));
    check(problem_for(plan, "note.txt") == RenameProblem::NameTooLong,
          "a rule producing a name longer than the filesystem accepts is refused");

    const RenamePlan fits =
        plan_bulk_rename({a}, literal_rule("note.txt", std::string(limit, 'n')));
    check(problem_for(fits, "note.txt") == RenameProblem::None,
          "a name of exactly the length the filesystem accepts is not refused");
}

// ---------------------------------------------------------------------------
// Collision class one: two sources, one target
// ---------------------------------------------------------------------------

void two_entries_given_one_name_are_both_refused() {
    TemporaryTree tree("bulk_duplicate_target");
    const fs::path a = tree.file("alpha-1.txt");
    const fs::path b = tree.file("alpha-2.txt");
    const RenamePlan plan = plan_bulk_rename({a, b}, regex_rule(R"(alpha-\d)", "alpha"));
    check(problem_for(plan, "alpha-1.txt") == RenameProblem::DuplicateTarget,
          "the first entry of a colliding pair is refused");
    check(problem_for(plan, "alpha-2.txt") == RenameProblem::DuplicateTarget,
          "the second entry of a colliding pair is refused");
    check(plan.blocked_count() == 2, "both members of the collision are reported");
}

void one_name_claimed_by_three_entries_refuses_all_three() {
    TemporaryTree tree("bulk_duplicate_three");
    const fs::path a = tree.file("x-1.txt");
    const fs::path b = tree.file("x-2.txt");
    const fs::path c = tree.file("x-3.txt");
    const RenamePlan plan = plan_bulk_rename({a, b, c}, regex_rule(R"(x-\d)", "x"));
    check(plan.blocked_count() == 3, "every claimant of one name is reported");
}

// ---------------------------------------------------------------------------
// Collision class two: the name is taken by something that is not leaving
// ---------------------------------------------------------------------------

void a_target_that_already_exists_is_refused() {
    TemporaryTree tree("bulk_target_exists");
    const fs::path a = tree.file("draft.txt", "source");
    tree.file("final.txt", "occupant");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("draft", "final"));
    check(problem_for(plan, "draft.txt") == RenameProblem::TargetExists,
          "a name already held by an entry outside the batch is refused");

    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::PlanRejected, "a refused plan is not applied");
    check(read_text(tree.root() / "final.txt") == "occupant",
          "the entry that held the name is untouched");
    check(read_text(tree.root() / "draft.txt") == "source", "the source is untouched");
}

void a_target_held_by_a_batch_member_that_stays_is_refused() {
    TemporaryTree tree("bulk_target_stays");
    // The rule renames only the first entry, so the second keeps its name and
    // is still standing on the name the first one wants.
    const fs::path a = tree.file("draft.txt", "source");
    const fs::path b = tree.file("final.txt", "occupant");
    const RenamePlan plan = plan_bulk_rename({a, b}, literal_rule("draft", "final"));
    check(problem_for(plan, "draft.txt") == RenameProblem::TargetExists,
          "a name held by a batch member whose own name does not change is refused");
    check(problem_for(plan, "final.txt") == RenameProblem::None,
          "the member that keeps its name carries no problem of its own");
}

// ---------------------------------------------------------------------------
// Collision class three: the name is held by a member that is leaving
// ---------------------------------------------------------------------------

/// A chain the planner really produces.
///
/// Appending a suffix maps "note" onto "note.old", which is another entry in
/// the batch, and that entry onto "note.old.old", which is free. The first
/// step therefore has to wait for the second.
void a_name_held_by_a_departing_member_is_sequenced_not_refused() {
    TemporaryTree tree("bulk_deferred");
    const fs::path bare = tree.file("note", "bare contents");
    const fs::path suffixed = tree.file("note.old", "suffixed contents");

    const RenamePlan plan = plan_bulk_rename({bare, suffixed}, regex_rule("^(.+)$", "$1.old"));
    check(plan.usable(), "a chain onto a departing member is usable, not refused");
    const RenamePlanStep* first = step_for(plan, "note");
    const RenamePlanStep* second = step_for(plan, "note.old");
    check(first != nullptr && first->sequencing == RenameSequencing::Deferred,
          "the step whose target is still held is deferred");
    check(second != nullptr && second->sequencing == RenameSequencing::Immediate,
          "the step whose target is free is immediate");
    check(plan.needs_sequencing(), "the plan reports that it has to be sequenced");
    check(!plan.needs_working_name(), "an open chain needs no working name");
}

/// The case that fails when sequencing is removed.
///
/// Performed in the order supplied, the first rename writes over the entry the
/// second was going to move, and the resulting listing is identical to the
/// correct one. Only the contents tell the two apart.
void a_chain_is_performed_without_destroying_the_entry_it_passes_through() {
    TemporaryTree tree("bulk_chain");
    const fs::path bare = tree.file("note", "bare contents");
    const fs::path suffixed = tree.file("note.old", "suffixed contents");

    const RenamePlan plan = plan_bulk_rename({bare, suffixed}, regex_rule("^(.+)$", "$1.old"));
    check(plan.usable(), "the chain is usable");

    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::Applied, "the chain is applied");
    check(joined(names_in(tree.root())) == "note.old,note.old.old",
          "the chain produced both names");
    check(read_text(tree.root() / "note.old") == "bare contents",
          "the entry that moved onto the occupied name carries its own contents");
    check(read_text(tree.root() / "note.old.old") == "suffixed contents",
          "the entry that had to leave first was not written over");
    check(applied.applied.size() == 2, "both renames are reported");
}

void a_longer_chain_moves_every_entry_in_the_right_order() {
    TemporaryTree tree("bulk_long_chain");
    tree.file("log", "first");
    tree.file("log.1", "second");
    tree.file("log.1.1", "third");
    const fs::path a = tree.root() / "log";
    const fs::path b = tree.root() / "log.1";
    const fs::path c = tree.root() / "log.1.1";

    const RenamePlan plan = plan_bulk_rename({a, b, c}, regex_rule("^(.+)$", "$1.1"));
    check(plan.usable(), "a three-link chain is usable");
    check(plan.change_count() == 3, "every entry changes name");

    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::Applied, "the chain is applied");
    check(read_text(tree.root() / "log.1") == "first", "the first entry landed intact");
    check(read_text(tree.root() / "log.1.1") == "second", "the second entry landed intact");
    check(read_text(tree.root() / "log.1.1.1") == "third", "the third entry landed intact");
}

void a_target_held_by_a_member_whose_own_name_is_refused_is_refused() {
    TemporaryTree tree("bulk_blocked_occupant");
    // The longer of the two names is already at the limit, so appending to it
    // is refused. It is therefore not leaving, and the shorter name, whose
    // target is exactly that name, has to be refused as well.
    const std::size_t limit = detail::name_limit_for(tree.root());
    const std::string shorter(limit - 1, 'a');
    const std::string longest(limit, 'a');
    const fs::path a = tree.file(shorter, "shorter");
    const fs::path b = tree.file(longest, "longest");

    const RenamePlan plan = plan_bulk_rename({a, b}, regex_rule("^(.+)$", "$1a"));
    check(problem_for(plan, longest) == RenameProblem::NameTooLong,
          "the occupant cannot take its own new name");
    check(problem_for(plan, shorter) == RenameProblem::TargetExists,
          "an occupant that cannot move is still holding its name");
    check(!plan.usable(), "the batch is refused as a whole");
}

// ---------------------------------------------------------------------------
// Collision class four: a name the filesystem folds onto an existing entry
// ---------------------------------------------------------------------------

void a_name_differing_only_by_case_is_refused_where_case_is_folded() {
    TemporaryTree tree("bulk_case_fold");
    const fs::path a = tree.file("draft.txt", "source");
    tree.file("Report.txt", "occupant");

    const RenamePlan folded =
        detail::plan_bulk_rename_using({a}, literal_rule("draft", "REPORT"), folding_probes());
    check(problem_for(folded, "draft.txt") == RenameProblem::CaseOnlyTargetExists,
          "a name the filesystem folds onto an existing entry is refused");

    const RenamePlan plain = plan_bulk_rename({a}, literal_rule("draft", "REPORT"));
    check(problem_for(plain, "draft.txt") == RenameProblem::None,
          "the same name is free where the filesystem distinguishes case");
}

void a_change_of_case_in_place_is_not_a_collision() {
    TemporaryTree tree("bulk_case_self");
    const fs::path a = tree.file("Report.txt", "self");
    const RenamePlan folded =
        detail::plan_bulk_rename_using({a}, literal_rule("Report", "report"), folding_probes());
    check(problem_for(folded, "Report.txt") == RenameProblem::None,
          "an entry changing its own case does not collide with itself");
    check(folded.usable(), "a change of case in place is usable");
    const RenamePlanStep* step = step_for(folded, "Report.txt");
    // Immediate, not deferred: there is nothing to wait for. Recognising the
    // fold as this entry itself and recognising it as some other entry that
    // happens to be leaving are different answers, and only one of them is
    // true.
    check(step != nullptr && step->sequencing == RenameSequencing::Immediate,
          "an entry changing its own case waits for nothing");
    check(!folded.needs_sequencing(), "a change of case in place needs no sequencing");
}

void a_folded_name_held_by_a_departing_member_is_sequenced() {
    TemporaryTree tree("bulk_case_departing");
    // "x" wants "x.old". The directory holds "X.old", which is not that name
    // as spelled, but a filesystem that folds case resolves the one to the
    // other. That entry is itself leaving, so the step waits rather than being
    // refused.
    const fs::path bare = tree.file("x", "bare contents");
    const fs::path upper = tree.file("X.old", "upper contents");

    const RenamePlan plan = detail::plan_bulk_rename_using(
        {bare, upper}, regex_rule("^(.+)$", "$1.old"), folding_probes());
    check(plan.usable(), "a fold onto an entry that is leaving is not refused");
    const RenamePlanStep* step = step_for(plan, "x");
    check(step != nullptr && step->sequencing == RenameSequencing::Deferred,
          "the step waits for the entry its name folds onto");

    const RenameApplication applied = detail::apply_bulk_rename_using(
        plan, folding_probes(), &detail::rename_with_filesystem_step, {});
    check(applied.status == RenameApplyStatus::Applied, "the sequenced fold is applied");
    check(read_text(tree.root() / "x.old") == "bare contents",
          "the entry that waited landed intact");
    check(read_text(tree.root() / "X.old.old") == "upper contents",
          "the entry it waited for was not written over");
}

// ---------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------

void an_entry_supplied_twice_is_planned_once() {
    TemporaryTree tree("bulk_duplicate_source");
    const fs::path a = tree.file("note.txt");
    const RenamePlan plan = plan_bulk_rename({a, a}, literal_rule("note", "memo"));
    check(plan.steps.size() == 2, "every supplied entry gets a step");
    check(plan.steps[0].problem == RenameProblem::None, "the first occurrence is planned");
    check(plan.steps[1].problem == RenameProblem::DuplicateSource,
          "the repeated occurrence is refused");
    check(!plan.usable(), "a batch naming one entry twice is not usable");
}

void a_missing_source_is_refused_before_anything_is_written() {
    TemporaryTree tree("bulk_missing_source");
    const fs::path a = tree.file("present.txt");
    const fs::path absent = tree.root() / "absent.txt";
    const RenamePlan plan = plan_bulk_rename({a, absent}, literal_rule("t", "T"));
    check(problem_for(plan, "absent.txt") == RenameProblem::SourceMissing,
          "an entry that is not there is refused");
    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::PlanRejected, "the batch is not applied");
    check(applied.applied.empty(), "nothing was renamed");
}

void entries_in_different_directories_do_not_collide() {
    TemporaryTree tree("bulk_two_dirs");
    const fs::path left = tree.directory("left");
    const fs::path right = tree.directory("right");
    const fs::path a = tree.file("left/item.txt", "left side");
    const fs::path b = tree.file("right/item.txt", "right side");
    const RenamePlan plan = plan_bulk_rename({a, b}, literal_rule("item", "entry"));
    check(plan.usable(), "the same new name in two directories is not a collision");
    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::Applied, "both directories are renamed");
    check(read_text(left / "entry.txt") == "left side", "the left entry kept its contents");
    check(read_text(right / "entry.txt") == "right side", "the right entry kept its contents");
}

void directories_are_renamed_like_any_other_entry() {
    TemporaryTree tree("bulk_directories");
    tree.file("old-set/inner.txt", "held");
    const fs::path directory = tree.root() / "old-set";
    const RenamePlan plan = plan_bulk_rename({directory}, literal_rule("old", "new"));
    check(plan.usable(), "a directory can be renamed");
    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::Applied, "the directory is renamed");
    check(read_text(tree.root() / "new-set" / "inner.txt") == "held",
          "the directory's contents moved with it");
}

// ---------------------------------------------------------------------------
// Application
// ---------------------------------------------------------------------------

void a_plan_with_no_changes_writes_nothing() {
    TemporaryTree tree("bulk_nothing");
    const fs::path a = tree.file("note.txt");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("absent", "x"));
    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::NothingToDo, "a plan with no changes does nothing");
    check(applied.succeeded(), "doing nothing is not a failure");
}

void a_plan_whose_target_was_taken_since_is_refused() {
    TemporaryTree tree("bulk_stale_target");
    const fs::path a = tree.file("draft.txt", "source");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("draft", "final"));
    check(plan.usable(), "the plan was usable when it was made");

    tree.file("final.txt", "arrived later");
    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::PlanStale,
          "a target taken since the plan was made stops the batch");
    check(read_text(tree.root() / "final.txt") == "arrived later",
          "the entry that arrived is untouched");
    check(read_text(tree.root() / "draft.txt") == "source", "the source is untouched");
}

void a_plan_whose_source_vanished_is_refused() {
    TemporaryTree tree("bulk_stale_source");
    const fs::path a = tree.file("one.txt", "first");
    const fs::path b = tree.file("two.txt", "second");
    const RenamePlan plan = plan_bulk_rename({a, b}, literal_rule(".txt", ".text"));
    check(plan.usable(), "the plan was usable when it was made");

    std::error_code ec;
    fs::remove(a, ec);
    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::PlanStale,
          "a source that vanished stops the whole batch");
    check(fs::exists(b), "the entry that is still there was not renamed");
}

void a_plan_whose_source_was_replaced_is_refused() {
    TemporaryTree tree("bulk_stale_identity");
    const fs::path a = tree.file("one.txt", "first");
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule(".txt", ".text"));
    check(plan.usable(), "the plan was usable when it was made");

    std::error_code ec;
    fs::remove(a, ec);
    tree.file("one.txt", "a different entry under the same name");
    const RenameApplication applied = apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::PlanStale,
          "a source replaced by a different entry of the same name stops the batch");
    check(read_text(tree.root() / "one.txt") == "a different entry under the same name",
          "the entry that took the name is untouched");
}

/// A cycle the planner really produces.
///
/// Exchanging the two leading characters maps "ab.txt" onto "ba.txt" and
/// "ba.txt" back onto "ab.txt". Neither can move first, so the batch has to
/// pass one of them through a working name.
RenamePlan swap_plan(const fs::path& left, const fs::path& right) {
    return plan_bulk_rename({left, right}, regex_rule(R"(^(.)(.)\.txt$)", "$2$1.txt"));
}

void a_closed_cycle_is_recognized_before_it_is_applied() {
    TemporaryTree tree("bulk_cycle_plan");
    const fs::path left = tree.file("ab.txt", "left contents");
    const fs::path right = tree.file("ba.txt", "right contents");

    const RenamePlan plan = swap_plan(left, right);
    check(plan.usable(), "a swap is usable");
    check(plan.change_count() == 2, "both names change");
    check(plan.needs_sequencing(), "both steps are sequenced behind each other");
    check(plan.needs_working_name(), "a closed cycle needs a working name to break it");
    check(proposed_for(plan, "ab.txt") == "ba.txt", "the first name maps onto the second");
    check(proposed_for(plan, "ba.txt") == "ab.txt", "the second name maps onto the first");
}

void a_closed_cycle_exchanges_both_entries() {
    TemporaryTree tree("bulk_cycle_apply");
    const fs::path left = tree.file("ab.txt", "left contents");
    const fs::path right = tree.file("ba.txt", "right contents");

    std::vector<fs::path> issued;
    const RenamePlan plan = swap_plan(left, right);
    const RenameApplication applied = detail::apply_bulk_rename_using(
        plan, detail::PlanProbes{},
        [&issued](const fs::path& from, const fs::path& to, std::error_code& error) {
            issued.push_back(to);
            detail::rename_with_filesystem_step(from, to, error);
        },
        {});

    check(applied.status == RenameApplyStatus::Applied, "the swap is applied");
    check(joined(names_in(tree.root())) == "ab.txt,ba.txt", "both names still exist");
    check(read_text(tree.root() / "ba.txt") == "left contents",
          "the first entry took the second name");
    check(read_text(tree.root() / "ab.txt") == "right contents",
          "the second entry took the first name");
    check(applied.applied.size() == 2, "two logical renames are reported");

    // Three physical renames for two logical ones: the extra one is the hop
    // through the working name that breaks the cycle.
    check(issued.size() == 3, "a two-entry cycle costs three renames");
    const bool used_working_name = std::ranges::any_of(
        issued, [](const fs::path& path) { return is_working_entry(path.filename().string()); });
    check(used_working_name, "the cycle was broken through a recognizable working name");
}

void a_reported_rename_never_names_a_working_entry() {
    TemporaryTree tree("bulk_cycle_reported");
    const fs::path left = tree.file("ab.txt", "left contents");
    const fs::path right = tree.file("ba.txt", "right contents");

    const RenameApplication applied = apply_bulk_rename(swap_plan(left, right));
    check(applied.status == RenameApplyStatus::Applied, "the swap is applied");
    for (const AppliedRename& done : applied.applied) {
        check(!is_working_entry(done.result.filename().string()),
              "a reported rename names where the entry finally landed");
        check(!is_working_entry(done.source.filename().string()),
              "a reported rename names where the entry started the batch");
    }
}

void a_failure_partway_through_reports_exactly_what_was_done() {
    TemporaryTree tree("bulk_interrupted");
    tree.file("m1.txt", "one");
    tree.file("m2.txt", "two");
    tree.file("m3.txt", "three");
    const fs::path one = tree.root() / "m1.txt";
    const fs::path two = tree.root() / "m2.txt";
    const fs::path three = tree.root() / "m3.txt";

    const RenamePlan plan =
        plan_bulk_rename({one, two, three}, regex_rule(R"(^m([123])\.txt$)", "n$1.txt"));
    check(plan.usable(), "the plan is usable");

    FailingStep step(2);
    const RenameApplication applied =
        detail::apply_bulk_rename_using(plan, detail::PlanProbes{},
                                        [&step](const fs::path& from, const fs::path& to,
                                                std::error_code& error) { step(from, to, error); },
                                        {});
    check(applied.status == RenameApplyStatus::Interrupted, "a failed step interrupts the batch");
    check(applied.error == std::errc::permission_denied, "the failure is reported as it was seen");
    check(applied.applied.size() == 1, "exactly the renames that completed are reported");
    check(applied.applied[0].result.filename() == "n1.txt",
          "the completed rename is reported with the name it reached");
    check(applied.failed_step == 1, "the step that failed is identified");
    check(applied.stranded_path.empty(), "no entry was left under a working name");
    check(fs::exists(tree.root() / "n1.txt"), "the first rename really happened");
    check(fs::exists(two), "the entry the failure stopped is still where it was");
    check(fs::exists(three), "the entry after the failure was not touched");
}

void an_interrupted_cycle_puts_the_working_entry_back() {
    TemporaryTree tree("bulk_cycle_restore");
    const fs::path left = tree.file("ab.txt", "left contents");
    const fs::path right = tree.file("ba.txt", "right contents");

    // Fail the rename that follows the hop onto the working name. The name the
    // working entry came from is free again by then, so it goes back.
    FailingStep step(2);
    const RenameApplication applied =
        detail::apply_bulk_rename_using(swap_plan(left, right), detail::PlanProbes{},
                                        [&step](const fs::path& from, const fs::path& to,
                                                std::error_code& error) { step(from, to, error); },
                                        {});
    check(applied.status == RenameApplyStatus::Interrupted, "the cycle is interrupted");
    check(applied.applied.empty(), "no logical rename completed");
    check(applied.stranded_path.empty(), "the working entry was put back");
    check(joined(names_in(tree.root())) == "ab.txt,ba.txt", "both original names are back");
    check(read_text(left) == "left contents", "the first entry is intact under its own name");
    check(read_text(right) == "right contents", "the second entry is intact under its own name");
}

void an_interrupted_cycle_reports_an_entry_it_cannot_put_back() {
    TemporaryTree tree("bulk_cycle_strand");
    const fs::path left = tree.file("ab.txt", "left contents");
    const fs::path right = tree.file("ba.txt", "right contents");

    // Fail the last rename. By then another entry has taken the name the
    // working entry came from, so there is nowhere to put it back.
    FailingStep step(3);
    const RenameApplication applied =
        detail::apply_bulk_rename_using(swap_plan(left, right), detail::PlanProbes{},
                                        [&step](const fs::path& from, const fs::path& to,
                                                std::error_code& error) { step(from, to, error); },
                                        {});
    check(applied.status == RenameApplyStatus::Interrupted, "the cycle is interrupted");
    check(applied.applied.size() == 1, "the one rename that completed is reported");
    check(!applied.stranded_path.empty(), "the entry left under a working name is reported");
    check(is_working_entry(applied.stranded_path.filename().string()),
          "the reported path is a recognizable working entry");
    check(classify_working_entry(applied.stranded_path.filename().string()) ==
              WorkingEntryRole::Prepared,
          "the stranded entry is classified as holding prepared data");
    check(fs::exists(applied.stranded_path), "the stranded entry still holds its data");
    check(read_text(applied.stranded_path) == "left contents",
          "the stranded entry was reported rather than removed");
}

// ---------------------------------------------------------------------------
// The journal
// ---------------------------------------------------------------------------

void a_batch_is_recorded_as_one_record_per_completed_rename() {
    TemporaryTree tree("bulk_journal_records");
    tree.file("j1.txt", "one");
    tree.file("j2.txt", "two");
    const fs::path one = tree.root() / "j1.txt";
    const fs::path two = tree.root() / "j2.txt";

    OperationJournal journal;
    const RenamePlan plan = plan_bulk_rename({one, two}, regex_rule("^j([12])\\.txt$", "k$1.txt"));
    const RenameApplication applied = journal.apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::Applied, "the batch is applied");
    check(journal.size() == 2, "one record per completed rename");
    check(journal.at(0).kind == OperationKind::Rename, "the newest record is a rename");
    check(journal.at(0).result_path.filename() == "k2.txt",
          "the newest record describes the last rename performed");
    check(journal.can_undo(), "an ordinary batch is reversible");
}

void reversing_a_batch_record_by_record_restores_every_name() {
    TemporaryTree tree("bulk_journal_undo");
    tree.file("u1.txt", "one");
    tree.file("u2.txt", "two");
    tree.file("u3.txt", "three");
    const fs::path one = tree.root() / "u1.txt";
    const fs::path two = tree.root() / "u2.txt";
    const fs::path three = tree.root() / "u3.txt";

    OperationJournal journal;
    const RenamePlan plan =
        plan_bulk_rename({one, two, three}, regex_rule("^u([123])\\.txt$", "v$1.txt"));
    const RenameApplication applied = journal.apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::Applied, "the batch is applied");
    check(joined(names_in(tree.root())) == "v1.txt,v2.txt,v3.txt", "every name changed");

    for (int reversal = 0; reversal < 3; ++reversal) {
        const UndoOutcome outcome = journal.undo();
        check(outcome.succeeded(), "each record reverses in turn");
    }
    check(joined(names_in(tree.root())) == "u1.txt,u2.txt,u3.txt",
          "reversing every record restores every original name");
    check(read_text(one) == "one", "contents came back with their entry");
    check(journal.empty(), "the batch's records are gone once reversed");
}

void reversing_a_sequenced_batch_finds_each_name_free_again() {
    TemporaryTree tree("bulk_journal_sequenced");
    const fs::path bare = tree.file("entry", "bare contents");
    const fs::path suffixed = tree.file("entry.old", "suffixed contents");

    // The first step has to wait for the second, so the batch is performed in
    // an order the supplied one does not give. Reversing has to unwind that
    // order, not the supplied one.
    OperationJournal journal;
    const RenamePlan plan = plan_bulk_rename({bare, suffixed}, regex_rule("^(.+)$", "$1.old"));
    check(plan.needs_sequencing(), "the batch has to be sequenced");

    const RenameApplication applied = journal.apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::Applied, "the sequenced batch is applied");
    check(journal.size() == 2, "both renames are recorded");
    check(journal.can_undo(), "a sequenced batch without a cycle is reversible");

    const UndoOutcome first = journal.undo();
    check(first.succeeded(), "the newest rename reverses");
    const UndoOutcome second = journal.undo();
    check(second.succeeded(), "the older rename reverses once the newer one is undone");
    check(joined(names_in(tree.root())) == "entry,entry.old", "the original names are back");
    check(read_text(bare) == "bare contents", "each entry came back with its own contents");
    check(read_text(suffixed) == "suffixed contents", "the other entry did too");
}

void a_batch_that_needed_a_working_name_is_never_offered_as_reversible() {
    TemporaryTree tree("bulk_journal_cycle");
    const fs::path left = tree.file("ab.txt", "left contents");
    const fs::path right = tree.file("ba.txt", "right contents");

    OperationJournal journal;
    const RenamePlan plan =
        plan_bulk_rename({left, right}, regex_rule(R"(^(.)(.)\.txt$)", "$2$1.txt"));
    check(plan.needs_working_name(), "the batch is a closed cycle");

    const RenameApplication applied = journal.apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::Applied, "the cycle is applied");
    check(journal.size() == 2, "both renames are still recorded");
    check(!journal.can_undo(), "a cycle is never offered as reversible");
    check(journal.at(0).barrier == ReversalBarrier::BatchCycleNotRestorable,
          "the newest record says why it can never be reversed");
    check(journal.at(1).barrier == ReversalBarrier::BatchCycleNotRestorable,
          "so does the older one, which cannot be reversed on its own either");

    const UndoOutcome refused = journal.undo();
    check(refused.status == UndoStatus::Barred, "an attempted reversal is refused");
    check(refused.barrier == ReversalBarrier::BatchCycleNotRestorable,
          "the refusal names the barrier");
    check(read_text(tree.root() / "ba.txt") == "left contents",
          "the refused reversal changed nothing");
}

void a_record_is_only_offered_after_its_rename_completed() {
    TemporaryTree tree("bulk_journal_partial");
    tree.file("d1.txt", "one");
    tree.file("d2.txt", "two");
    const fs::path one = tree.root() / "d1.txt";
    const fs::path two = tree.root() / "d2.txt";

    const RenamePlan plan =
        plan_bulk_rename({one, two}, regex_rule(R"(^d([12])\.txt$)", "e$1.txt"));

    std::vector<fs::path> recorded;
    FailingStep step(2);
    const RenameApplication applied = detail::apply_bulk_rename_using(
        plan, detail::PlanProbes{},
        [&step](const fs::path& from, const fs::path& to, std::error_code& error) {
            step(from, to, error);
        },
        [&recorded](const AppliedRename& done) { recorded.push_back(done.result); });

    check(applied.status == RenameApplyStatus::Interrupted, "the batch is interrupted");
    check(recorded.size() == 1, "a record is offered for exactly the rename that completed");
    check(recorded[0].filename() == "e1.txt", "the record describes where the entry landed");
    check(fs::exists(tree.root() / "e1.txt"), "the recorded rename really happened");
    check(fs::exists(two), "the rename that failed left its entry where it was");
}

void a_refused_plan_is_never_recorded() {
    TemporaryTree tree("bulk_journal_refused");
    const fs::path a = tree.file("draft.txt", "source");
    tree.file("final.txt", "occupant");

    OperationJournal journal;
    const RenamePlan plan = plan_bulk_rename({a}, literal_rule("draft", "final"));
    const RenameApplication applied = journal.apply_bulk_rename(plan);
    check(applied.status == RenameApplyStatus::PlanRejected, "the plan is refused");
    check(journal.empty(), "a refused batch records nothing");
    check(!journal.can_undo(), "there is nothing to reverse");
}

struct Scenario {
    const char* name;
    void (*run)();
};

const Scenario scenarios[] = {
    {.name = "literal replacement rewrites every occurrence",
     .run = literal_replacement_rewrites_every_occurrence},
    {.name = "literal replacement can stop after the first",
     .run = literal_replacement_can_stop_after_the_first},
    {.name = "literal replacement copies a dollar reference verbatim",
     .run = literal_replacement_copies_a_dollar_reference_verbatim},
    {.name = "literal replacement can ignore case", .run = literal_replacement_can_ignore_case},
    {.name = "regex replacement expands capture groups",
     .run = regex_replacement_expands_capture_groups},
    {.name = "regex replacement expands the whole match and a literal dollar",
     .run = regex_replacement_expands_the_whole_match_and_a_literal_dollar},
    {.name = "regex replacement can stop after the first",
     .run = regex_replacement_can_stop_after_the_first},
    {.name = "regex replacement can ignore case", .run = regex_replacement_can_ignore_case},
    {.name = "an empty pattern is refused as a rule", .run = an_empty_pattern_is_refused_as_a_rule},
    {.name = "a malformed expression is refused as a rule",
     .run = a_malformed_expression_is_refused_as_a_rule},
    {.name = "a rule that matches nothing changes nothing",
     .run = a_rule_that_matches_nothing_changes_nothing},
    {.name = "planning writes nothing", .run = planning_writes_nothing},
    {.name = "a rule producing an empty name is refused",
     .run = a_rule_producing_an_empty_name_is_refused},
    {.name = "a rule producing a separator is refused",
     .run = a_rule_producing_a_separator_is_refused},
    {.name = "a rule producing a reserved name is refused",
     .run = a_rule_producing_a_reserved_name_is_refused},
    {.name = "a rule producing an overlong name is refused",
     .run = a_rule_producing_an_overlong_name_is_refused},
    {.name = "two entries given one name are both refused",
     .run = two_entries_given_one_name_are_both_refused},
    {.name = "one name claimed by three entries refuses all three",
     .run = one_name_claimed_by_three_entries_refuses_all_three},
    {.name = "a target that already exists is refused",
     .run = a_target_that_already_exists_is_refused},
    {.name = "a target held by a batch member that stays is refused",
     .run = a_target_held_by_a_batch_member_that_stays_is_refused},
    {.name = "a name held by a departing member is sequenced not refused",
     .run = a_name_held_by_a_departing_member_is_sequenced_not_refused},
    {.name = "a chain is performed without destroying the entry it passes through",
     .run = a_chain_is_performed_without_destroying_the_entry_it_passes_through},
    {.name = "a longer chain moves every entry in the right order",
     .run = a_longer_chain_moves_every_entry_in_the_right_order},
    {.name = "a target held by a member whose own name is refused is refused",
     .run = a_target_held_by_a_member_whose_own_name_is_refused_is_refused},
    {.name = "a name differing only by case is refused where case is folded",
     .run = a_name_differing_only_by_case_is_refused_where_case_is_folded},
    {.name = "a change of case in place is not a collision",
     .run = a_change_of_case_in_place_is_not_a_collision},
    {.name = "a folded name held by a departing member is sequenced",
     .run = a_folded_name_held_by_a_departing_member_is_sequenced},
    {.name = "an entry supplied twice is planned once",
     .run = an_entry_supplied_twice_is_planned_once},
    {.name = "a missing source is refused before anything is written",
     .run = a_missing_source_is_refused_before_anything_is_written},
    {.name = "entries in different directories do not collide",
     .run = entries_in_different_directories_do_not_collide},
    {.name = "directories are renamed like any other entry",
     .run = directories_are_renamed_like_any_other_entry},
    {.name = "a plan with no changes writes nothing", .run = a_plan_with_no_changes_writes_nothing},
    {.name = "a plan whose target was taken since is refused",
     .run = a_plan_whose_target_was_taken_since_is_refused},
    {.name = "a plan whose source vanished is refused",
     .run = a_plan_whose_source_vanished_is_refused},
    {.name = "a plan whose source was replaced is refused",
     .run = a_plan_whose_source_was_replaced_is_refused},
    {.name = "a closed cycle is recognized before it is applied",
     .run = a_closed_cycle_is_recognized_before_it_is_applied},
    {.name = "a closed cycle exchanges both entries", .run = a_closed_cycle_exchanges_both_entries},
    {.name = "a reported rename never names a working entry",
     .run = a_reported_rename_never_names_a_working_entry},
    {.name = "a failure partway through reports exactly what was done",
     .run = a_failure_partway_through_reports_exactly_what_was_done},
    {.name = "an interrupted cycle puts the working entry back",
     .run = an_interrupted_cycle_puts_the_working_entry_back},
    {.name = "an interrupted cycle reports an entry it cannot put back",
     .run = an_interrupted_cycle_reports_an_entry_it_cannot_put_back},
    {.name = "a batch is recorded as one record per completed rename",
     .run = a_batch_is_recorded_as_one_record_per_completed_rename},
    {.name = "reversing a batch record by record restores every name",
     .run = reversing_a_batch_record_by_record_restores_every_name},
    {.name = "reversing a sequenced batch finds each name free again",
     .run = reversing_a_sequenced_batch_finds_each_name_free_again},
    {.name = "a batch that needed a working name is never offered as reversible",
     .run = a_batch_that_needed_a_working_name_is_never_offered_as_reversible},
    {.name = "a record is only offered after its rename completed",
     .run = a_record_is_only_offered_after_its_rename_completed},
    {.name = "a refused plan is never recorded", .run = a_refused_plan_is_never_recorded},
};

/// The scenario count this suite is expected to run. A scenario left out of
/// the table, or removed from it, fails here rather than passing quietly.
constexpr std::size_t expected_scenarios = 47;

} // namespace

int main() {
    std::size_t ran = 0;
    for (const Scenario& scenario : scenarios) {
        scenario.run();
        ++ran;
    }

    check(std::size(scenarios) == expected_scenarios,
          "the scenario table holds every scenario the suite claims");
    check(ran == std::size(scenarios), "every listed scenario ran");
    std::fputs(("bulk rename: " + std::to_string(ran) + " scenarios ran\n").c_str(), stdout);
    return odysea::test::report("core_bulk_rename");
}

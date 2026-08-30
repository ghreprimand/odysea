#!/usr/bin/env bash
# Holds the split development record together: DEVLOG.md carries the newest
# entries, older ones live in docs/devlog/, and the live file indexes them.
# Without this guard the arrangement decays quietly - an archive nobody links
# to is an entry nobody can find, and a link to a file that was never tracked
# is a dangling reference in a published record.
#
# A month is archived one of two ways, never both. A closed month moves whole
# into YYYY-MM.md. A month that reaches the tracked-file line ceiling before it
# closes is archived in numbered YYYY-MM-partN.md files, each holding a
# consecutive stretch of it, with the newest entries staying in the live
# record. Parts are not merged back when the month closes; a part that has been
# published is as settled as a closed month.
#
# The failure mode of any split is silent loss: an entry that survives neither
# the move nor a later rebase is gone, and nothing about the remaining files
# looks wrong. Structure alone cannot see that, so the record carries a
# manifest of every published entry heading in reading order, and this gate
# requires the manifest and the files to agree exactly - so within one tree an
# entry cannot be reworded, reordered, or duplicated without the gate saying
# so.
#
# The manifest cannot bound the record from below, and it was wrong to say it
# did. It is a tracked file, so the change that drops an entry drops its
# manifest line in the same commit, and every structural rule here then holds
# over a record with a hole in it: nothing duplicated, nothing misordered,
# manifest and files in exact agreement. A record cannot be bounded by a file
# whoever removes the entry also writes.
#
# What bounds it is history. Walking the published branch yields every entry
# ever published and the text it was published with, and the comparison is
# non-forgetting: an entry deleted three commits ago is still demanded today.
#
# The walk is --full-history deliberately. Default history simplification
# follows one parent through a merge that is TREESAME to it for the paths
# given, and discards the other side entirely - published entries with it. A
# branch that touches nothing in the record, merged with the record files
# resolved to that branch's older version, is TREESAME to it, so an entry
# published on the other parent is never walked and never demanded. That is not
# hypothetical here: this branch carries merge commits whose record-touching
# parents simplification drops.
#
# HOW FAR HISTORY BOUNDS THE COMMIT UNDER TEST, exactly. The commits it reads
# cannot be edited by the change being judged. The choice of ref can be: this
# gate runs after the commit exists, so a baseline of local `main` would
# compare a just-committed rewrite against itself and approve it. The baseline
# is therefore `origin/main` where it resolves - the published branch is the
# remote one - which catches every feature branch and every local commit that
# has not been pushed. The residue is real and is not closed here: a rewrite
# committed on a local `main` that has already been fast-forwarded past it, in
# a clone with no `origin/main`, is judged against itself. What remains is the
# window between committing on the integration branch and pushing it.
#
# HOW BIG THE BOUND HAS TO BE. Everything the comparison demands is an entry
# the walk found, so the size of the walk is the strength of the check, and a
# walk that reads less than it read before reports success in the same shape as
# one that read everything. The count therefore carries a floor, described at
# the point where it is applied, and naming a baseline at all is a precondition
# rather than something this gate may pass over.
#
# WHAT THIS GATE DOES NOT DO, stated exactly, because the division matters. In
# a repository, loss and alteration are both mechanically caught - history is
# the copy of what was published that the comparison needs. In a source tree
# extracted from a release archive there is no history, only the manifest, so
# what remains checkable there is the record's internal agreement: no entry
# duplicated, misordered, misfiled, or unrecorded. Body text is unbounded there
# and the run says so by name rather than reporting a check it did not make.
set -euo pipefail

# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"
guard_corpus_init devlog_archive_guard

readonly live_record="DEVLOG.md"
readonly archive_directory="docs/devlog"
readonly manifest="$archive_directory/published-entries.txt"
readonly entry_expression='^## [0-9]{4}-[0-9]{2}-[0-9]{2} '
readonly archive_name_expression='^[0-9]{4}-[0-9]{2}(-part[0-9]+)?\.md$'

status=0

fail() {
    printf 'devlog_archive_guard: %s\n' "$1" >&2
    status=1
}

if [[ ! -f "$live_record" ]]; then
    fail "$live_record is missing"
    exit 1
fi

entry_headings() {
    grep -E "$entry_expression" "$1" || true
}

entry_dates() {
    entry_headings "$1" | sed -E 's/^## ([0-9]{4}-[0-9]{2}-[0-9]{2}) .*$/\1/'
}

entry_months() {
    entry_dates "$1" | sed -E 's/^([0-9]{4}-[0-9]{2})-[0-9]{2}$/\1/'
}

# The month and part a file name denotes. A whole-month file is part 0, which
# is only ever compared against other parts of the same month - and a month may
# not hold both forms, so the two never meet.
archive_month() {
    local name="${1##*/}"
    printf '%s' "${name:0:7}"
}

archive_part() {
    local name="${1##*/}"
    name="${name%.md}"
    if [[ "$name" == *-part* ]]; then
        printf '%s' "${name##*-part}"
    else
        printf '0'
    fi
}

# --- Archive files must be named for what they contain ----------------------
archive_files=()
while IFS= read -r path; do
    [[ -n "$path" ]] || continue
    # The manifest lives beside the archives but is not one of them.
    [[ "${path##*/}" == "${manifest##*/}" ]] && continue
    archive_files+=("$path")
done < <(guard_corpus_list "$archive_directory/*" | tr '\0' '\n')

declare -A month_has_whole_file=()
declare -A month_part_numbers=()
# The months that are held in parts, in the order first seen. Kept as a plain
# list rather than read back out of the associative array's keys: the key
# expansion form carries an at-sign, and the publishing guard bans that byte
# wherever shell syntax does not require it. Nothing here requires it.
parted_months=""

for path in "${archive_files[@]:+${archive_files[@]}}"; do
    name="${path##*/}"
    if [[ ! "$name" =~ $archive_name_expression ]]; then
        fail "archive file $path is not named YYYY-MM.md or YYYY-MM-partN.md"
        continue
    fi

    month="$(archive_month "$path")"
    part="$(archive_part "$path")"

    if [[ "$part" == "0" ]]; then
        month_has_whole_file["$month"]=1
    else
        if ((10#$part < 1)); then
            fail "archive $path is numbered from zero; parts start at one"
        fi
        if [[ -z "${month_part_numbers[$month]:-}" ]]; then
            parted_months="$parted_months $month"
        fi
        month_part_numbers["$month"]="${month_part_numbers[$month]:-} $part"
    fi

    if [[ ! -f "$path" ]]; then
        fail "tracked archive $path is missing from the working tree"
        continue
    fi

    if [[ -z "$(entry_headings "$path")" ]]; then
        fail "archive $path contains no dated entry"
        continue
    fi

    while IFS= read -r entry_month; do
        if [[ "$entry_month" != "$month" ]]; then
            fail "archive $path contains a $entry_month entry"
        fi
    done < <(entry_months "$path" | sort -u)
done

# --- A month is archived whole or in parts, never both ----------------------
for month in $parted_months; do
    if [[ -n "${month_has_whole_file[$month]:-}" ]]; then
        fail "$month is archived both whole and in parts; it must be one or the other"
    fi

    # Part numbers run from one without gaps or repeats. A gap means a part was
    # written and never tracked, which is exactly how a stretch of the record
    # goes missing while every remaining file still looks well formed.
    expected=1
    while IFS= read -r part; do
        if ((10#$part != expected)); then
            fail "$month parts are numbered ${month_part_numbers[$month]# }; expected consecutive numbers from one"
            break
        fi
        expected=$((expected + 1))
    done < <(printf '%s\n' ${month_part_numbers[$month]} | sort -n)
done

# --- Reading order, derived rather than read from the index -----------------
# Most recent first: month descending, and within a month part descending. The
# order is computed here and the live record's link block is checked against
# it, so a mis-ordered index cannot quietly define the order it is judged by.
ordered_archives=()
while IFS= read -r path; do
    [[ -n "$path" ]] && ordered_archives+=("$path")
done < <(
    for path in "${archive_files[@]:+${archive_files[@]}}"; do
        # Zero-padded so the sort is numeric without needing a numeric sort
        # key, which would otherwise order part 10 before part 2.
        part_number=$((10#$(archive_part "$path")))
        printf '%s\t%03d\t%s\n' "$(archive_month "$path")" "$part_number" "$path"
    done | sort -r -k1,1 -k2,2 | cut -f3
)

# --- Every archive is linked, and every link resolves -----------------------
linked_paths=()
while IFS= read -r link; do
    [[ -n "$link" ]] && linked_paths+=("$link")
done < <(grep -oE "\\($archive_directory/[^)]+\\)" "$live_record" |
    sed -E 's/^\((.*)\)$/\1/')

contains_value() {
    local needle="$1"
    shift
    local candidate
    for candidate in "$@"; do
        [[ "$candidate" == "$needle" ]] && return 0
    done
    return 1
}

for path in "${archive_files[@]:+${archive_files[@]}}"; do
    if ! contains_value "$path" "${linked_paths[@]:+${linked_paths[@]}}"; then
        fail "archive $path is not linked from $live_record"
    fi
done

for link in "${linked_paths[@]:+${linked_paths[@]}}"; do
    if ! contains_value "$link" "${archive_files[@]:+${archive_files[@]}}"; then
        fail "$live_record links $link, which is not a tracked archive file"
    fi
done

# --- The index reads in the order the record reads --------------------------
if ((${#linked_paths[@]} == ${#ordered_archives[@]})); then
    for ((index = 0; index < ${#ordered_archives[@]}; index++)); do
        if [[ "${linked_paths[index]}" != "${ordered_archives[index]}" ]]; then
            fail "archive links are not most-recent-first: expected ${ordered_archives[index]} where $live_record lists ${linked_paths[index]}"
            break
        fi
    done
fi

# --- The live record holds only entries newer than the archive --------------
# Compared by date rather than by month, because parts divide a month that the
# live record is still adding to.
newest_archived_date=""
for path in "${archive_files[@]:+${archive_files[@]}}"; do
    [[ -f "$path" ]] || continue
    while IFS= read -r entry_date; do
        if [[ -z "$newest_archived_date" || "$entry_date" > "$newest_archived_date" ]]; then
            newest_archived_date="$entry_date"
        fi
    done < <(entry_dates "$path")
done

if [[ -n "$newest_archived_date" ]]; then
    while IFS= read -r entry_date; do
        if [[ "$entry_date" < "$newest_archived_date" ]]; then
            fail "$live_record holds a $entry_date entry older than the archived $newest_archived_date"
        fi
    done < <(entry_dates "$live_record" | sort -u)
fi

# --- A new entry belongs in the live record ---------------------------------
# An entry newer than everything the live record holds, sitting in an archive,
# means a new entry was written into an archive file. That is how an entry ends
# up published somewhere nobody reads first.
newest_live_date="$(entry_dates "$live_record" | sort | tail -n1)"
if [[ -n "$newest_live_date" && -n "$newest_archived_date" &&
    "$newest_archived_date" > "$newest_live_date" ]]; then
    fail "an archived $newest_archived_date entry is newer than every entry in $live_record"
fi

# --- Parts of one month hold disjoint, increasing stretches -----------------
for month in $parted_months; do
    previous_newest=""
    while IFS= read -r part; do
        path="$archive_directory/$month-part$part.md"
        [[ -f "$path" ]] || continue
        oldest="$(entry_dates "$path" | sort | head -n1)"
        newest="$(entry_dates "$path" | sort | tail -n1)"
        if [[ -n "$previous_newest" && "$oldest" < "$previous_newest" ]]; then
            fail "$path starts at $oldest, which is not after the previous part's $previous_newest"
        fi
        previous_newest="$newest"
    done < <(printf '%s\n' ${month_part_numbers[$month]} | sort -n)
done

# --- No entry appears twice -------------------------------------------------
all_headings() {
    entry_headings "$live_record"
    for path in "${ordered_archives[@]:+${ordered_archives[@]}}"; do
        [[ -f "$path" ]] && entry_headings "$path"
    done
}

duplicates="$(all_headings | sort | uniq -d)"
if [[ -n "$duplicates" ]]; then
    while IFS= read -r heading; do
        fail "entry appears more than once: ${heading}"
    done <<<"$duplicates"
fi

# --- Reading order runs newest first, from the baseline entry upward --------
# The record's own header promises reverse-chronological order and nothing
# checked it, so the promise drifted from the file. It drifted through
# integration rather than through authorship: a branch appends its entry at the
# top, every rebase onto an advanced main collides there, and the resolution
# puts the replayed entry back on top - so a branch written days before it lands
# is published above entries dated after it. Each date is honest about its own
# commit; landing order and date order are what disagree. The fix at authoring
# time is to date an entry the day it lands, which is the day it is published.
#
# WHY THE CHECK IS BOUNDED. Published entries are never edited, reordered, or
# removed, and the record already contains this disorder, so a gate demanding
# order over the whole record would demand that published text move. It would
# fail on the day it landed and be deleted the day after. The order is
# therefore enforced from a fixed baseline entry upward: everything published
# after the rule existed, and nothing published before it.
#
# WHAT IT CANNOT CATCH, exactly:
#   * Any disorder below the baseline. That is the point, not an oversight: the
#     existing disorder stays visible in the record rather than being rewritten
#     out of it, and whether it is corrected at all is not this gate's call.
#   * A wrong date. Order is checked against the dates the record carries, so
#     an entry dated a day late reads as ordered. Nothing here compares an
#     entry's date against its commit's date.
#   * The order of entries sharing one date, which is unconstrained by design -
#     several entries land on one day and their relative order carries no
#     claim.
#   * Its own baseline moving. The baseline is a constant in this file, so
#     advancing it would exempt new entries; that is a visible edit to a gate,
#     the same exposure every gate here has, and the constant carries the rule
#     against it rather than relying on the reader to infer it.
#
# The baseline is the newest entry published when this check landed. It must
# never be advanced: moving it forward retires the rule for every entry between
# the old and new positions. It changes only if the entry it names is somehow
# unpublishable, and no published entry is ever removed, so it does not change.
readonly ordering_baseline='## 2026-08-11 -- The prohibition is published, not only the mechanism'

if ! all_headings | grep -Fxq -- "$ordering_baseline"; then
    # A baseline that names nothing checks nothing, so this is a failure rather
    # than an empty pass: without it the section below would silently compare
    # the whole record and report success on any input at all.
    fail "the ordering baseline entry is not published, so reading order is unchecked: ${ordering_baseline}"
else
    # Both sides of a violating pair are named. The entry a comparison trips on
    # is usually the older one's neighbour rather than the entry that was
    # misplaced, so reporting one heading alone would point at the wrong text.
    while IFS= read -r message; do
        [[ -n "$message" ]] && fail "$message"
    done < <(all_headings | awk -v baseline="$ordering_baseline" '
        {
            date = substr($0, 4, 10)
            if (previous_date != "" && date > previous_date) {
                printf "reading order is not newest-first: %s is published below %s; a new entry belongs at the top of the record carrying the date it lands\n", $0, previous_heading
            }
            previous_date = date
            previous_heading = $0
        }
        $0 == baseline { exit }
    ')
fi

# --- A heading's date is the date its commit was made -----------------------
# The ordering check above compares entries against each other using the dates
# the record carries, and says so: it cannot see a wrong date, because a record
# where every entry is misdated by the same day is perfectly ordered. That hole
# is not hypothetical. Twelve of this record's entries were published carrying a
# date other than their commit's, and the shape recurs -- an entry written near
# midnight is dated in UTC while the commit is recorded in local time, so the
# heading runs a day ahead of the commit that published it.
#
# The rule is therefore stated against something outside the record: an entry's
# heading date must equal the date of the commit that first added that heading.
# `%as` is used rather than a formatted date because it renders the author date
# in the commit's OWN recorded timezone, so the comparison does not change
# meaning when a different reader runs it.
#
# WHY THIS READS LOCAL HISTORY AND NOT THE PUBLISHED BRANCH. Every other history
# check here reads `origin/main`, deliberately, because it is the one ref the
# commit under test cannot have written. This check must do the opposite. A
# misdated entry is fixable only while it is unpushed -- published entries are
# never edited -- so a check that waited for `origin/main` would first fire on
# text it is already too late to correct, and would then stay red forever on an
# entry nobody is permitted to touch. A permanently red gate over unfixable text
# is a gate that gets deleted, which is the reasoning the ordering baseline
# above already carries. Reading `HEAD` reports the mistake while amending is
# still allowed, which is the only moment the report is worth anything.
#
# WHAT IT CANNOT CATCH, exactly:
#   * An entry that is not committed yet. Its heading has been added to no
#     commit, so there is no date to compare against; it is counted as unchecked
#     and named, not passed. A pre-commit run therefore checks the entries
#     already in history and says how many it compared.
#   * Any entry published at or below the baseline, for the same reason the
#     ordering check is bounded: the record already contains twelve of them,
#     published entries never move, and a gate demanding they change would be
#     removed rather than obeyed.
#   * A commit whose own date is wrong. The comparison is between two things the
#     commit asserts, so a clock set wrong at commit time satisfies it.
#   * A heading re-added later -- an archive move -- does not confuse it: the
#     walk runs newest-commit-first and keeps overwriting, so the value left
#     standing is the OLDEST addition, which is the original publication.
readonly dating_baseline='## 2026-08-20 -- A declared compositor now has to be one this run created'

if ! guard_corpus_is_git; then
    printf 'devlog_archive_guard: heading dates are UNCHECKED: no Git metadata is available\n'
elif [[ -z "$(git rev-list --all --max-count=1 2>/dev/null)" ]]; then
    printf 'devlog_archive_guard: heading dates are UNCHECKED: the repository holds no commit to read\n'
elif ! all_headings | grep -Fxq -- "$dating_baseline"; then
    # A baseline naming nothing bounds nothing, so nothing is compared: without
    # a place to stop, the loop below would run to the end of the record and
    # demand a date of every entry the bound exists to exempt.
    #
    # This reports rather than fails, for the reason the bound's own anchor
    # does. The baseline is a published entry, and a published entry that has
    # gone missing from the record is already refused by name a few checks
    # below - "a published entry is present nowhere in the record". So the rule
    # going inert costs a failure elsewhere rather than nothing, and the
    # self-test pins that keeper rather than this comment asserting it.
    printf 'devlog_archive_guard: the heading-date rule does not apply here: %s is not published\n' \
        "$dating_baseline"
else
    dates_compared=0
    while IFS=$'\t' read -r kind first second; do
        case "$kind" in
        COMPARED) dates_compared="$first" ;;
        FAIL) fail "$first" ;;
        NOTE) printf 'devlog_archive_guard: %s\n' "$first" ;;
        esac
    done < <(
        awk -F'\t' -v baseline="$dating_baseline" '
            # First stream: every heading addition in local history, newest
            # commit first. Overwritten rather than kept first-seen, so what
            # survives is the oldest addition - the commit that published it.
            /^C\t/ { commit_date = $3; next }
            /^\+## / { added[substr($0, 2)] = commit_date; next }
            # Second stream: the record as it reads now, newest entry first.
            /^H\t/ {
                heading = $2
                if (heading in added) {
                    compared++
                    heading_date = substr(heading, 4, 10)
                    if (heading_date != added[heading])
                        printf "FAIL\ta heading carries a date its commit does not: %s was added by a commit dated %s; date an entry the day it lands, in the same timezone the commit records\n", heading, added[heading]
                } else {
                    printf "NOTE\theading date not yet checkable, as this entry is in no commit: %s\n", heading
                }
                if (heading == baseline) exit
            }
            END { printf "COMPARED\t%d\t\n", compared }
        ' < <(
            git log --full-history --format="C%x09%H%x09%as" -p --no-color -- \
                "$live_record" "$archive_directory" 2>/dev/null
            all_headings | sed 's/^/H\t/'
        )
    )

    # A floor. Every comparison above holds vacuously over a walk that found no
    # additions at all: each entry would fall into the uncommitted branch and
    # the section would report success having compared nothing. The baseline is
    # published - checked above - and it is in history by construction, so it
    # alone guarantees at least one comparison whenever the walk is working.
    if ((dates_compared == 0)); then
        fail "no heading date was compared against a commit, though $dating_baseline is published; the history walk found no entry additions"
    else
        printf 'devlog_archive_guard: %d heading date(s) compared against the commit that published them, down to the dating baseline\n' \
            "$dates_compared"
    fi
fi

# --- The manifest and the files agree exactly -------------------------------
# This is the check that bounds the record from below. Every other check here
# is satisfied by a record with entries missing from it.
if [[ ! -f "$manifest" ]]; then
    fail "the entry manifest $manifest is missing"
else
    # Selected by the entry pattern rather than by stripping comment lines: an
    # entry heading begins with a hash itself, so a comment marker cannot be
    # told from a heading by its first character.
    recorded_headings="$(grep -E "$entry_expression" "$manifest" || true)"
    actual_headings="$(all_headings)"

    recorded_count="$(printf '%s' "$recorded_headings" | grep -c . || true)"
    actual_count="$(printf '%s' "$actual_headings" | grep -c . || true)"

    if ((recorded_count == 0)); then
        fail "the entry manifest records no entry, so it constrains nothing"
    elif [[ "$recorded_headings" != "$actual_headings" ]]; then
        fail "the record and its manifest disagree ($actual_count entries present, $recorded_count recorded)"

        missing="$(comm -23 <(printf '%s\n' "$recorded_headings" | sort) \
            <(printf '%s\n' "$actual_headings" | sort))"
        if [[ -n "$missing" ]]; then
            while IFS= read -r heading; do
                fail "recorded entry is no longer present: ${heading}"
            done <<<"$missing"
        fi

        unrecorded="$(comm -13 <(printf '%s\n' "$recorded_headings" | sort) \
            <(printf '%s\n' "$actual_headings" | sort))"
        if [[ -n "$unrecorded" ]]; then
            while IFS= read -r heading; do
                fail "entry is present but not recorded in the manifest: ${heading}"
            done <<<"$unrecorded"
        fi

        if [[ -z "$missing" && -z "$unrecorded" ]]; then
            fail "the same entries are recorded in a different order than they are published"
        fi
    fi
fi

# --- What was published, read from history rather than from a tracked file ---
# Everything above is satisfied by a lossy record whose manifest was trimmed to
# match it. History cannot be trimmed by the commit being judged, so the record
# is compared against every state it has ever been published in.
#
# The published branch only, never every ref. Refs that are not ancestors of it
# carry record states that were never published - local checkpoints among them
# - and a baseline built from those would both import unpublished material into
# the standard the public record is judged by and demand entries nobody ever
# published. The guard protecting the record would be the thing contaminating
# it.

# Every (heading, body) pair in the input, one per line: the heading, a tab,
# then the body with its line breaks replaced by a byte that cannot occur in
# the text. Trailing blank lines and a trailing horizontal rule are dropped,
# along with trailing whitespace, because those are the separator between one
# entry and the next rather than part of either: moving an entry to the end of
# a file changes them and changes nothing that was written. Comparing bodies
# byte for byte would call every part split a rewrite and be unusable on the
# day it landed.
#
# The date is spelled out digit by digit rather than with a repetition count,
# which not every awk implementation reads as one. The conflict-marker matcher
# does need an interval because Git's marker size is configurable; the `awk`
# invoked by this guard supports that ERE form. It remains safe for the matcher
# to live here because the pattern sits inside an indented awk program: no line
# of this file begins with a marker, so the corpus guard that bans them can
# still read this one.
#
# Unresolved conflict markers are dropped from a body wherever they appear,
# which is a different exemption from the trailing-separator one above and
# needs its own justification. A marker is not text an entry can legitimately
# be published with; it is damage. Without this, the history-derived baseline
# compares an entry against the last form it was published in - markers
# included - so a record that went out conflicted would have the damage
# adopted as its authoritative text, and the gate protecting the record would
# refuse every attempt to repair it while agreeing with the break. That is not
# a hypothetical: the record was published in exactly that state, and this
# guard passed it.
#
# The exemption is narrow on purpose. Only lines that are entirely a marker are
# removed, so the surrounding entry text is still compared byte for byte and a
# genuine rewrite is still caught. A marker label is dropped wholesale here,
# which is safe only because the corpus guard refuses any marker line that this
# parser accepts. Keep this parser's `[ \t]` terminator a strict subset of the
# corpus guard's `[[:space:]]` terminator; widening the archive exemption makes
# marker-label text invisible to the history comparison. The pair means markers
# cannot enter and removing them is always allowed.
entry_pairs() {
    awk '
        function flush(   position, joined) {
            if (heading == "") return
            while (line_count > 0 &&
                (body[line_count] == "" || body[line_count] ~ /^-{3,}$/))
                line_count--
            joined = ""
            for (position = 1; position <= line_count; position++)
                joined = joined (position > 1 ? separator : "") body[position]
            printf "%s\t%s\n", heading, joined
            heading = ""
            line_count = 0
        }
        BEGIN { separator = sprintf("%c", 1) }
        /^## [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9] / {
            flush()
            heading = $0
            sub(/[ \t]+$/, "", heading)
            next
        }
        /^(<{7,}|\|{7,}|={7,}|>{7,})([ \t]|$)/ { next }
        {
            if (heading != "") {
                line = $0
                sub(/[ \t]+$/, "", line)
                body[++line_count] = line
            }
        }
        END { flush() }
    ' "$@"
}

# The record files one commit held, as blob names. The manifest is skipped: it
# is a list of headings, so read as a record it would present every published
# entry as one with no body at all, and every entry that does have a body would
# be reported as rewritten.
#
# That exclusion is currently unreachable, and it is worth being exact about
# why, because the obvious explanation is the wrong one. It is not the order
# the blobs arrive in: first-seen-wins would indeed let `DEVLOG.md` register a
# heading before the manifest could, but that only holds while no commit is
# missing an entry its manifest still lists, and such commits are ordinary
# intermediate states. What actually makes the exclusion unreachable today is
# the extension test on the line above - the manifest is a `.txt` file, so it
# is already filtered out as a candidate blob before the name is compared.
#
# So the exclusion is the principled check and the extension is the accident.
# Rename this file to `published-entries.md`, or widen that filter, and the
# exclusion becomes the only thing standing between the comparison and a file
# that would report every entry with a body as rewritten. It stays, and the
# self-test pins the combination rather than the exclusion alone, because no
# mutation of the exclusion by itself can be observed while the manifest keeps
# a name the filter rejects.
history_record_blobs() {
    git ls-tree -r "$1" -- "$live_record" "$archive_directory" |
        awk -F'\t' -v manifest="$manifest" '
            {
                split($1, metadata, " ")
                if ($2 ~ /\.md$/ && $2 != manifest) print metadata[3]
            }'
}

# Every pair the branch has ever held, newest commit first.
#
# The two streams below are tagged and read as one, rather than passed as two
# files distinguished by record number. An awk comparison keyed on the first
# file's record count silently reads the second file as the first when the
# first is empty, which turns "nothing was ever published" into "everything
# present was published and nothing present is" - and that is a real defect
# living in another gate in this directory, found by the scenario that
# publishes nothing.
published_pairs() {
    local commit blob
    while IFS= read -r commit; do
        while IFS= read -r blob; do
            git cat-file blob "$blob" | entry_pairs | sed 's/^/published\t/'
        done < <(history_record_blobs "$commit")
        # --full-history: see the note at the top. Without it a merge that is
        # TREESAME to one parent for these paths hides everything published on
        # the other.
    done < <(git rev-list --full-history "$baseline_ref" -- \
        "$live_record" "$archive_directory")
}

# Every pair the working tree holds, tagged with the file holding it.
current_pairs() {
    local path
    for path in "$live_record" "${ordered_archives[@]:+${ordered_archives[@]}}"; do
        [[ -f "$path" ]] || continue
        entry_pairs "$path" | awk -v source="$path" '
            {
                position = index($0, "\t")
                printf "current\t%s\t%s\t%s\n", substr($0, 1, position - 1),
                    source, substr($0, position + 1)
            }'
    done
}

# --- A bound that cannot quietly get smaller --------------------------------
# Everything the walk demands is an entry it found. That makes the size of the
# walk the whole strength of the check, and nothing compared that size against
# anything: the count went out on the success line and no rule said it could
# not fall. A bound that reads less than it read yesterday reports success in
# exactly the same shape as one that read everything.
#
# It falls for ordinary reasons, not only adversarial ones. A baseline ref left
# behind by a fetch that never ran drops every entry published since; a walk
# weakened later - a lost `--full-history`, a narrowed pathspec, a shallow
# clone - drops whatever it can no longer see. In each case the entries that
# leave the bound become deletable without complaint, and the only trace is a
# number nobody is comparing.
#
# So the count carries a floor. Published entries are never removed from the
# record, so the number of entries the branch has published only ever grows: a
# reading below the mark is a statement about the instrument, not about the
# record.
#
# WHY THE FLOOR IS ANCHORED TO AN ENTRY. A bare count would be a claim about
# every record this script is ever pointed at, including the scenario records
# the accompanying self-test builds, which publish a handful of entries by
# design. The floor is a fact about THIS record, so it is conditioned on this
# record: the anchor is the oldest entry ever published here, it sits in a
# closed archive month, and published entries are never edited or removed.
#
# The anchor cannot go silently absent and take the floor with it. It lives in
# an archive file rather than the live record, so a walk that stopped finding
# it would leave it present in an archive and unpublished, which the comparison
# above refuses by name as a new entry written into that archive. The floor
# going inert therefore costs a failure elsewhere rather than nothing, and the
# self-test pins that keeper rather than asserting it here.
#
# THE COUNT MAY BE RAISED, NEVER LOWERED, and raised only to a number the
# baseline branch has already published - not to whatever the working tree
# holds, or a branch adding entries would fail against a baseline that does not
# have them yet. Lowering it is the cheapest way to make a shrinking bound look
# healthy, which is the exact failure this floor exists to catch.
readonly bound_floor_anchor='## 2026-07-27 -- Initial core and Qt Quick foundation'
readonly bound_floor_count=112

baseline_ref=""
if guard_corpus_is_git; then
    # Most authoritative first. `origin/main` is what was actually published,
    # and it is the only candidate the commit under test cannot have written.
    # Local `main` follows it for clones that have no remote configured. `HEAD`
    # is last because it is the one candidate that always resolves in a
    # repository with commits: without it, renaming the branch and the remote -
    # `git branch -m main trunk`, `git remote rename origin upstream` - reaches
    # the unchecked path with the whole of history sitting right there. It
    # cannot reintroduce an empty baseline either, since the floor below judges
    # what the walk found rather than which name found it.
    for candidate in origin/main main HEAD; do
        if git rev-parse --verify --quiet "$candidate" >/dev/null 2>&1; then
            baseline_ref="$candidate"
            break
        fi
    done
fi

if [[ -z "$baseline_ref" ]]; then
    # WHEN A SKIP IS ALLOWED, AND WHEN IT IS A FAILURE. Reporting the bound
    # unchecked is honest, and an honest skip still reads in the summary
    # exactly like a check that ran: both are one line above a green result.
    # So the skip is permitted only where there is genuinely nothing to read,
    # and refused wherever the history it needs is sitting in the tree.
    #
    # A tarball has no history: the manifest is the whole of what can be
    # checked there, and a gate that declined outright would report success
    # over the source a package build compiles. The same holds for a
    # repository with no commit in it yet.
    #
    # A repository that does have commits is a different case, and it was
    # being passed over. The candidate names above cover every ordinary
    # clone, but they are names, and history does not depend on them: renaming
    # the branch and the remote and then checking out an unborn branch -
    # `git checkout --orphan` - leaves every commit reachable under other refs
    # while none of the three candidates resolves. The bound then went
    # entirely unenforced with the whole record's history present, at exit
    # zero. Naming a baseline is a precondition of this gate, not something it
    # may decline: a run that cannot name one has to fail so somebody names
    # one, rather than publish a green result over an unbounded record.
    history_exists=0
    if guard_corpus_is_git &&
        [[ -n "$(git rev-list --all --max-count=1 2>/dev/null)" ]]; then
        history_exists=1
    fi

    if ((history_exists == 1)); then
        fail "published history is present but no baseline ref could be named: none of origin/main, main, or HEAD resolves, so the record is unbounded"
    elif guard_corpus_is_git; then
        printf 'devlog_archive_guard: published history is UNCHECKED: the repository holds no commit to read\n'
    else
        printf 'devlog_archive_guard: published history is UNCHECKED: no Git metadata is available\n'
    fi
else
    history_published=0
    history_compared=0
    anchor_published=0
    while IFS=$'\t' read -r kind first second; do
        case "$kind" in
        COUNT)
            history_published="$first"
            history_compared="$second"
            ;;
        ANCHOR) anchor_published="$first" ;;
        FAIL) fail "$first" ;;
        esac
    done < <(
        awk -v live="$live_record" -v anchor="$bound_floor_anchor" '
            function next_field(   position, value) {
                position = index(remainder, "\t")
                value = substr(remainder, 1, position - 1)
                remainder = substr(remainder, position + 1)
                return value
            }
            {
                remainder = $0
                tag = next_field()
            }
            # Newest commit first, so the first form seen is the most recently
            # published one and later forms are earlier history. An entry the
            # record once held in another form is measured against what the
            # branch publishes now; against its oldest form the gate would
            # demand text the branch itself no longer carries.
            tag == "published" {
                position = index(remainder, "\t")
                heading = substr(remainder, 1, position - 1)
                if (!(heading in published)) {
                    published[heading] = substr(remainder, position + 1)
                    order[++published_count] = heading
                    # Whether the floor below applies to this record at all is
                    # decided by what the walk found, not by what the working
                    # tree holds: the floor is a claim about the bound.
                    if (heading == anchor) anchor_published = 1
                }
                next
            }
            tag == "current" {
                heading = next_field()
                source = next_field()
                body = remainder

                present[heading] = 1
                if (heading in published) {
                    compared++
                    if (published[heading] != body)
                        printf "FAIL\ta published entry has been rewritten under an unchanged heading: %s\n", heading
                } else if (source != live) {
                    printf "FAIL\ta new entry was written into %s: %s\n", source, heading
                }
            }
            END {
                for (position = 1; position <= published_count; position++)
                    if (!(order[position] in present))
                        printf "FAIL\ta published entry is present nowhere in the record: %s\n", order[position]
                printf "ANCHOR\t%d\t\n", anchor_published
                printf "COUNT\t%d\t%d\n", published_count, compared
            }
        ' < <(
            published_pairs
            current_pairs
        )
    )

    # A floor, because every comparison above holds vacuously over a baseline
    # that read nothing, and a resolution mistake presents exactly that way.
    if ((history_published == 0)); then
        fail "no entry has ever been published in $baseline_ref, so history bounded nothing"
    else
        printf 'devlog_archive_guard: %d entries published in %s, %d compared against their published text\n' \
            "$history_published" "$baseline_ref" "$history_compared"
    fi

    # The floor on the size of that bound. Which branch was taken is reported
    # either way: a floor that had quietly stopped applying would otherwise be
    # indistinguishable from one that passed.
    if ((anchor_published == 1)); then
        if ((history_published < bound_floor_count)); then
            fail "the bound read $history_published published entries in $baseline_ref, fewer than the $bound_floor_count this record had already published; published entries are never removed, so the bound has weakened rather than the record shrinking"
        else
            printf 'devlog_archive_guard: the bound holds %d entries against a floor of %d\n' \
                "$history_published" "$bound_floor_count"
        fi
    else
        printf 'devlog_archive_guard: the entry-count floor does not apply here: %s has never been published in %s\n' \
            "$bound_floor_anchor" "$baseline_ref"
    fi
fi

# --- Entry boundaries -------------------------------------------------------
# An entry is separated from the entry below it by one blank line, a horizontal
# rule alone on its line, and one blank line before the heading. The same three
# lines close a record file's opening paragraphs above its first entry. Without
# the rule an entry's extent is invisible: a heading alone reads as a section of
# the entry above it, and every archive move depends on knowing where one entry
# stops.
#
# THE RECORD PREDATES THE RULE. The convention was written down long after the
# record started and the record is inconsistent before that: when this check
# landed, 81 of 142 published boundaries did not meet it. Published text is not
# rewritten to suit a later rule, so those boundaries are listed by heading in
# the census file below and left as published.
#
# WHY A CENSUS AND NOT AN EXEMPTION. An exemption says only that a published
# boundary may lack a separator, and that permits the failure this check exists
# for: a published boundary quietly acquiring one. That happens without anyone
# deciding to rewrite the record - a merge resolution that replays an entry and
# normalises the whitespace around it produces exactly that edit, and it looks
# like tidying in a diff. The census is therefore read in both directions. A
# boundary that does not conform and is not listed is a new entry breaking the
# rule. A boundary that is listed and now carries a separator is published text
# repaired in place. Both fail.
#
# KEYED BY HEADING ALONE, because a heading is fixed for the life of the record
# while the file holding it changes at every split. An entry that becomes the
# first of an archive part has no entry above it and so has no entry boundary;
# it keeps its census line, and the opening of the file it now heads is checked
# by the file-opening rule instead. That transition is the only way a listed
# boundary may stop being compared, and it is a structural move this gate
# already constrains, not an edit to published text.
#
# THIS CHECK NEVER WRITES. Nothing here opens a record file for writing, so it
# cannot normalise what it is measuring. A gate that repaired the record would
# be the same act as the merge resolution it is here to catch.
readonly boundary_census="tools/devlog_boundary_census.txt"

# The floor applies once the record still reaches back to this entry, which
# lives in a closed archive and can therefore never be edited or removed. It is
# deliberately not the entry the history bound is anchored on: these are two
# different questions, and one constant answering both would tie the size of
# this list to how far history happens to be readable.
readonly boundary_census_floor_anchor='## 2026-07-27 -- Asynchronous shell model and navigation state'
readonly boundary_census_floor=81

if [[ ! -f "$boundary_census" ]]; then
    fail "the entry-boundary census $boundary_census is missing, so no boundary was compared against what was published"
else
    declare -A census_listed=()
    declare -A census_seen=()
    census_count=0
    while IFS= read -r line; do
        # Only heading lines carry data. Everything else in the file is
        # commentary, and a heading marker is itself a comment character, so
        # the test is for the heading shape rather than against a comment one.
        [[ "$line" == '## '* ]] || continue
        census_listed["$line"]=1
        census_count=$((census_count + 1))
    done <"$boundary_census"

    # A boundary's shape, reported per heading: the three lines above it must be
    # a blank line, a horizontal rule alone on its line, and a blank line. A
    # rule with different spacing around it is not the convention and is not
    # accepted as one - three boundaries in the census carry a rule and are
    # listed anyway for that reason.
    boundary_shapes() {
        awk -v expression="$entry_expression" '
            $0 ~ expression {
                seen++
                conforming = (first == "" && second == "---" && third == "")
                printf "%s|%s|%s\n", (seen == 1 ? "opening" : "boundary"), \
                    (conforming ? "conforming" : "broken"), $0
            }
            { third = second; second = first; first = $0 }
        ' "$1"
    }

    boundaries_checked=0
    boundaries_pinned=0
    boundaries_examined=0
    for path in "$live_record" "${ordered_archives[@]:+${ordered_archives[@]}}"; do
        [[ -f "$path" ]] || continue
        while IFS='|' read -r position form heading; do
            [[ -n "$heading" ]] || continue
            boundaries_examined=$((boundaries_examined + 1))
            # Recorded for every entry rather than only for listed ones, and
            # recorded before the opening entry is set aside below: an entry
            # that heads a file is still in the record, and a census line
            # naming it is still holding something.
            census_seen["$heading"]=1
            if [[ "$position" == opening ]]; then
                # The first entry of a file has no entry above it. What must
                # close above it is the file's opening paragraphs, in the same
                # three lines, and no census line can excuse that: the opening
                # of a file is written when the file is created, which is
                # always after this rule.
                if [[ "$form" != conforming ]]; then
                    fail "$path does not close its opening paragraphs with a blank line, a horizontal rule, and a blank line above ${heading}"
                fi
                continue
            fi
            boundaries_checked=$((boundaries_checked + 1))
            if [[ -n "${census_listed[$heading]+set}" ]]; then
                boundaries_pinned=$((boundaries_pinned + 1))
                if [[ "$form" == conforming ]]; then
                    fail "the boundary above ${heading} was published without a separator and now carries one, so published text was repaired in place rather than left as published"
                fi
            elif [[ "$form" != conforming ]]; then
                fail "the boundary above ${heading} in $path is not a blank line, a horizontal rule, and a blank line; a new entry is separated from the entry below it"
            fi
        done < <(boundary_shapes "$path")
    done

    # WHAT THE SCAN ACTUALLY READ. Every judgement above is made about a
    # heading the scan emitted, so a scan that emitted nothing reports success
    # in exactly the shape of one that read the whole record: no failure, and a
    # count of zero that nothing compares against. A pattern that will not
    # compile, or a classifier that stops matching headings, presents that way.
    # The record's entry count is known independently here, so the scan is held
    # to it rather than trusted.
    record_entry_count="$(all_headings | grep -c . || true)"
    if ((boundaries_examined != record_entry_count)); then
        fail "the entry-boundary scan read $boundaries_examined of the record's $record_entry_count entries, so boundaries were judged over part of the record or none of it"
    fi

    # A census line naming an entry the record does not hold means the list and
    # the record have parted company - the entry was renamed, or the list was
    # written against a different record. Either way the pin is no longer
    # holding anything.
    while IFS= read -r line; do
        [[ "$line" == '## '* ]] || continue
        if [[ -z "${census_seen[$line]+set}" ]]; then
            fail "the entry-boundary census names an entry the record does not hold: ${line}"
        fi
    done <"$boundary_census"

    # The floor. The census records what was published, so it never shrinks;
    # removing a line is how a repair is made to look like it was always the
    # rule. Which branch was taken is reported either way, because a floor that
    # had quietly stopped applying reads exactly like one that passed.
    if all_headings | grep -Fxq -- "$boundary_census_floor_anchor"; then
        if ((census_count < boundary_census_floor)); then
            fail "the entry-boundary census holds $census_count entries, fewer than the $boundary_census_floor this record published without a separator; published entries are never repaired, so the list has been weakened rather than the record improving"
        else
            printf 'devlog_archive_guard: %d entry boundaries checked, %d pinned as published without a separator, against a census floor of %d\n' \
                "$boundaries_checked" "$boundaries_pinned" "$boundary_census_floor"
        fi
    else
        printf 'devlog_archive_guard: %d entry boundaries checked, %d pinned as published without a separator; the census floor does not apply here: %s is not in the record\n' \
            "$boundaries_checked" "$boundaries_pinned" "$boundary_census_floor_anchor"
    fi
fi

# --- Floors -----------------------------------------------------------------
# Each of the comparisons above holds over a record with nothing in it. A live
# file with no entry is not a record, and neither is one whose entries were all
# archived: the newest entry always belongs in the live file.
live_entry_count="$(entry_headings "$live_record" | grep -c . || true)"
if ((live_entry_count == 0)); then
    fail "$live_record holds no entry, so nothing here was compared against it"
fi

if ((status != 0)); then
    exit "$status"
fi

printf 'devlog_archive_guard: %s and %d archive file(s) hold %d entries matching the manifest\n' \
    "$live_record" "${#archive_files[@]}" \
    "$(all_headings | grep -c . || true)"

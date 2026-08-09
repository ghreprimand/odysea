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
# requires the manifest and the files to agree exactly. That bounds the record
# from below as well as above - an entry cannot be dropped, and it equally
# cannot be reworded, reordered, or duplicated.
#
# WHAT THIS GATE DOES NOT DO, stated because the manifest could be mistaken for
# more than it is. It pins headings, not bodies. Rewriting the text underneath
# an unchanged heading passes untouched, and no check here can see it without a
# copy of what was published to compare against - which a source tree extracted
# from a release archive does not carry. The rule against editing a published
# entry is enforced by review; what is mechanical is that no entry silently
# disappears, changes place, or changes name.
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

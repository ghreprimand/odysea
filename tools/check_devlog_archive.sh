#!/usr/bin/env bash
# Holds the split development record together: DEVLOG.md carries the current
# calendar month, closed months live in docs/devlog/YYYY-MM.md, and the live
# file indexes them. Without this guard the arrangement decays quietly - an
# archive nobody links to is an entry nobody can find, and a link to a file
# that was never tracked is a dangling reference in a published record.
set -euo pipefail

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "devlog_archive_guard: skipped because Git metadata is unavailable"
    exit 77
fi

cd "$(git rev-parse --show-toplevel)"

readonly live_record="DEVLOG.md"
readonly archive_directory="docs/devlog"
readonly entry_expression='^## [0-9]{4}-[0-9]{2}-[0-9]{2} '
readonly archive_name_expression='^[0-9]{4}-[0-9]{2}\.md$'

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

entry_months() {
    entry_headings "$1" | sed -E 's/^## ([0-9]{4}-[0-9]{2})-.*$/\1/'
}

# --- Archive files must be named for the month they contain -----------------
archive_files=()
while IFS= read -r path; do
    [[ -n "$path" ]] && archive_files+=("$path")
done < <(git ls-files -- "$archive_directory")

archive_months=()
for path in "${archive_files[@]:+${archive_files[@]}}"; do
    name="${path##*/}"
    if [[ ! "$name" =~ $archive_name_expression ]]; then
        fail "archive file $path is not named YYYY-MM.md"
        continue
    fi

    month="${name%.md}"
    archive_months+=("$month")

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
        fail "$live_record links $link, which is not a tracked file"
    fi
done

# --- The index reads most recent first --------------------------------------
previous=""
for link in "${linked_paths[@]:+${linked_paths[@]}}"; do
    name="${link##*/}"
    month="${name%.md}"
    if [[ -n "$previous" && ! "$month" < "$previous" ]]; then
        fail "archive links are not in reverse-chronological order: $previous then $month"
    fi
    previous="$month"
done

# --- The live record holds only months newer than the archive ---------------
newest_archived=""
for month in "${archive_months[@]:+${archive_months[@]}}"; do
    if [[ -z "$newest_archived" || "$month" > "$newest_archived" ]]; then
        newest_archived="$month"
    fi
done

if [[ -n "$newest_archived" ]]; then
    while IFS= read -r entry_month; do
        if [[ ! "$entry_month" > "$newest_archived" ]]; then
            fail "$live_record still holds a $entry_month entry; $newest_archived is archived"
        fi
    done < <(entry_months "$live_record" | sort -u)
fi

# --- No entry appears twice -------------------------------------------------
duplicates="$(
    {
        entry_headings "$live_record"
        for path in "${archive_files[@]:+${archive_files[@]}}"; do
            [[ -f "$path" ]] && entry_headings "$path"
        done
    } | sort | uniq -d
)"
if [[ -n "$duplicates" ]]; then
    while IFS= read -r heading; do
        fail "entry appears more than once: ${heading}"
    done <<<"$duplicates"
fi

if ((status != 0)); then
    exit "$status"
fi

printf 'devlog_archive_guard: %s and %d archived month(s) are consistent\n' \
    "$live_record" "${#archive_files[@]}"

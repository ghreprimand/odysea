#!/usr/bin/env bash
# Exercises tools/check_devlog_archive.sh in both directions. A guard that is
# never shown rejecting a broken layout is indistinguishable from one that
# reports success unconditionally, so every scenario below asserts the specific
# reason the guard gives, not merely its exit status.
#
# The scenarios that matter most are the ones covering silent loss. A record
# split across files can lose a stretch of itself and leave every remaining
# file well formed, so the cases below drop an entry, reword one, reorder two,
# and write a new one into an archive, and each has to be refused by name.
set -euo pipefail

readonly script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly guard="$script_directory/check_devlog_archive.sh"

if [[ ! -x "$guard" && ! -f "$guard" ]]; then
    echo "devlog_archive_guard_self_test: guard script is missing" >&2
    exit 1
fi

# Stated as a precondition rather than skipped. The scenarios below need a
# real repository to build, and a self-test that declined to run would report
# nothing while reading as a pass in the summary.
if ! command -v git >/dev/null 2>&1; then
    echo "devlog_archive_guard_self_test: git is required to build the scenario repositories and is not installed" >&2
    exit 1
fi

readonly sandbox_root="$(mktemp -d)"
trap 'rm -rf -- "$sandbox_root"' EXIT

failures=0

report() {
    local outcome="$1" scenario="$2"
    printf '%-5s %s\n' "$outcome" "$scenario"
    [[ "$outcome" == PASS ]] || failures=$((failures + 1))
}

# Writes an archive file holding the named entries.
write_archive() {
    local path="$1"
    shift

    {
        printf '# Devlog archive %s\n\nArchived entries.\n' "$(basename "${path%.md}")"
        local heading
        for heading in "$@"; do
            printf '\n---\n\n## %s\n\nBody text.\n' "$heading"
        done
    } >"$path"
}

# Writes the live record holding the named entries, with an index naming each
# archive path given before the `--` separator.
write_live_record() {
    local root="$1"
    shift

    local -a links=()
    while (($# > 0)) && [[ "$1" != "--" ]]; do
        links+=("$1")
        shift
    done
    shift || true

    {
        printf '# Devlog\n\nLive record. Archived record, most recent first:\n\n'
        local link
        for link in ${links+"${links[@]}"}; do
            printf -- '- [%s](%s)\n' "$(basename "${link%.md}")" "$link"
        done
        local heading
        for heading in "$@"; do
            printf '\n---\n\n## %s\n\nBody text.\n' "$heading"
        done
    } >"$root/DEVLOG.md"
}

# Regenerates the manifest from whatever the files currently hold, in reading
# order: the live record, then archives most recent first.
#
# Derived here independently of the guard rather than by asking the guard what
# it expects. A self-test that took its answer from the code under test would
# agree with a wrong answer.
refresh_manifest() {
    local root="$1"

    {
        printf '# Published entry headings, in reading order.\n'
        grep -E '^## [0-9]{4}-[0-9]{2}-[0-9]{2} ' "$root/DEVLOG.md" || true
        # Sorted by the file name and read through the absolute path. Sorting
        # on a relative name and then reading it would resolve against whatever
        # directory this runs from, which is not the sandbox.
        local name
        while IFS= read -r name; do
            [[ -n "$name" ]] || continue
            grep -E '^## [0-9]{4}-[0-9]{2}-[0-9]{2} ' \
                "$root/docs/devlog/$name" || true
        done < <(find "$root/docs/devlog" -name '*.md' -type f -printf '%f\n' |
            sort -r)
    } >"$root/docs/devlog/published-entries.txt"
}

# Builds a throwaway repository whose August is archived in two parts, with
# July closed whole. This is the layout the split introduced, so the scenarios
# exercise the arrangement the record is actually in.
build_repository() {
    local name="$1"
    local root="$sandbox_root/$name"

    mkdir -p "$root/docs/devlog"

    write_live_record "$root" \
        docs/devlog/2026-08-part2.md \
        docs/devlog/2026-08-part1.md \
        docs/devlog/2026-07.md \
        -- \
        "2026-08-09 -- Ninth August entry" \
        "2026-08-08 -- Eighth August entry"

    write_archive "$root/docs/devlog/2026-08-part2.md" \
        "2026-08-05 -- Fifth August entry" \
        "2026-08-04 -- Fourth August entry"

    write_archive "$root/docs/devlog/2026-08-part1.md" \
        "2026-08-02 -- Second August entry" \
        "2026-08-01 -- First August entry"

    write_archive "$root/docs/devlog/2026-07.md" \
        "2026-07-31 -- Last July entry" \
        "2026-07-01 -- First July entry"

    refresh_manifest "$root"

    # The scenarios never commit, so no identity configuration is required.
    git -C "$root" init --quiet
    printf '%s\n' "$root"
}

track_everything() {
    git -C "$1" add --all
}

run_guard() {
    local root="$1"
    (cd "$root" && bash "$guard") 2>&1
}

expect_accepted() {
    local scenario="$1" root="$2"
    local output exit_status=0
    output="$(run_guard "$root")" || exit_status=$?

    if ((exit_status != 0)); then
        report FAIL "$scenario: guard rejected a consistent layout"
        printf '  %s\n' "$output" >&2
        return
    fi
    # A guard that exits early - before Git metadata, before enumeration -
    # would also exit zero. Require the success line that only the end of the
    # script can print, including the counts it had to compute.
    if [[ "$output" != *"3 archive file(s) hold 8 entries matching the manifest"* ]]; then
        report FAIL "$scenario: guard exited zero without reaching its final report"
        printf '  %s\n' "$output" >&2
        return
    fi
    report PASS "$scenario"
}

expect_rejected() {
    local scenario="$1" root="$2" expected="$3"
    local output exit_status=0
    output="$(run_guard "$root")" || exit_status=$?

    if ((exit_status == 0)); then
        report FAIL "$scenario: guard accepted a broken layout"
        return
    fi
    if [[ "$output" != *"$expected"* ]]; then
        report FAIL "$scenario: rejected for the wrong reason"
        printf '  expected to contain: %s\n' "$expected" >&2
        printf '  actual: %s\n' "$output" >&2
        return
    fi
    report PASS "$scenario"
}

# --- Accepted layout --------------------------------------------------------
root="$(build_repository consistent)"
track_everything "$root"
expect_accepted "a live record, two parts, and a closed month" "$root"

# --- Silent loss: an entry that survives neither file ------------------------
# The failure any split invites. Every structural check above still passes: the
# files are named correctly, ordered correctly, linked correctly, and hold only
# their own month. Only the manifest can see that a stretch is gone.
root="$(build_repository dropped_entry)"
sed -i '/^## 2026-08-01 -- First August entry$/,+3d' \
    "$root/docs/devlog/2026-08-part1.md"
track_everything "$root"
expect_rejected "an entry present in neither the live record nor an archive" "$root" \
    "recorded entry is no longer present: ## 2026-08-01 -- First August entry"

# --- A published entry reworded in place -------------------------------------
root="$(build_repository reworded_entry)"
sed -i 's/^## 2026-08-02 -- Second August entry$/## 2026-08-02 -- Second August entry, revisited/' \
    "$root/docs/devlog/2026-08-part1.md"
track_everything "$root"
expect_rejected "a published entry reworded inside a part" "$root" \
    "recorded entry is no longer present: ## 2026-08-02 -- Second August entry"

# --- Two published entries swapped -------------------------------------------
# The same entries in a different order: no entry is missing and none is new,
# so only the ordered comparison catches it.
root="$(build_repository reordered_entries)"
write_archive "$root/docs/devlog/2026-08-part1.md" \
    "2026-08-01 -- First August entry" \
    "2026-08-02 -- Second August entry"
track_everything "$root"
expect_rejected "two published entries reordered within a part" "$root" \
    "the same entries are recorded in a different order than they are published"

# --- A new entry written into an archive rather than the live record ---------
root="$(build_repository new_entry_in_archive)"
write_archive "$root/docs/devlog/2026-08-part2.md" \
    "2026-08-10 -- An entry written straight into the archive" \
    "2026-08-05 -- Fifth August entry" \
    "2026-08-04 -- Fourth August entry"
refresh_manifest "$root"
track_everything "$root"
expect_rejected "a new entry written into a part instead of the live record" "$root" \
    "an archived 2026-08-10 entry is newer than every entry in DEVLOG.md"

# --- An entry copied instead of moved ---------------------------------------
root="$(build_repository duplicated_entry)"
write_archive "$root/docs/devlog/2026-08-part2.md" \
    "2026-08-08 -- Eighth August entry" \
    "2026-08-05 -- Fifth August entry" \
    "2026-08-04 -- Fourth August entry"
refresh_manifest "$root"
track_everything "$root"
expect_rejected "the same entry in two files" "$root" \
    "entry appears more than once: ## 2026-08-08 -- Eighth August entry"

# --- The manifest itself ----------------------------------------------------
root="$(build_repository missing_manifest)"
rm "$root/docs/devlog/published-entries.txt"
track_everything "$root"
expect_rejected "the manifest is missing" "$root" \
    "the entry manifest docs/devlog/published-entries.txt is missing"

root="$(build_repository empty_manifest)"
printf '# Nothing recorded.\n' >"$root/docs/devlog/published-entries.txt"
track_everything "$root"
expect_rejected "the manifest records nothing" "$root" \
    "records no entry, so it constrains nothing"

# --- A month archived both ways ---------------------------------------------
root="$(build_repository whole_and_parts)"
write_archive "$root/docs/devlog/2026-08.md" \
    "2026-08-03 -- Third August entry"
sed -i 's#- \[2026-08-part2\](docs/devlog/2026-08-part2.md)#- [2026-08](docs/devlog/2026-08.md)\n- [2026-08-part2](docs/devlog/2026-08-part2.md)#' \
    "$root/DEVLOG.md"
refresh_manifest "$root"
track_everything "$root"
expect_rejected "a month archived whole and in parts at once" "$root" \
    "2026-08 is archived both whole and in parts"

# --- A gap in the part numbering --------------------------------------------
# A missing part number is how a whole stretch of the record goes absent while
# every file that remains still looks well formed.
root="$(build_repository part_numbering_gap)"
mv "$root/docs/devlog/2026-08-part2.md" "$root/docs/devlog/2026-08-part3.md"
sed -i 's#2026-08-part2#2026-08-part3#g' "$root/DEVLOG.md"
refresh_manifest "$root"
track_everything "$root"
expect_rejected "part numbers with a gap in them" "$root" \
    "expected consecutive numbers from one"

# --- Parts that do not hold increasing stretches ----------------------------
root="$(build_repository overlapping_parts)"
write_archive "$root/docs/devlog/2026-08-part1.md" \
    "2026-08-06 -- Sixth August entry" \
    "2026-08-01 -- First August entry"
refresh_manifest "$root"
track_everything "$root"
expect_rejected "a later part starting before an earlier one ends" "$root" \
    "which is not after the previous part's"

# --- An archive nobody links to --------------------------------------------
root="$(build_repository unlinked_archive)"
sed -i '/docs\/devlog\/2026-07.md/d' "$root/DEVLOG.md"
track_everything "$root"
expect_rejected "archive present but not linked" "$root" \
    "archive docs/devlog/2026-07.md is not linked"

# --- A link to a file that was never tracked -------------------------------
root="$(build_repository dangling_link)"
sed -i 's#- \[2026-07\](docs/devlog/2026-07.md)#- [2026-06](docs/devlog/2026-06.md)\n- [2026-07](docs/devlog/2026-07.md)#' \
    "$root/DEVLOG.md"
track_everything "$root"
expect_rejected "link to an untracked archive" "$root" \
    "links docs/devlog/2026-06.md, which is not a tracked archive file"

# --- An entry filed under the wrong month ----------------------------------
root="$(build_repository wrong_month)"
sed -i 's/^## 2026-07-01 -- First July entry$/## 2026-06-30 -- Misfiled entry/' \
    "$root/docs/devlog/2026-07.md"
refresh_manifest "$root"
track_everything "$root"
expect_rejected "archive holds an entry from another month" "$root" \
    "archive docs/devlog/2026-07.md contains a 2026-06 entry"

# --- The live record keeps an already-archived entry -----------------------
root="$(build_repository stale_live_entry)"
cat >>"$root/DEVLOG.md" <<'STALE'

---

## 2026-07-15 -- Entry left behind in the live record

Body text.
STALE
refresh_manifest "$root"
track_everything "$root"
expect_rejected "live record holds an entry older than the archive" "$root" \
    "holds a 2026-07-15 entry older than the archived"

# --- The index in the wrong order ------------------------------------------
root="$(build_repository unordered_index)"
write_live_record "$root" \
    docs/devlog/2026-07.md \
    docs/devlog/2026-08-part1.md \
    docs/devlog/2026-08-part2.md \
    -- \
    "2026-08-09 -- Ninth August entry" \
    "2026-08-08 -- Eighth August entry"
track_everything "$root"
expect_rejected "archive index is not most-recent-first" "$root" \
    "archive links are not most-recent-first"

# --- An archive that is not named for a month or a part --------------------
root="$(build_repository misnamed_archive)"
mv "$root/docs/devlog/2026-07.md" "$root/docs/devlog/older-entries.md"
sed -i 's#docs/devlog/2026-07.md#docs/devlog/older-entries.md#' "$root/DEVLOG.md"
track_everything "$root"
expect_rejected "archive file not named YYYY-MM.md or YYYY-MM-partN.md" "$root" \
    "is not named YYYY-MM.md or YYYY-MM-partN.md"

# A name that merely begins with a month is the one that gets through a rule
# written loosely. It is not a part, so nothing orders it against the parts,
# and it would otherwise be read as the whole-month file for a month already
# held in parts.
root="$(build_repository month_prefixed_archive)"
write_archive "$root/docs/devlog/2026-08-extra.md" \
    "2026-08-03 -- Third August entry"
sed -i 's#- \[2026-08-part2\](docs/devlog/2026-08-part2.md)#- [2026-08-extra](docs/devlog/2026-08-extra.md)\n- [2026-08-part2](docs/devlog/2026-08-part2.md)#' \
    "$root/DEVLOG.md"
refresh_manifest "$root"
track_everything "$root"
expect_rejected "an archive named for a month but not a part" "$root" \
    "archive file docs/devlog/2026-08-extra.md is not named YYYY-MM.md or YYYY-MM-partN.md"

# --- An archive with no entries at all -------------------------------------
root="$(build_repository empty_archive)"
cat >"$root/docs/devlog/2026-07.md" <<'EMPTY'
# Devlog archive 2026-07

Nothing was moved here.
EMPTY
refresh_manifest "$root"
track_everything "$root"
expect_rejected "archive contains no dated entry" "$root" \
    "contains no dated entry"

# --- A live record with nothing in it ---------------------------------------
# Every comparison in the guard holds over a live record with no entries, and
# the newest entry always belongs there, so the emptiness is refused by name.
root="$(build_repository emptied_live_record)"
write_live_record "$root" \
    docs/devlog/2026-08-part2.md \
    docs/devlog/2026-08-part1.md \
    docs/devlog/2026-07.md \
    --
refresh_manifest "$root"
track_everything "$root"
expect_rejected "a live record holding no entry at all" "$root" \
    "holds no entry, so nothing here was compared against it"

# --- Without repository metadata the guard runs rather than declining -------
# A copy of the guard is installed in a source tree that has a live record and
# archives but no repository, and it has to reach the same result there. A gate
# that declined here would be reporting success over a build made from a
# release archive, which is exactly what a package build compiles.
without_metadata="$(build_repository without_metadata)"
track_everything "$without_metadata"
rm -rf -- "$without_metadata/.git"
mkdir -p "$without_metadata/tools"
cp "$guard" "$without_metadata/tools/check_devlog_archive.sh"
cp "$script_directory/guard_corpus.sh" "$without_metadata/tools/guard_corpus.sh"
printf 'project(without_metadata)\n' >"$without_metadata/CMakeLists.txt"

metadata_free_status=0
metadata_free_output="$(cd "$without_metadata" &&
    HOME="$without_metadata" GIT_CEILING_DIRECTORIES="$sandbox_root" \
    bash tools/check_devlog_archive.sh 2>&1)" || metadata_free_status=$?
if ((metadata_free_status == 0)) &&
    [[ "$metadata_free_output" == *"no Git metadata is available"* ]] &&
    [[ "$metadata_free_output" == *"matching the manifest"* ]]; then
    report PASS "runs against the source tree when there is no repository"
else
    report FAIL "expected a completed run without repository metadata, got $metadata_free_status"
    printf '  %s\n' "$metadata_free_output" >&2
fi

if ((failures == 0)); then
    echo "devlog_archive_guard_self_test: all scenarios passed"
    exit 0
fi

echo "devlog_archive_guard_self_test: $failures scenario(s) failed" >&2
exit 1

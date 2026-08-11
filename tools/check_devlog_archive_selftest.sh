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

    # Initialised on `main` because the guard's history baseline is that branch
    # by name. Most scenarios never commit, so the branch stays unborn and the
    # history check reports itself unchecked; the scenarios that publish call
    # `publish` below.
    git -C "$root" init --quiet -b main
    # Hooks are neutralised rather than inherited. A global hooks path would
    # otherwise run this repository's attribution hooks against a throwaway
    # commit and fail it for an identity the scenario has no business setting.
    git -C "$root" config core.hooksPath "$root/.git/absent-hooks"
    printf '%s\n' "$root"
}

# Composed at run time rather than written out, because an address in tracked
# text is exactly what the publishing guard exists to refuse.
readonly self_test_identity="devlog-self-test$(printf '\100')example.invalid"

# Commits whatever the scenario currently holds, on `main`. A scenario that
# publishes is the only kind the history baseline can judge.
publish() {
    local root="$1" message="$2"
    shift 2

    git -C "$root" add --all
    git -C "$root" \
        -c "user.name=Devlog self test" \
        -c "user.email=$self_test_identity" \
        -c commit.gpgsign=false \
        commit --quiet -m "$message" "$@"
}

track_everything() {
    git -C "$1" add --all
}

run_guard() {
    local root="$1"
    (cd "$root" && bash "$guard") 2>&1
}

expect_accepted_reporting() {
    local scenario="$1" root="$2" history_report="$3"
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
    # And require it to say what it did about history. Silence there is the one
    # outcome that reads the same whether the baseline was compared or never
    # resolved at all.
    if [[ "$output" != *"$history_report"* ]]; then
        report FAIL "$scenario: guard did not report what it did about published history"
        printf '  expected to contain: %s\n' "$history_report" >&2
        printf '  actual: %s\n' "$output" >&2
        return
    fi
    report PASS "$scenario"
}

# An unpublished scenario: the branch is unborn, so no candidate ref resolves -
# not even HEAD - and the guard has to say so rather than passing over it.
expect_accepted() {
    expect_accepted_reporting "$1" "$2" \
        "published history is UNCHECKED: none of origin/main, main, or HEAD resolves"
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

# --- What history bounds that a tracked manifest cannot ---------------------
# The manifest travels with the record, so the change that drops an entry drops
# its manifest line too and every structural rule still holds. These scenarios
# publish the record first and then judge the working tree against what was
# published, which is the one comparison the commit under test cannot edit.

# Moves an entry out of the live record and into the newest part, which is what
# a part split does. Both writers emit the same body, so the move is verbatim
# by construction rather than by a copy this file would have to keep correct.
move_eighth_into_part2() {
    local root="$1"

    write_live_record "$root" \
        docs/devlog/2026-08-part2.md \
        docs/devlog/2026-08-part1.md \
        docs/devlog/2026-07.md \
        -- \
        "2026-08-09 -- Ninth August entry"
    write_archive "$root/docs/devlog/2026-08-part2.md" \
        "2026-08-08 -- Eighth August entry" \
        "2026-08-05 -- Fifth August entry" \
        "2026-08-04 -- Fourth August entry"
    refresh_manifest "$root"
}

root="$(build_repository published_unchanged)"
publish "$root" "Publish the record"
expect_accepted_reporting "a published record the working tree still matches" \
    "$root" "8 entries published in main, 8 compared against their published text"

# The case the manifest cannot see. The entry and its manifest line go in one
# change, and three further commits land on top, so nothing in the tree or its
# index remembers the entry. History does.
root="$(build_repository dropped_after_publication)"
publish "$root" "Publish the record"
sed -i '/^## 2026-08-01 -- First August entry$/,+3d' \
    "$root/docs/devlog/2026-08-part1.md"
refresh_manifest "$root"
publish "$root" "Drop an entry and its manifest line"
for later in "2026-08-10 -- Tenth August entry" \
    "2026-08-11 -- Eleventh August entry" \
    "2026-08-12 -- Twelfth August entry"; do
    printf '\n---\n\n## %s\n\nBody text.\n' "$later" >>"$root/DEVLOG.md"
    refresh_manifest "$root"
    publish "$root" "Publish a later entry"
done
expect_rejected "an entry dropped three commits back, manifest and all" "$root" \
    "a published entry is present nowhere in the record: ## 2026-08-01 -- First August entry"

# A move is not a rewrite, and the gate has to agree or the first part split
# under it fails.
root="$(build_repository moved_verbatim)"
publish "$root" "Publish the record"
move_eighth_into_part2 "$root"
expect_accepted_reporting "a published entry moved into a part verbatim" \
    "$root" "8 entries published in main, 8 compared against their published text"

# The difference a move actually produces: the entry gains or loses the blank
# line and rule that separated it from what followed it. That is layout, and
# the comparison has to be blind to exactly that much and no more.
root="$(build_repository moved_with_boundary_whitespace)"
publish "$root" "Publish the record"
move_eighth_into_part2 "$root"
# A blank line after the moved entry's last line of text, and blank lines at
# the end of the file it moved into. Both are separator.
awk '
    /^## 2026-08-08 -- Eighth August entry$/ { pending = 1 }
    pending && /^Body text\.$/ { print; print ""; pending = 0; next }
    { print }
' "$root/docs/devlog/2026-08-part2.md" >"$root/part2.rewritten"
mv "$root/part2.rewritten" "$root/docs/devlog/2026-08-part2.md"
printf '\n\n' >>"$root/docs/devlog/2026-08-part2.md"
expect_accepted_reporting "a moved entry whose boundary whitespace changed" \
    "$root" "8 entries published in main, 8 compared against their published text"

# And no more than that: one word under an unchanged heading.
root="$(build_repository moved_and_reworded)"
publish "$root" "Publish the record"
move_eighth_into_part2 "$root"
sed -i '/^## 2026-08-08 -- Eighth August entry$/,+2s/^Body text\.$/Body text, revised./' \
    "$root/docs/devlog/2026-08-part2.md"
expect_rejected "a moved entry whose text was changed under its heading" "$root" \
    "a published entry has been rewritten under an unchanged heading: ## 2026-08-08 -- Eighth August entry"

# A new entry written where nobody reads first. Dated inside the part's own
# stretch, so every arrangement rule is satisfied and the manifest records it:
# only never having been published gives it away.
root="$(build_repository new_entry_written_into_a_part)"
publish "$root" "Publish the record"
write_archive "$root/docs/devlog/2026-08-part1.md" \
    "2026-08-03 -- Third August entry" \
    "2026-08-02 -- Second August entry" \
    "2026-08-01 -- First August entry"
refresh_manifest "$root"
expect_rejected "an unpublished entry written straight into a part" "$root" \
    "a new entry was written into docs/devlog/2026-08-part1.md: ## 2026-08-03 -- Third August entry"

# Copied rather than moved, with the record published. Both copies match what
# was published, so history is silent here by design and the duplicate rule is
# what has to fire.
root="$(build_repository duplicated_after_publication)"
publish "$root" "Publish the record"
write_archive "$root/docs/devlog/2026-08-part2.md" \
    "2026-08-08 -- Eighth August entry" \
    "2026-08-05 -- Fifth August entry" \
    "2026-08-04 -- Fourth August entry"
refresh_manifest "$root"
expect_rejected "an entry left in the live record and copied into a part" "$root" \
    "entry appears more than once: ## 2026-08-08 -- Eighth August entry"

# Which published form an entry is measured against. The record's own rule
# forbids editing a published entry, but history from before that rule holds
# entries in more than one form, and the branch's most recent form is the only
# coherent baseline: measured against its oldest, the gate would demand text
# the branch itself no longer carries and could never be made green.
root="$(build_repository corrected_before_the_rule)"
publish "$root" "Publish the record"
sed -i '/^## 2026-08-04 -- Fourth August entry$/,+2s/^Body text\.$/Body text, as corrected./' \
    "$root/docs/devlog/2026-08-part2.md"
publish "$root" "A correction the branch now carries"
expect_accepted_reporting "an entry published in more than one form" \
    "$root" "8 entries published in main, 8 compared against their published text"

# The reason the baseline is one branch and not every ref. A ref that is not an
# ancestor of the published branch holds record states that were never
# published - local checkpoints among them. A baseline built from all refs
# would demand this branch's entry be present and refuse the published record.
root="$(build_repository unrelated_ref)"
publish "$root" "Publish the record"
git -C "$root" checkout --quiet -b local-checkpoint
sed -i '/^## 2026-08-01 -- First August entry$/,+3d' \
    "$root/docs/devlog/2026-08-part1.md"
printf '\n---\n\n## %s\n\nBody text.\n' \
    "2026-08-20 -- An entry that was never published" >>"$root/DEVLOG.md"
refresh_manifest "$root"
publish "$root" "A state that never reached the published branch"
git -C "$root" checkout --quiet main
expect_accepted_reporting "a commit on a ref outside the published branch" \
    "$root" "8 entries published in main, 8 compared against their published text"

# The floor. Every comparison against history holds vacuously over a baseline
# that read nothing, and a resolution mistake - wrong ref, wrong paths - looks
# exactly like a record nobody has published yet.
root="$(build_repository nothing_published_yet)"
printf 'Placeholder.\n' >"$root/README.md"
publish "$root" "Commit nothing but a placeholder" -- README.md
expect_rejected "a baseline that read no published entry at all" "$root" \
    "no entry has ever been published in main, so history bounded nothing"

# --- History simplification must not hide a published entry -----------------
# The shape that defeats a plain `git rev-list <ref> -- <paths>`. A branch that
# touches nothing in the record is merged back with the record files resolved
# to its side. The merge is then TREESAME to that parent for these paths, so
# default simplification follows it alone and discards the other parent - and
# every entry published on it. The dropped entry is in no file and in no
# manifest, so nothing but history can demand it, and history is exactly what
# simplification threw away.
root="$(build_repository merge_simplification)"
publish "$root" "Publish the record"
before_the_entry="$(git -C "$root" rev-parse HEAD)"
printf '\n---\n\n## %s\n\nBody text.\n' \
    "2026-08-10 -- Tenth August entry" >>"$root/DEVLOG.md"
refresh_manifest "$root"
publish "$root" "Publish a tenth entry"
git -C "$root" checkout --quiet -b touches_nothing_in_the_record "$before_the_entry"
printf 'An unrelated file.\n' >"$root/NOTES.md"
publish "$root" "Change a file outside the record"
git -C "$root" checkout --quiet main
git -C "$root" merge --quiet --no-ff --no-commit touches_nothing_in_the_record \
    >/dev/null 2>&1 || true
# The ordinary take-theirs resolution. Nothing here looks like an attack, and
# the resulting tree passes every arrangement rule the guard has.
git -C "$root" checkout touches_nothing_in_the_record -- DEVLOG.md docs/devlog
publish "$root" "Merge the branch, resolving the record to its side"
expect_rejected "an entry hidden behind a merge by history simplification" "$root" \
    "a published entry is present nowhere in the record: ## 2026-08-10 -- Tenth August entry"

# --- The baseline is the published branch, not the local one ----------------
# This gate runs after the commit exists. With local `main` as the baseline, a
# rewrite committed there is compared against itself and approved - the whole
# rewriting class self-approves on the integration branch. `origin/main` is the
# published branch and the commit under test is not in it.
root="$(build_repository baseline_prefers_the_published_branch)"
publish "$root" "Publish the record"
git -C "$root" update-ref refs/remotes/origin/main "$(git -C "$root" rev-parse HEAD)"
sed -i '/^## 2026-08-04 -- Fourth August entry$/,+2s/^Body text\.$/Body text, quietly rewritten./' \
    "$root/docs/devlog/2026-08-part2.md"
publish "$root" "Rewrite a published entry on the local branch"
expect_rejected "a published entry rewritten and then committed on the local branch" \
    "$root" \
    "a published entry has been rewritten under an unchanged heading: ## 2026-08-04 -- Fourth August entry"

# --- A repository whose branch and remote were renamed ----------------------
# A full clone carries the whole of history under any set of names. Resolving
# only the two expected names reaches the unchecked path with that history
# sitting right there, and the notice goes to standard output where a passing
# run hides it. HEAD resolves in any repository that has commits.
root="$(build_repository baseline_falls_back_to_head)"
publish "$root" "Publish the record"
git -C "$root" branch -m main trunk
expect_accepted_reporting "a renamed branch is still judged against its history" \
    "$root" "8 entries published in HEAD, 8 compared against their published text"

root="$(build_repository dropped_under_a_renamed_branch)"
publish "$root" "Publish the record"
git -C "$root" branch -m main trunk
sed -i '/^## 2026-08-01 -- First August entry$/,+3d' \
    "$root/docs/devlog/2026-08-part1.md"
refresh_manifest "$root"
expect_rejected "an entry dropped where neither main nor origin/main resolves" "$root" \
    "a published entry is present nowhere in the record: ## 2026-08-01 -- First August entry"

# --- The manifest is a list of headings, not a record file ------------------
# Read as a record it presents every published entry as one with no body, so
# every entry that has a body reads as rewritten. Here a commit's record files
# no longer carry an entry that its manifest still lists, so the manifest is
# the first place that heading is seen and its empty body would be taken for
# what was published.
#
# STATED EXACTLY: this does not discriminate the exclusion on its own, and
# nothing can while the manifest is named `.txt`. The blob filter admits only
# `.md`, so it rejects the manifest before the exclusion is ever consulted -
# removing the exclusion alone changes no behaviour and this scenario keeps
# passing. What the scenario does pin is the pair: remove the exclusion and
# widen that filter, which is what renaming the manifest to a `.md` file would
# amount to, and this scenario fails by name for the eighth entry.
root="$(build_repository manifest_is_not_a_record)"
publish "$root" "Publish the record"
write_live_record "$root" \
    docs/devlog/2026-08-part2.md \
    docs/devlog/2026-08-part1.md \
    docs/devlog/2026-07.md \
    -- \
    "2026-08-09 -- Ninth August entry"
# Deliberately not refreshed: the manifest keeps the heading the record files
# just lost, which is the state this scenario needs in history.
publish "$root" "A commit whose manifest outlives an entry in its record files"
write_live_record "$root" \
    docs/devlog/2026-08-part2.md \
    docs/devlog/2026-08-part1.md \
    docs/devlog/2026-07.md \
    -- \
    "2026-08-09 -- Ninth August entry" \
    "2026-08-08 -- Eighth August entry"
refresh_manifest "$root"
expect_accepted_reporting "the manifest is not read as a published record file" \
    "$root" "8 entries published in main, 8 compared against their published text"

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
    [[ "$metadata_free_output" == *"published history is UNCHECKED"* ]] &&
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

#!/usr/bin/env bash
# Exercises tools/check_devlog_archive.sh in both directions. A guard that is
# never shown rejecting a broken layout is indistinguishable from one that
# reports success unconditionally, so every scenario below asserts the specific
# reason the guard gives, not merely its exit status.
set -euo pipefail

readonly script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly guard="$script_directory/check_devlog_archive.sh"

if [[ ! -x "$guard" && ! -f "$guard" ]]; then
    echo "devlog_archive_guard_self_test: guard script is missing" >&2
    exit 1
fi

if ! command -v git >/dev/null 2>&1; then
    echo "devlog_archive_guard_self_test: skipped because git is unavailable"
    exit 77
fi

readonly sandbox_root="$(mktemp -d)"
trap 'rm -rf -- "$sandbox_root"' EXIT

failures=0

report() {
    local outcome="$1" scenario="$2"
    printf '%-5s %s\n' "$outcome" "$scenario"
    [[ "$outcome" == PASS ]] || failures=$((failures + 1))
}

# Builds a throwaway repository with a consistent live record and one archived
# month, then applies the caller's mutation before running the guard.
build_repository() {
    local name="$1"
    local root="$sandbox_root/$name"

    mkdir -p "$root/docs/devlog"
    cat >"$root/DEVLOG.md" <<'RECORD'
# Devlog

Live record. Archived months, most recent first:

- [2026-07](docs/devlog/2026-07.md)

---

## 2026-08-02 -- Second August entry

Body text.

---

## 2026-08-01 -- First August entry

Body text.
RECORD

    cat >"$root/docs/devlog/2026-07.md" <<'ARCHIVE'
# Devlog archive 2026-07

Archived entries.

---

## 2026-07-31 -- Last July entry

Body text.

---

## 2026-07-01 -- First July entry

Body text.
ARCHIVE

    # The scenarios never commit, so no identity configuration is required.
    git -C "$root" init --quiet
    printf '%s\n' "$root"
}

track_everything() {
    git -C "$1" add --all
}

run_guard() {
    local root="$1"
    ( cd "$root" && bash "$guard" ) 2>&1
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
    # script can print, including the archive count it had to compute.
    if [[ "$output" != *"1 archived month(s) are consistent"* ]]; then
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
expect_accepted "consistent live record and archive" "$root"

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
    "links docs/devlog/2026-06.md, which is not a tracked file"

# --- An entry filed under the wrong month ----------------------------------
root="$(build_repository wrong_month)"
sed -i 's/^## 2026-07-01 -- First July entry$/## 2026-06-30 -- Misfiled entry/' \
    "$root/docs/devlog/2026-07.md"
track_everything "$root"
expect_rejected "archive holds an entry from another month" "$root" \
    "archive docs/devlog/2026-07.md contains a 2026-06 entry"

# --- The live record keeps an already-archived month -----------------------
root="$(build_repository stale_live_entry)"
cat >>"$root/DEVLOG.md" <<'STALE'

---

## 2026-07-15 -- Entry left behind in the live record

Body text.
STALE
track_everything "$root"
expect_rejected "live record holds an archived month" "$root" \
    "still holds a 2026-07 entry"

# --- The index in the wrong order ------------------------------------------
root="$(build_repository unordered_index)"
mkdir -p "$root/docs/devlog"
cat >"$root/docs/devlog/2026-06.md" <<'JUNE'
# Devlog archive 2026-06

---

## 2026-06-15 -- A June entry

Body text.
JUNE
sed -i 's#- \[2026-07\](docs/devlog/2026-07.md)#- [2026-06](docs/devlog/2026-06.md)\n- [2026-07](docs/devlog/2026-07.md)#' \
    "$root/DEVLOG.md"
track_everything "$root"
expect_rejected "archive index is not reverse-chronological" "$root" \
    "not in reverse-chronological order: 2026-06 then 2026-07"

# --- An entry copied instead of moved --------------------------------------
root="$(build_repository duplicated_entry)"
cat >>"$root/docs/devlog/2026-07.md" <<'COPY'

---

## 2026-08-01 -- First August entry

Body text.
COPY
track_everything "$root"
expect_rejected "the same entry in two files" "$root" \
    "entry appears more than once: ## 2026-08-01 -- First August entry"

# --- An archive that is not named for a month ------------------------------
root="$(build_repository misnamed_archive)"
mv "$root/docs/devlog/2026-07.md" "$root/docs/devlog/older-entries.md"
sed -i 's#docs/devlog/2026-07.md#docs/devlog/older-entries.md#' "$root/DEVLOG.md"
track_everything "$root"
expect_rejected "archive file not named YYYY-MM.md" "$root" \
    "is not named YYYY-MM.md"

# --- An archive with no entries at all -------------------------------------
root="$(build_repository empty_archive)"
cat >"$root/docs/devlog/2026-07.md" <<'EMPTY'
# Devlog archive 2026-07

Nothing was moved here.
EMPTY
track_everything "$root"
expect_rejected "archive contains no dated entry" "$root" \
    "contains no dated entry"

# --- Outside a repository the guard must skip, not pass --------------------
outside="$sandbox_root/outside"
mkdir -p "$outside"
skip_status=0
skip_output="$( cd "$outside" && HOME="$outside" GIT_CEILING_DIRECTORIES="$sandbox_root" \
    bash "$guard" 2>&1 )" || skip_status=$?
if ((skip_status == 77)) && [[ "$skip_output" == *"Git metadata is unavailable"* ]]; then
    report PASS "skips with status 77 when Git metadata is unavailable"
else
    report FAIL "expected a status 77 skip outside a repository, got $skip_status"
    printf '  %s\n' "$skip_output" >&2
fi

if ((failures == 0)); then
    echo "devlog_archive_guard_self_test: all scenarios passed"
    exit 0
fi

echo "devlog_archive_guard_self_test: $failures scenario(s) failed" >&2
exit 1

#!/usr/bin/env bash

# Static-analysis gate.
#
# The check policy in `.clang-tidy` promotes the categories that indicate real
# defects to errors; the remainder are advisory. Advisory diagnostics are not
# silenced, because several of them are legitimate work this codebase has not
# done yet, and silencing a category would also hide the next occurrence of it.
# They are instead held against a recorded baseline: the gate fails when a file
# gains a diagnostic it did not have, and equally when it sheds one without the
# baseline being updated, so the recorded set can only move downward and can
# never quietly drift.
#
# Each recorded entry carries a digest of the diagnostic text alongside the
# count. A count alone cannot tell a fixed diagnostic from a substituted one:
# parenthesising one expression while introducing a precedence defect in
# another leaves the same file with the same number of occurrences of the same
# check, and the gate's output was byte-identical to a clean tree. The digest
# is taken over the message text only, which carries no line or column, so the
# deliberate insensitivity to editing above a recorded diagnostic is kept: an
# entry restates the baseline when what the analyser said about the file
# changes, not when the file moved.
#
# Advisory output is summarised rather than reprinted. Emitting every advisory
# line on every run buries a new diagnostic in several hundred unchanged ones,
# which defeats the reason for running the analysis in a gate at all.
#
# Every translation unit is analysed before the gate reports, so one run lists
# every fatal diagnostic instead of stopping at the first file that has one.
#
# Usage:
#   check_clang_tidy.sh <build-directory>
#   check_clang_tidy.sh <build-directory> --update-baseline

set -euo pipefail

if [[ "$#" -lt 1 || "$#" -gt 2 ]]; then
    printf 'usage: %s <build-directory> [--update-baseline]\n' "$0" >&2
    exit 2
fi

update_baseline=0
if [[ "$#" -eq 2 ]]; then
    if [[ "$2" != "--update-baseline" ]]; then
        printf 'usage: %s <build-directory> [--update-baseline]\n' "$0" >&2
        exit 2
    fi
    update_baseline=1
fi

# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"
guard_corpus_init static_analysis
readonly repository_root="$guard_corpus_root"

build_directory="$1"
if [[ "$build_directory" != /* ]]; then
    build_directory="$repository_root/$build_directory"
fi
if [[ ! -f "$build_directory/compile_commands.json" ]]; then
    printf 'static_analysis: compilation database missing at %s\n' \
        "$build_directory/compile_commands.json" >&2
    exit 1
fi

readonly baseline_file="$repository_root/tools/clang_tidy_baseline.txt"
readonly policy_file="$repository_root/.clang-tidy"

# The set of checks promoted to errors is read from the policy rather than
# repeated here. It was repeated here, and the two drifted the moment a check
# was promoted: the gate still failed on the new fatal diagnostic, but it
# printed the failing translation unit with none of its diagnostics under it,
# because the hard-coded pattern did not know the check existed. A gate that
# names the file and withholds the reason sends its reader to read the whole
# file.
#
# Each entry is a clang-tidy check glob; only the trailing star has meaning, so
# it becomes the character class that matches the rest of a check name.
if [[ ! -f "$policy_file" ]]; then
    printf 'static_analysis: the check policy at %s is missing\n' "$policy_file" >&2
    exit 1
fi
fatal_check_pattern="$(
    sed -n "s/^WarningsAsErrors:[[:space:]]*['\"]\(.*\)['\"][[:space:]]*$/\1/p" \
        "$policy_file" |
        tr ',' '\n' |
        sed 's/^[[:space:]]*//; s/[[:space:]]*$//' |
        grep -v '^$' |
        sed 's/[*]$/[a-z0-9-]*/' |
        paste -sd '|' - || true
)"
readonly fatal_check_pattern

# A gate that could not read its own fatal set must not run. With an empty
# pattern the alternation below matches every diagnostic, so a single advisory
# line would be reprinted as though it were fatal, and the failure above would
# be explained by the wrong list entirely.
if [[ -z "$fatal_check_pattern" ]]; then
    printf 'static_analysis: no check is promoted to an error in %s\n' "$policy_file" >&2
    printf '  The gate reads its fatal set from that line; without it there is nothing to enforce.\n' >&2
    exit 1
fi

# The digest tool is required rather than optional. Without it every entry
# would carry an empty digest, every empty digest would compare equal to every
# other, and the comparison would go on reporting success while checking
# nothing - the same failure as a counter that stopped counting.
if ! command -v sha256sum >/dev/null 2>&1; then
    printf 'static_analysis: sha256sum is required to digest diagnostic text\n' >&2
    exit 1
fi

tidy_binary="${ODYSEA_CLANG_TIDY:-}"
if [[ -z "$tidy_binary" ]]; then
    tidy_binary="$(command -v clang-tidy || true)"
fi
if [[ -z "$tidy_binary" ]]; then
    if guard_corpus_is_git; then
        common_git_directory="$(git rev-parse --path-format=absolute --git-common-dir)"
        shared_repository_root="$(dirname "$common_git_directory")"
    else
        shared_repository_root="$repository_root"
    fi
    local_tidy="$shared_repository_root/.archon/tools/clang-tidy-22/bin/clang-tidy"
    if [[ -x "$local_tidy" ]]; then
        tidy_binary="$local_tidy"
    fi
fi
if [[ -z "$tidy_binary" || ! -x "$tidy_binary" ]]; then
    printf 'static_analysis: clang-tidy 22 is required\n' >&2
    exit 1
fi

tidy_version="$("$tidy_binary" --version)"
case "$tidy_version" in
    *"LLVM version 22."*) ;;
    *)
        printf 'static_analysis: clang-tidy 22 is required; found %s\n' \
            "$tidy_version" >&2
        exit 1
        ;;
esac

escaped_repository_root="$(
    printf '%s' "$repository_root" |
        sed 's/[][\\.^$*+?(){}|]/\\&/g'
)"
header_filter="^${escaped_repository_root}/(app|core|tests)/"

workspace="$(mktemp -d)"
trap 'rm -rf -- "$workspace"' EXIT

readonly raw_output="$workspace/diagnostics.txt"
: >"$raw_output"

checked=0
declare -a failed_units=()

while IFS= read -r -d '' source_file; do
    unit_output="$workspace/unit.txt"
    unit_status=0
    "$tidy_binary" -p "$build_directory" --quiet \
        --header-filter="$header_filter" "$source_file" \
        >"$unit_output" 2>&1 || unit_status=$?

    cat "$unit_output" >>"$raw_output"
    if ((unit_status != 0)); then
        failed_units+=("$source_file")
    fi
    checked=$((checked + 1))
done < <(guard_corpus_list '*.cpp')

# A floor under the count. Analysing nothing produces no diagnostics, which
# matches an empty baseline exactly and reports as a clean gate. The corpus is
# enumerated from a repository root and the baseline is a file that can be
# emptied, so both halves of that coincidence are reachable without anyone
# intending it.
if ((checked == 0)); then
    printf 'static_analysis: no translation unit was found, so nothing was analysed\n' >&2
    exit 1
fi

# A header is re-analysed by every translation unit that includes it, so the
# same diagnostic is reported many times. Identity is the location plus the
# check that produced it, and the duplicates are collapsed before counting: a
# count that rose and fell with the number of unrelated sources including a
# header would make the baseline shift for reasons unconnected to the analysis.
#
# The count is per file and check rather than per line so that editing a file
# above a recorded diagnostic does not restate the baseline.
#
# The message text is kept, because the count on its own cannot distinguish a
# diagnostic that was fixed from one that was replaced. It is carried into a
# per-entry digest below rather than into the entry key, so that two
# occurrences of one check in one file stay a single recorded line.
#
# It is deliberately not part of the identity that collapses the duplicates
# either. One location can be described in more than one way: analysed from a
# translation unit that sees only the declaration of a destructor, a class is
# reported as defining "a destructor", and from one that sees the definition as
# defining "a non-default destructor" - the same diagnostic, at the same line
# and column, worded by what that unit could see. Counting those separately
# made a header's recorded count depend on which unrelated sources include it,
# which is the instability collapsing duplicates exists to prevent. A location
# is therefore one occurrence, and every wording observed at it is carried into
# the digest together.
#
# The repository root is stripped everywhere on the line, not only at its
# start: a message that quoted an absolute path would otherwise make the digest
# depend on where the checkout happens to live. Tabs are folded to spaces first
# so that a message can never split the fields it is carried in.
#
# Every sort that the digest depends on runs in the C collation order. The
# digest is committed to the baseline and has to reproduce on another machine,
# and the default collation orders punctuation differently between locales, so
# an unpinned sort would make the recorded digest a property of the checkout's
# environment rather than of the analysis.
readonly diagnostics_file="$workspace/diagnostics-normalised.txt"
tr '\t' ' ' <"$raw_output" |
    sed "s|${repository_root}/||g" |
    sed -nE 's/^([^ :]+):([0-9]+):([0-9]+): (warning|error): (.*) \[([a-z0-9-]+)\]$/\1\t\2\t\3\t\6\t\5/p' |
    LC_ALL=C sort -u >"$diagnostics_file"

# Reduces the diagnostics to one record per location, carrying every wording
# observed there. The unit separator joins them because it cannot occur in a
# diagnostic message, and it never reaches the output: the joined text is
# digest input only.
readonly locations_file="$workspace/locations.txt"
LC_ALL=C sort -t$'\t' -k1,1 -k4,4 -k2,2 -k3,3 -k5,5 "$diagnostics_file" |
    awk -F'\t' -v separator=$'\x1f' '
        {
            key = $1 FS $4 FS $2 FS $3
            if (key != previous) {
                if (started) {
                    printf "%s\t%s\t%s\n", path, check, wordings
                }
                started = 1
                previous = key
                path = $1
                check = $4
                wordings = $5
            } else {
                wordings = wordings separator $5
            }
        }
        END {
            if (started) {
                printf "%s\t%s\t%s\n", path, check, wordings
            }
        }
    ' >"$locations_file"

# Groups the locations by file and check, counting them and digesting their
# text. Locations are sorted by their wording within a group by the sort below,
# so the digest describes what was said about that file and check and not the
# order the translation units happened to be analysed in.
readonly current_file="$workspace/current.txt"
{
    group_key=""
    group_count=0
    group_text=""

    flush_group() {
        if [[ -z "$group_key" ]]; then
            return 0
        fi
        local path="${group_key%%$'\t'*}"
        local check="${group_key#*$'\t'}"
        local digest
        digest="$(printf '%s' "$group_text" | sha256sum | cut -c1-12)"
        printf '%d\t%s\t%s\t%s\n' "$group_count" "$path" "$check" "$digest"
    }

    while IFS=$'\t' read -r path check wordings; do
        entry_key="$path"$'\t'"$check"
        if [[ "$entry_key" != "$group_key" ]]; then
            flush_group
            group_key="$entry_key"
            group_count=0
            group_text=""
        fi
        group_count=$((group_count + 1))
        group_text+="$wordings"$'\n'
    done < <(LC_ALL=C sort -t$'\t' -k1,1 -k2,2 -k3,3 "$locations_file")

    flush_group
} | LC_ALL=C sort -k2,2 -k3,3 >"$current_file"

# A floor under the digest itself. An entry whose digest is empty or malformed
# means the digesting step produced nothing, and an empty digest compares equal
# to every other empty digest, so the comparison below would pass every swap it
# exists to catch.
while IFS=$'\t' read -r _count path check digest; do
    if [[ ! "$digest" =~ ^[0-9a-f]{12}$ ]]; then
        printf 'static_analysis: %s: %s was measured without a usable diagnostic-text digest\n' \
            "$path" "$check" >&2
        printf '  The digest is what distinguishes a fixed diagnostic from a substituted one.\n' >&2
        exit 1
    fi
done <"$current_file"

if ((update_baseline == 1)); then
    {
        printf '# Recorded advisory clang-tidy diagnostics, one line per file and\n'
        printf '# check, written as: count<TAB>path<TAB>check<TAB>digest.\n'
        printf '#\n'
        printf '# The digest covers the diagnostic text of that entry, so that fixing\n'
        printf '# one occurrence while introducing another is not recorded as no change.\n'
        printf '# It carries no line or column: moving a diagnostic down a file leaves\n'
        printf '# the entry alone.\n'
        printf '#\n'
        printf '# The gate fails when this set changes in either direction. Regenerate\n'
        printf '# with: bash tools/check_clang_tidy.sh <build-directory> --update-baseline\n'
        cat "$current_file"
    } >"$baseline_file"
    printf 'static_analysis: baseline rewritten with %d entries\n' \
        "$(wc -l <"$current_file")"
    exit 0
fi

status=0

if ((${#failed_units[@]} > 0)); then
    printf 'static_analysis: %d translation units reported fatal diagnostics:\n' \
        "${#failed_units[@]}" >&2
    for unit in "${failed_units[@]}"; do
        printf '  %s\n' "$unit" >&2
    done
    grep -E "(error|warning): .*\[($fatal_check_pattern)" \
        "$raw_output" | sed "s|^${repository_root}/||" | sort -u >&2 || true
    status=1
fi

if [[ ! -f "$baseline_file" ]]; then
    printf 'static_analysis: the baseline at %s is missing\n' "$baseline_file" >&2
    exit 1
fi

readonly recorded_file="$workspace/recorded.txt"
grep -v '^#' "$baseline_file" | grep -v '^[[:space:]]*$' |
    LC_ALL=C sort -k2,2 -k3,3 >"$recorded_file" || true

# A baseline written before entries carried a digest has three fields, and a
# missing digest would compare equal to nothing and be reported as a change on
# every entry at once - or, worse, be treated as "no digest recorded, so no
# digest to disagree with". Neither is a comparison. The baseline is refused
# until it has been regenerated in the current form.
while IFS=$'\t' read -r _count path check digest; do
    if [[ ! "$digest" =~ ^[0-9a-f]{12}$ ]]; then
        printf 'static_analysis: the baseline entry for %s: %s carries no diagnostic-text digest\n' \
            "$path" "$check" >&2
        printf '  Regenerate it with: bash tools/check_clang_tidy.sh <build-directory> --update-baseline\n' >&2
        exit 1
    fi
done <"$recorded_file"

# Compares the recorded and current sets keyed on path and check, so a count
# that moves in either direction is reported with both values.
#
# The two sets are tagged and read as one stream rather than passed as two
# files told apart by record number. The record-number idiom reads the second
# file as the first whenever the first is empty, and an empty recorded set is
# not a hypothetical here: it is the state this ratchet exists to reach. In
# that state the current set was loaded as the recorded one, so a genuinely new
# diagnostic was reported as one that no longer occurs. The gate still failed,
# but its message pointed at the opposite conclusion, on a gate whose only job
# is telling a reader which way the set moved.
readonly drift_file="$workspace/drift.txt"
awk -F'\t' '
    $1 == "recorded" {
        recorded[$3 "\t" $4] = $2
        digest[$3 "\t" $4] = $5
        next
    }
    $1 == "current" {
        key = $3 "\t" $4
        if (!(key in recorded)) {
            printf "new\t%s\t%s\t0\t%s\t\t%s\n", $3, $4, $2, $5
        } else {
            if (recorded[key] + 0 != $2 + 0) {
                printf "changed\t%s\t%s\t%s\t%s\t%s\t%s\n", \
                    $3, $4, recorded[key], $2, digest[key], $5
            } else if (digest[key] != $5) {
                printf "substituted\t%s\t%s\t%s\t%s\t%s\t%s\n", \
                    $3, $4, recorded[key], $2, digest[key], $5
            }
            delete recorded[key]
        }
    }
    END {
        for (key in recorded) {
            split(key, parts, "\t")
            printf "cleared\t%s\t%s\t%s\t0\t%s\t\n", \
                parts[1], parts[2], recorded[key], digest[key]
        }
    }
' < <(
    sed 's/^/recorded\t/' "$recorded_file"
    sed 's/^/current\t/' "$current_file"
) | LC_ALL=C sort >"$drift_file"

if [[ -s "$drift_file" ]]; then
    printf 'static_analysis: the advisory diagnostic set moved away from the baseline\n' >&2
    while IFS=$'\t' read -r kind path check was now was_digest now_digest; do
        case "$kind" in
            new)
                printf '  %s: %s is new (%s occurrence(s))\n' \
                    "$path" "$check" "$now" >&2
                ;;
            changed)
                printf '  %s: %s went from %s to %s occurrence(s)\n' \
                    "$path" "$check" "$was" "$now" >&2
                ;;
            substituted)
                printf '  %s: %s still occurs %s time(s), but the analyser now says something else about it (recorded %s, now %s)\n' \
                    "$path" "$check" "$now" "$was_digest" "$now_digest" >&2
                printf '    An occurrence was fixed and another introduced, or an existing one changed form.\n' >&2
                ;;
            cleared)
                printf '  %s: %s no longer occurs (%s recorded)\n' \
                    "$path" "$check" "$was" >&2
                ;;
        esac
    done <"$drift_file"
    printf 'static_analysis: fix a new diagnostic, or record an improvement with --update-baseline\n' >&2
    status=1
fi

if ((status != 0)); then
    exit "$status"
fi

readonly recorded_total="$(awk -F'\t' '{ total += $1 } END { printf "%d", total }' \
    "$current_file")"

# The success line states the size of both sets, not only the diagnostic total.
# A held baseline and an exhausted one both pass, and only the recorded entry
# count tells them apart: zero recorded entries is the end state this
# ratchet is aimed at, but it is also what an emptied baseline over a tree with
# nothing to say looks like. It is not made a failure - refusing it would
# forbid the ratchet from ever reaching its own target - so it is made visible
# instead.
readonly recorded_entries="$(grep -c . <"$recorded_file" || true)"
printf 'static_analysis: %d translation units passed, %d advisory diagnostics across %d recorded entries held at the baseline\n' \
    "$checked" "$recorded_total" "$recorded_entries"

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
readonly current_file="$workspace/current.txt"
sed "s|^${repository_root}/||" "$raw_output" |
    sed -nE 's/^([^ :]+):([0-9]+):([0-9]+): (warning|error): .*\[([a-z0-9-]+)\]$/\1\t\2\t\3\t\5/p' |
    sort -u |
    awk -F'\t' '{ print $1 "\t" $4 }' |
    sort |
    uniq -c |
    sed -E 's/^ *([0-9]+) /\1\t/' |
    sort -k2,2 -k3,3 >"$current_file"

if ((update_baseline == 1)); then
    {
        printf '# Recorded advisory clang-tidy diagnostics, one line per file and\n'
        printf '# check, written as: count<TAB>path<TAB>check.\n'
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
    grep -E '(error|warning): .*\[(bugprone-|cppcoreguidelines-owning-memory|cppcoreguidelines-no-malloc|clang-analyzer-)' \
        "$raw_output" | sed "s|^${repository_root}/||" | sort -u >&2 || true
    status=1
fi

if [[ ! -f "$baseline_file" ]]; then
    printf 'static_analysis: the baseline at %s is missing\n' "$baseline_file" >&2
    exit 1
fi

readonly recorded_file="$workspace/recorded.txt"
grep -v '^#' "$baseline_file" | grep -v '^[[:space:]]*$' |
    sort -k2,2 -k3,3 >"$recorded_file" || true

# Compares the recorded and current sets keyed on path and check, so a count
# that moves in either direction is reported with both values.
readonly drift_file="$workspace/drift.txt"
awk -F'\t' '
    FNR == NR {
        recorded[$2 "\t" $3] = $1
        next
    }
    {
        key = $2 "\t" $3
        if (!(key in recorded)) {
            printf "new\t%s\t%s\t0\t%s\n", $2, $3, $1
        } else {
            if (recorded[key] + 0 != $1 + 0) {
                printf "changed\t%s\t%s\t%s\t%s\n", $2, $3, recorded[key], $1
            }
            delete recorded[key]
        }
    }
    END {
        for (key in recorded) {
            split(key, parts, "\t")
            printf "cleared\t%s\t%s\t%s\t0\n", parts[1], parts[2], recorded[key]
        }
    }
' "$recorded_file" "$current_file" | sort >"$drift_file"

if [[ -s "$drift_file" ]]; then
    printf 'static_analysis: the advisory diagnostic set moved away from the baseline\n' >&2
    while IFS=$'\t' read -r kind path check was now; do
        case "$kind" in
            new)
                printf '  %s: %s is new (%s occurrence(s))\n' \
                    "$path" "$check" "$now" >&2
                ;;
            changed)
                printf '  %s: %s went from %s to %s occurrence(s)\n' \
                    "$path" "$check" "$was" "$now" >&2
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
printf 'static_analysis: %d translation units passed, %d advisory diagnostics held at the baseline\n' \
    "$checked" "$recorded_total"

#!/usr/bin/env bash
set -euo pipefail

readonly max_lines=2000

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "file_length_guard: skipped because Git metadata is unavailable"
    exit 77
fi

readonly repository_root="$(git rev-parse --show-toplevel)"
cd "$repository_root"

temporary_directory="$(mktemp -d)"
trap 'rm -rf -- "$temporary_directory"' EXIT
readonly index_copy="$temporary_directory/index-copy"

status=0
tracked_paths=0
working_tree_measurements=0
indexed_measurements=0

# Measures one file and reports whether it was measured at all. A binary asset
# is not a source or text file, so it is counted as unmeasured rather than as a
# silent pass: the difference is what the two floors below are able to see.
check_file() {
    local file="$1"
    local label="$2"
    local path="$3"

    # An empty file has zero lines and therefore cannot violate the ceiling,
    # but it is still text and still measured.
    if ! LC_ALL=C grep -Iq '' "$file"; then
        return 1
    fi

    local line_count
    line_count="$(awk 'END { print NR }' "$file")"
    if ((line_count > max_lines)); then
        printf 'file_length_guard: %s %q has %d lines; maximum is %d\n' \
            "$label" "$path" "$line_count" "$max_lines" >&2
        status=1
    fi
    return 0
}

while IFS= read -r -d '' path; do
    tracked_paths=$((tracked_paths + 1))

    if [[ -f "$path" && ! -L "$path" ]]; then
        if check_file "$path" "working-tree file" "$path"; then
            working_tree_measurements=$((working_tree_measurements + 1))
        fi
    fi

    if git show ":$path" >"$index_copy" 2>/dev/null; then
        if check_file "$index_copy" "indexed file" "$path"; then
            indexed_measurements=$((indexed_measurements + 1))
        fi
    fi
done < <(git ls-files -z)

# Two floors. A ceiling alone is satisfied by a run that measured nothing: over
# an empty corpus every file is under the limit, and so is every file in a
# corpus that holds only binary assets. Both states are reachable by ordinary
# mistakes - a corpus enumerated from the wrong root, a checkout with nothing
# staged - and both would otherwise print the same success line as a full run.
if ((tracked_paths == 0)); then
    printf 'file_length_guard: the tracked corpus is empty, so no file was measured against the %d-line ceiling\n' \
        "$max_lines" >&2
    exit 1
fi
if ((working_tree_measurements + indexed_measurements == 0)); then
    printf 'file_length_guard: %d tracked paths held no text file, so no file was measured against the %d-line ceiling\n' \
        "$tracked_paths" "$max_lines" >&2
    exit 1
fi

if ((status != 0)); then
    exit "$status"
fi

# The size of what was measured is part of the result. A success line that
# reads the same over a full corpus and over none cannot be checked by reading
# it.
printf 'file_length_guard: %d working-tree and %d indexed text files across %d tracked paths passed the %d-line ceiling\n' \
    "$working_tree_measurements" "$indexed_measurements" "$tracked_paths" \
    "$max_lines"

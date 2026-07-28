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

check_file() {
    local file="$1"
    local label="$2"
    local path="$3"

    # Binary assets are not source or text files. An empty file has zero lines
    # and therefore cannot violate the ceiling.
    if ! LC_ALL=C grep -Iq '' "$file"; then
        return
    fi

    local line_count
    line_count="$(awk 'END { print NR }' "$file")"
    if ((line_count > max_lines)); then
        printf 'file_length_guard: %s %q has %d lines; maximum is %d\n' \
            "$label" "$path" "$line_count" "$max_lines" >&2
        status=1
    fi
}

while IFS= read -r -d '' path; do
    if [[ -f "$path" && ! -L "$path" ]]; then
        check_file "$path" "working-tree file" "$path"
    fi

    if git show ":$path" >"$index_copy" 2>/dev/null; then
        check_file "$index_copy" "indexed file" "$path"
    fi
done < <(git ls-files -z)

if ((status != 0)); then
    exit "$status"
fi

echo "file_length_guard: tracked source and text files passed"

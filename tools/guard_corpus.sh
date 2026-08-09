# Corpus resolution for the repository guards. Sourced, never executed.
#
# Every guard in this directory derives its corpus from the same place, and for
# a long time every one of them derived it from Git. That made seventeen gates
# depend on a single condition: in a source tree without `.git` - which is what
# a release tarball is, and what a package build compiles - the whole set
# declined to run and the test summary still read as a complete pass. A gate
# that cannot fail in the environment where the software is actually built is
# not a gate there.
#
# The corpus is therefore resolved two ways behind one interface. With Git
# present it is the tracked set, which is the stricter reading: it is what a
# commit would publish, and it excludes ignored and untracked files. Without
# Git it is the source tree itself, minus version-control metadata, the ignored
# workflow directory, and every CMake build tree found beneath the root. A
# build tree is identified by the CMakeCache.txt it contains rather than by its
# name, so a build directory called anything at all is still excluded.
#
# Matching is done in one place for both. Only enumeration differs; the
# pathspec semantics a caller sees are identical, which is what makes the two
# modes comparable and is checked directly by the accompanying self-test.
#
# Two limits, stated rather than hidden. Without Git the corpus cannot honour
# `.gitignore`, so an ignored file sitting in the source tree is read as part
# of it; and it cannot read the index or the commit history, so gates whose
# subject is staged content or attribution say so and check what remains. A
# tarball has neither of those things to check.

# Resolved by guard_corpus_init.
guard_corpus_mode=""
guard_corpus_root=""

_guard_corpus_loaded=0
declare -a _guard_corpus_paths=()

# Establishes the corpus root and mode, then moves to the root. Every guard
# calls this before anything else and reports its mode, so a run in a tree
# without Git says which corpus it read rather than leaving it to be inferred.
guard_corpus_init() {
    local guard_name="$1"
    local library_directory

    library_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"

    if guard_corpus_root="$(git rev-parse --show-toplevel 2>/dev/null)"; then
        guard_corpus_mode="git"
    else
        guard_corpus_mode="filesystem"
        guard_corpus_root="$(cd "$library_directory/.." && pwd -P)"

        # Without Git the root is inferred from where this file sits, so the
        # inference is checked rather than assumed. A guard reading the wrong
        # tree is the failure mode this whole file exists to prevent, and it
        # would otherwise present as an unexplained empty corpus.
        if [[ ! -f "$guard_corpus_root/CMakeLists.txt" ||
            ! -d "$guard_corpus_root/tools" ]]; then
            printf '%s: no Git metadata, and %s is not a source root\n' \
                "$guard_name" "$guard_corpus_root" >&2
            exit 1
        fi
    fi

    cd "$guard_corpus_root"

    if [[ "$guard_corpus_mode" == "git" ]]; then
        printf '%s: corpus is the Git-tracked set\n' "$guard_name"
    else
        printf '%s: corpus is the source tree; no Git metadata is available\n' \
            "$guard_name"
    fi
}

# True when the index and the commit history are available to be checked.
guard_corpus_is_git() {
    [[ "$guard_corpus_mode" == "git" ]]
}

# Enumerates the source tree, excluding version-control metadata, the ignored
# workflow directory, and any CMake build tree beneath the root. Symbolic links
# are not followed and not listed; this repository tracks none.
_guard_corpus_find_paths() {
    local -a prune=(-name .git -o -name .archon)
    local cache_path build_root

    while IFS= read -r -d '' cache_path; do
        build_root="${cache_path%/CMakeCache.txt}"
        prune+=(-o -path "$build_root")
    done < <(find . -name .git -prune -o -name .archon -prune -o \
        -name CMakeCache.txt -type f -print0)

    find . \( "${prune[@]}" \) -prune -o -type f -print0 |
        sed -z 's|^\./||'
}

_guard_corpus_load() {
    ((_guard_corpus_loaded == 0)) || return 0

    if [[ "$guard_corpus_mode" == "git" ]]; then
        mapfile -d '' -t _guard_corpus_paths < <(git ls-files -z)
    else
        mapfile -d '' -t _guard_corpus_paths < <(_guard_corpus_find_paths)
    fi
    _guard_corpus_loaded=1
}

# Writes the matching corpus paths, NUL-separated, relative to the root.
#
# A pattern is an ordinary shell pattern matched against the whole relative
# path, in which `*` crosses directory separators exactly as a Git pathspec
# does. A pattern prefixed with `!` excludes. With no include pattern, or with
# the pattern `.`, everything matches.
guard_corpus_list() {
    _guard_corpus_load

    local -a include=() exclude=()
    local specification
    for specification in "$@"; do
        if [[ "$specification" == '!'* ]]; then
            exclude+=("${specification#!}")
        else
            include+=("$specification")
        fi
    done
    if ((${#include[@]} == 0)); then
        include=(".")
    fi

    local path pattern keep
    for path in ${_guard_corpus_paths+"${_guard_corpus_paths[@]}"}; do
        keep=0
        for pattern in "${include[@]}"; do
            if [[ "$pattern" == "." || "$path" == $pattern ]]; then
                keep=1
                break
            fi
        done
        ((keep == 1)) || continue

        for pattern in ${exclude+"${exclude[@]}"}; do
            if [[ "$path" == $pattern ]]; then
                keep=0
                break
            fi
        done
        ((keep == 1)) && printf '%s\0' "$path"
    done
    return 0
}

# Searches the corpus and prints `path:line:text` matches, exiting non-zero
# when there are none, which is what both underlying tools do.
#
# Usage: guard_corpus_grep <search-flags...> -- <pattern...>
#
# With Git present the search reads the index rather than the working tree, so
# a guard run before a commit judges what that commit would contain. Without
# Git there is no index and the working tree is all there is.
guard_corpus_grep() {
    local -a flags=() specifications=()
    while (($# > 0)); do
        if [[ "$1" == "--" ]]; then
            shift
            specifications=("$@")
            break
        fi
        flags+=("$1")
        shift
    done

    local -a paths=()
    mapfile -d '' -t paths < <(guard_corpus_list \
        ${specifications+"${specifications[@]}"})
    ((${#paths[@]} > 0)) || return 1

    if [[ "$guard_corpus_mode" == "git" ]]; then
        git grep --cached "${flags[@]}" -- "${paths[@]}"
    else
        # /dev/null keeps the path prefix on the output when the corpus has
        # narrowed to a single file.
        grep "${flags[@]}" -- "${paths[@]}" /dev/null
    fi
}

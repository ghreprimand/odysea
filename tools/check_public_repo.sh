#!/usr/bin/env bash

set -euo pipefail

# shellcheck source=tools/guard_corpus.sh
source "$(dirname "${BASH_SOURCE[0]}")/guard_corpus.sh"
guard_corpus_init public_repository_guard

failed=0

report_matches() {
    local label="$1"
    shift

    local matches
    if matches="$("$@" 2>/dev/null)"; then
        printf 'public_repository_guard: %s\n%s\n' "$label" "$matches" >&2
        failed=1
    fi
}

corpus_size="$(guard_corpus_list | tr '\0' '\n' | grep -c . || true)"
if ((corpus_size == 0)); then
    printf 'public_repository_guard: the corpus is empty, so nothing was scanned\n' >&2
    exit 1
fi

sensitive_paths="$(
    guard_corpus_list | tr '\0' '\n' |
        grep -E '(^|/)(\.env($|\.)|\.aws/|\.credentials/|\.gnupg/|\.netrc$|\.npmrc$|\.pypirc$|\.secrets/|\.ssh/|credentials([./]|$)|secrets([./]|$)|id_(dsa|ecdsa|ed25519|rsa)|[^/]+\.(age|enc|gpg|key|kdbx|p12|pem|pfx|ppk)$)' ||
        true
)"
if [[ -n "$sensitive_paths" ]]; then
    printf 'public_repository_guard: sensitive path names are tracked\n%s\n' \
        "$sensitive_paths" >&2
    failed=1
fi

# An unresolved merge conflict must never reach the corpus. This is a content
# check rather than a formatting one: a marker left in tracked text publishes a
# file that is visibly broken, and it does so in exactly the files a conflicted
# merge touches most - the record, the roadmap, the contributor guide.
#
# No file-level exclusion list for Git-text files. The pattern is anchored at
# the start of a line and written with repetition counts rather than literal
# runs, so this guard, its own self-test, and any fixture that has to construct
# a marker can all be scanned by it: a marker composed inside a `printf`
# argument does not begin its line. That is what keeps the rule from needing
# the file-level exclusions the at-sign carve-out was written to avoid.
#
# `-I` necessarily skips a file Git reads as binary, so a staged NUL-containing
# file is outside this text scan. The line anchor also does not match after a
# UTF-8 BOM on line one. Neither form occurs in the tracked corpus; both are
# limitations of the text-anchor design, not exclusions to grow into a list.
#
# The conflict styles Git can produce are all covered: `diff3` and `zdiff3` add
# a `|||||||` base section to the two `<<<<<<<` and `>>>>>>>` sides, so the
# base marker is matched too rather than being left as the one spelling that
# could pass. Git's default marker size is seven characters, but its size is
# configurable, so every marker run of at least seven characters is matched.
# The run must still be followed by space or the end of the line; requiring
# that terminator keeps a marker-shaped run continuing into prose from being
# read as a marker.
#
# One consequence of matching runs of at least seven characters: a Markdown
# setext heading underlined with seven or more equals signs is a marker-shaped
# line and is refused, reporting an unresolved conflict for what is a heading.
# Documentation here is written in ATX style, `^=+$` occurs nowhere in the
# tracked corpus, and the alternative is leaving the longer marker runs
# unmatched, so the rule stands as written. It is recorded because the refusal
# message would otherwise be an inexplicable thing to be told about a heading.
report_matches "an unresolved merge conflict marker is tracked" \
    guard_corpus_grep -nI -E '^(<{7,}|\|{7,}|={7,}|>{7,})([[:space:]]|$)' -- .

# The attribution enforcement sources are excluded from the corpus scans below
# for the same reason this script is: their subject matter is the shape of a
# commit identity, so they necessarily contain address syntax and would
# otherwise trip the blanket at-sign rule they exist to support. They are
# reviewed as enforcement code, and no other tracked file may contain an
# at-sign.
self_excluding_pathspec=(
    .
    '!tools/check_public_repo.sh'
    '!tools/check_hooks_selftest.sh'
    '!tools/hooks/*'
)

# Shell expansion syntax requires the at-sign, so the ban on it is lifted for
# those forms alone rather than for the files that contain them. The permitted
# forms are the array-at subscript inside a parameter expansion, the braced
# positional form, and the bare positional form, which covers both the quoted
# and unquoted spellings because the quotes sit outside the token.
#
# A file-level exclusion was rejected. Excluding a shell file wholesale would
# stop the guard reading the rest of it, and the exclusion list would grow one
# entry per script until the ban existed only in the comment describing it. A
# syntax-level carve-out keeps every other at-sign in those files banned: a
# line is scanned again after the permitted forms are removed, so an address
# sitting beside a legitimate expansion on the same line is still caught.
#
# The alternation is written with bracket expressions because a brace carries
# interval meaning in an extended regular expression.
permitted_shell_expansion_re='[$][{]#?[A-Za-z_][A-Za-z0-9_]*[[]@[]]|[$][{]@[^}]*[}]|[$]@'

report_matches "email-like or user-at-host text is tracked" \
    guard_corpus_grep -nI -E '@' -- "${self_excluding_pathspec[@]}" \
    '!*.sh'

# Shell sources are scanned with the permitted expansion forms removed first.
# Anything still holding an at-sign afterwards is text, not syntax.
shell_at_sign_matches="$(
    guard_corpus_grep -nI -E '@' -- '*.sh' \
        '!tools/check_public_repo.sh' \
        '!tools/check_hooks_selftest.sh' 2>/dev/null |
        sed -E "s/${permitted_shell_expansion_re}//g" |
        grep -E '@' || true
)"
if [[ -n "$shell_at_sign_matches" ]]; then
    printf 'public_repository_guard: email-like or user-at-host text is tracked in a shell file\n%s\n' \
        "$shell_at_sign_matches" >&2
    failed=1
fi

report_matches "personal home-directory path is tracked" \
    guard_corpus_grep -nI -E '(/home/[^/[:space:]]+|/Users/[^/[:space:]]+|[A-Za-z]:\\Users\\[^\\[:space:]]+)' \
    -- "${self_excluding_pathspec[@]}"

report_matches "private-key or common access-token signature is tracked" \
    guard_corpus_grep -nI -E \
    '(-----BEGIN ([A-Z0-9]+ )?PRIVATE[[:space:]]KEY-----|AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9]{20,}|xox[baprs]-[A-Za-z0-9-]{10,})' \
    -- .

report_matches "private-network address or local-only transport is tracked" \
    guard_corpus_grep -nI -E \
    '(ssh://|git@|https?://[^/[:space:]]+\.local([/:]|$)|(^|[^0-9])10\.([0-9]{1,3}\.){2}[0-9]{1,3}([^0-9]|$)|(^|[^0-9])192\.168\.[0-9]{1,3}\.[0-9]{1,3}([^0-9]|$)|(^|[^0-9])172\.(1[6-9]|2[0-9]|3[01])\.[0-9]{1,3}\.[0-9]{1,3}([^0-9]|$))' \
    -- "${self_excluding_pathspec[@]}" '!.gitignore'

report_matches "internal workflow narration is tracked" \
    guard_corpus_grep -nI -E \
    '(the operator|the user (asked|requested|wanted)|per operator|Director role|Builder [0-9]|agent workflow|work packet|peer_send|audit[- ]round)' \
    -- "${self_excluding_pathspec[@]}"

# Bare process-role vocabulary. Words with legitimate engineering meanings are
# deliberately absent from this list and matched only in the phrase forms
# above: "worker" names thread-pool members throughout core, "builder" names
# the builder pattern, and bare "operator" is the C++ keyword in every
# operator=, operator==, and operator() declaration. "director" does not
# collide with "directory": the word boundary requires the token to end.
# `.gitignore` is excluded because it legitimately names ignored files.
report_matches "process-role vocabulary is tracked" \
    guard_corpus_grep -nI -iE \
    '\b(reviewers?|directors?|verdicts?|packets?|agents?|subagents?|orchestrators?|coordinators?|the assignee|sign[- ]?off)\b' \
    -- "${self_excluding_pathspec[@]}" '!.gitignore'

report_matches "process-adjacent operator phrasing is tracked" \
    guard_corpus_grep -nI -iE \
    "\\boperator'?s? (approval|approved|asked|decision|instruction|request|review)" \
    -- "${self_excluding_pathspec[@]}"

# --- Third-party project references and derivative framing ------------------
#
# Tracked text describes this project's own behaviour, decisions, verification,
# and gaps. It does not identify another project of the same kind, and it does
# not present a decision as taken from, measured against, or in contest with
# one. Upstream dependencies are the opposite case: a toolkit, a build system,
# a compiler, a bundled typeface and its license are cited as dependencies,
# and naming them is required rather than avoided.
#
# The rules below enforce that category without enumerating instances. A
# deny-list of specific names would publish those names in a tracked file and
# would additionally advertise that the suppression exists; hashing them fixes
# neither, because a short known name falls to a wordlist. Nothing here names a
# project. The one name below is this repository's own forge owner, which is
# the single owner a tracked forge reference may carry.
own_forge_owner='ghreprimand'

# Without an owner the removal step below would strip the host and path prefix
# from every reference and the scan would permit all of them. That is the shape
# of a check that has stopped running, so it is refused rather than assumed.
if [[ -z "$own_forge_owner" ]]; then
    printf 'public_repository_guard: no forge owner is configured, so every forge reference would be permitted\n' >&2
    failed=1
fi

# A hosted-source reference is a host followed by an owner. The separator is
# required, which is what keeps the account-scoped no-reply domain - a bare
# host with nothing after it - from reading as a repository reference.
forge_host_re='((github|gitlab|codeberg|bitbucket|sourceforge|launchpad|gitee|savannah)\.(com|org|net|io|dev|gnu\.org)|(git\.)?sr\.ht)'
forge_reference_re="${forge_host_re}[/:][A-Za-z0-9_.-]+"
own_forge_reference_re="${forge_host_re}[/:]${own_forge_owner}\\b"

# The vendored dependency tree is excluded because the files in it are the
# upstream's own provenance and license text, reproduced verbatim as their
# terms require. That is a stated limit, not a general exemption: a reference
# reaches that tree only by importing a dependency, and it is reviewed there
# as part of the import.
#
# The line is scanned again with this repository's own references removed, for
# the same reason the at-sign carve-out rescans: a third-party reference
# sharing a line with our own must still be reported.
#
# The removal step is run on its own and its exit status is read, rather than
# being buried in a pipeline ending in `|| true`. A filter that fails to
# compile prints nothing, and nothing is indistinguishable from a clean corpus
# once the failure has been swallowed - which is how a delimiter collision in
# this very expression first passed for a green run.
forge_scan="$(
    guard_corpus_grep -nI -E "$forge_reference_re" \
        -- "${self_excluding_pathspec[@]}" '!app/third_party/*' 2>/dev/null || true
)"
if ! forge_scan_without_own="$(
    printf '%s' "$forge_scan" | sed -E "s#${own_forge_reference_re}##g"
)"; then
    printf 'public_repository_guard: the own-owner forge filter failed to run, so no reference was judged\n' >&2
    failed=1
    forge_scan_without_own=""
fi
third_party_forge_matches="$(
    printf '%s' "$forge_scan_without_own" | grep -E "$forge_reference_re" || true
)"
if [[ -n "$third_party_forge_matches" ]]; then
    printf 'public_repository_guard: a third-party hosted-source reference is tracked\n%s\n' \
        "$third_party_forge_matches" >&2
    failed=1
fi

# Derivative and rivalry framing. Every phrase here states a relationship to
# another project rather than a property of this one, which is why the set can
# be matched anywhere in tracked text without a scope.
#
# The weak comparatives are the reason this rule is written the way it is.
# "unlike", "compared to" and "similar to" all have ordinary technical uses in
# prose about this project's own behaviour, and the category noun is worse: the
# thumbnail cache is interoperable precisely because its escaping matches what
# other desktop file managers produce, and saying so is a specification
# statement. Matching either on its own would report correct prose several
# times over, and a check that reports correct prose is one people route
# around. So a weak comparative is matched only in the construction that makes
# it a comparison - the comparative immediately governing a set this project is
# being placed within or against.
#
# One limitation, stated rather than hidden: the search is line-oriented, so a
# phrase broken across a wrapped line is not matched. Wrapping cannot be
# relied on to hide anything, because the phrase has to survive an edit to stay
# wrapped, but it is a gap in the pattern rather than in the rule.
report_matches "derivative or comparative framing is tracked" \
    guard_corpus_grep -nI -iE \
    '(inspired by|inspiration (from|for)|takes? (its )?cues? from|borrowed from|lifted from|cribbed from|ported from|a port of|modell?ed (on|after)|patterned after|in the style of|re-?implementation|re-?implements|drop-in replacement|feature parity with|prior art|competitors?\b|(competing|rival) (projects?|applications?|implementations?|file managers?)|\bfork of\b|(unlike|similar to|compared to|compared with|better than|worse than|nicer than|cleaner than|faster than) +(other|another|existing|most|many|comparable|competing|rival|third-party)\b|\bas (do )?(other|most|many) +[a-z-]+ +(file managers?|projects?|applications?|tools?|implementations?))' \
    -- "${self_excluding_pathspec[@]}"

# A section that exists to credit or to survey other work. The heading is the
# whole signal: the words below are unremarkable in running prose - one of them
# appears in this repository as an ordinary verb - and only become a survey of
# other projects when they head a section. "Attribution" is deliberately absent
# because commit attribution is a policy this project documents under that
# name.
report_matches "a credits or prior-work section heading is tracked" \
    guard_corpus_grep -nI -iE \
    '^#{1,6}[[:space:]]+(credits?|acknowledge?ments?|inspirations?|thanks|related (projects?|work)|see also|alternatives|prior work|prior art|comparisons?)[[:space:]]*$' \
    -- "${self_excluding_pathspec[@]}"

# Attribution must use the account-scoped GitHub no-reply form
# <numeric-account-id>+<login>@users.noreply.github.com. Requiring the numeric
# account prefix is what makes the check meaningful: a bare local part such as
# a project or role name is still syntactically a no-reply address, but GitHub
# resolves it to whichever unrelated account happens to own that login. Only
# the account-scoped form is guaranteed to map to this repository's owner.
owner_identity_re='^[0-9]+\+[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?@users\.noreply\.github\.com$'

# The checks below read commits, so they need a repository and not merely a
# source tree. An extracted tarball has no history to attribute, which is a
# different thing from history that has not been checked - and the difference
# has to be said out loud, because a check that quietly evaporates is
# indistinguishable from one that passed.
commits_examined=0

unsafe_identity_commits=""
observed_identities=""
while IFS=$'\t' read -r commit author_email committer_email; do
    if [[ ! "$author_email" =~ $owner_identity_re ]] ||
        [[ ! "$committer_email" =~ $owner_identity_re ]]; then
        unsafe_identity_commits+="${commit}"$'\n'
    fi
    observed_identities+="${author_email}"$'\n'"${committer_email}"$'\n'
    commits_examined=$((commits_examined + 1))
done < <(if guard_corpus_is_git; then git log --format='%H%x09%ae%x09%ce'; fi)
if [[ -n "$unsafe_identity_commits" ]]; then
    printf 'public_repository_guard: commits are not attributed to an account-scoped no-reply identity\n%s' \
        "$unsafe_identity_commits" >&2
    failed=1
fi

# Every commit must carry the same identity. A second well-formed identity is
# still a second published contributor, which is the outcome this guards.
distinct_identities="$(printf '%s' "$observed_identities" | sort -u | grep -c . || true)"
if ((distinct_identities > 1)); then
    printf 'public_repository_guard: history carries %s distinct commit identities; expected exactly one\n' \
        "$distinct_identities" >&2
    failed=1
fi

# Machine-generated collaboration trailers must never reach published history.
# The match is case-insensitive and covers the trailers emitted by common
# coding agents; any of them would publish a second contributor on the commit.
attribution_trailer_re='^[[:space:]]*(co-authored-by|signed-off-by|assisted-by|generated-by|created-by|authored-by|on-behalf-of):'
trailer_commits=""
while IFS= read -r commit; do
    if git show -s --format=%B "$commit" | grep -qiE "$attribution_trailer_re"; then
        trailer_commits+="${commit}"$'\n'
    fi
done < <(if guard_corpus_is_git; then git log --format=%H; fi)
if [[ -n "$trailer_commits" ]]; then
    printf 'public_repository_guard: attribution trailers are present\n%s' \
        "$trailer_commits" >&2
    failed=1
fi

# A repository with commits in it must have had them examined. Without that
# floor the two loops above would report success over a history they never
# read, which is exactly the shape of the failure this guard was extended for.
if guard_corpus_is_git && ((commits_examined == 0)); then
    printf 'public_repository_guard: no commit was examined for attribution\n' >&2
    failed=1
fi

if ((failed != 0)); then
    exit 1
fi

if guard_corpus_is_git; then
    printf 'public_repository_guard: %d corpus paths and %d commits passed\n' \
        "$corpus_size" "$commits_examined"
else
    printf 'public_repository_guard: %d corpus paths passed; attribution was not checked, as this tree carries no history\n' \
        "$corpus_size"
fi

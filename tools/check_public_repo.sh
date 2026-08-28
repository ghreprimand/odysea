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
# prose about this project's own behaviour. Matching them on their own would
# report correct prose several times over, and a check that reports correct
# prose is one people route around. So a weak comparative is matched only in
# the construction that makes it a comparison - the comparative immediately
# governing a set this project is being placed within or against.
#
# The peer-category noun is matched, and only that noun. Interoperability is a
# real and frequent subject here: the thumbnail cache is shared, so prose about
# what the rest of the system can read is required and stays permitted. What is
# refused is the enumerated peer group - a qualifier such as "most" or
# "existing" in front of the project category itself - because there is no way
# to write that except as a statement about a set of peers.
#
# One limitation, stated rather than hidden: the search is line-oriented, so a
# phrase broken across a wrapped line is not matched. Wrapping cannot be
# relied on to hide anything, because the phrase has to survive an edit to stay
# wrapped, but it is a gap in the pattern rather than in the rule.
report_matches "derivative or comparative framing is tracked" \
    guard_corpus_grep -nI -iE \
    '(inspired by|inspiration (from|for)|takes? (its )?cues? from|borrowed from|lifted from|cribbed from|ported from|a port of|modell?ed (on|after)|patterned after|in the style of|re-?implementation|re-?implements|drop-in (clone|replacement)|feature parity with|prior art|competitors?\b|(competing|rival) (projects?|applications?|implementations?|file managers?)|\bfork of\b|(unlike|similar to|compared to|compared with|better than|worse than|nicer than|cleaner than|faster than) +(other|another|existing|most|many|comparable|competing|rival|third-party)\b|\bas (do )?(other|most|many) +[a-z-]+ +(file managers?|projects?|applications?|tools?|implementations?)|\b(most|many|other|another|existing|mainstream|popular|established|conventional|competing|rival|comparable) +([a-z-]+ +)?file managers?\b|\b(other|established|existing|mainstream) +(desktop|terminal) +(implementations?|applications?)\b)' \
    -- "${self_excluding_pathspec[@]}"

# A peer implementation named by its standing rather than by its name. The rule
# above reaches this shape only through the spelling
# "<qualifier> <desktop|terminal> <implementation>", and a line wrapped between
# the qualifier and the noun leaves the rest of the phrase alone on a line the
# rule cannot match. One published line in this repository is exactly that: the
# qualifier and its noun sit together, the derivation verb is on the line above,
# and the corpus scan reported nothing.
#
# The qualifier set is what does the work, and it is deliberately narrower than
# the one above. "Other" and "another" are ordinary English and appear in
# correct prose here - interoperability statements about what other software can
# read, the policy's own description of what it forbids, and the license text -
# so widening the noun phrase to admit them reports six lines of correct prose,
# three of them in published entries that can no longer be edited. The words
# below have no such use: calling an implementation established, mainstream,
# conventional, comparable or popular is a claim about its standing among peers,
# which is the claim this rule exists to refuse. Measured over the corpus the
# narrow set reports exactly one line, and that line is the live instance.
peer_standing_re='\b(established|mainstream|conventional|comparable|popular) +(([a-z-]+) +)?(implementations?|applications?|programs?|tools?|projects?)\b'

# That one line is in the published archive, where entries are immutable by
# rule: it cannot be reworded, so the rule would stand red over text nobody may
# edit, and a permanently red gate is one that gets deleted. The correction is
# published as a new dated entry instead, and the archived line is exempted
# here by its exact text.
#
# The cost is stated rather than hidden. Recording the exemption necessarily
# writes the refused phrase into this file, and this file is the one the corpus
# scan excludes, so the phrase lives in the only place it cannot be caught. The
# alternative is a rule that provably misses a live published instance, which
# is worse. Two floors keep the exemption from becoming a habit: it must still
# match something, so it cannot outlive the text it excuses, and it is a single
# exact line rather than a pattern, so it cannot quietly widen. Anything else
# the rule finds is reported.
exempt_published_line='established implementation rather than from this one.'

peer_standing_scan="$(
    guard_corpus_grep -nI -iE "$peer_standing_re" \
        -- "${self_excluding_pathspec[@]}" 2>/dev/null || true
)"
exempted_published_matches=0
peer_standing_matches=""
while IFS= read -r match_line; do
    [[ -n "$match_line" ]] || continue
    match_path="${match_line%%:*}"
    match_text="${match_line#*:}"
    match_text="${match_text#*:}"
    if [[ "$match_path" == docs/devlog/* &&
        "$match_text" == "$exempt_published_line" ]]; then
        exempted_published_matches=$((exempted_published_matches + 1))
        continue
    fi
    peer_standing_matches+="${match_line}"$'\n'
done <<<"$peer_standing_scan"

# The floor applies wherever the archived record exists. A corpus with no
# archive holds nothing for the exemption to excuse - that is every throwaway
# fixture the accompanying self-test builds - and demanding the line there
# would fail the guard for the absence of a file the corpus never claimed to
# have. Where the archive is present the line must be too, so the exemption
# cannot outlive the text it was written for.
archived_record_paths="$(
    guard_corpus_list 'docs/devlog/*' | tr '\0' '\n' | grep -c . || true
)"
if ((archived_record_paths > 0 && exempted_published_matches == 0)); then
    printf 'public_repository_guard: the archived line this rule exempts is no longer in the record, so the exemption excuses nothing\n' >&2
    failed=1
fi
if [[ -n "$peer_standing_matches" ]]; then
    printf 'public_repository_guard: a decision is attributed to a peer implementation\n%s' \
        "$peer_standing_matches" >&2
    failed=1
fi

# Placing this project within a field. The rule above catches a sentence that
# names or characterises a peer; this one catches the paragraph that needs no
# peer at all - a survey of a field, a gap identified inside it, and this
# project set in that gap. Written without a single name it passes every check
# above, and it is the same claim.
#
# The phrases are structural rather than topical: a field noun attached to this
# project's category, a placement verb, and the two-poles-and-a-middle
# construction that a positioning argument almost always reaches for.
report_matches "comparative positioning within a field is tracked" \
    guard_corpus_grep -nI -iE \
    '((file[- ]manager|application|product|tool)s?[- ](landscape|market|ecosystem)|occupies the (gap|space|middle|niche|ground)|the middle ground|tends? to (sit|fall|land|cluster) +(at|in|into)\b|two extremes|\bas +[a-z]+ +as any +[a-z-]+( +[a-z-]+)? +(file managers?|applications?|tools?|programs?|implementations?))' \
    -- "${self_excluding_pathspec[@]}"

# Every rule above keys on framing: a survey sentence, a placement verb, a
# section heading. Strip the framing and keep the list, and all of them go
# quiet - which is the wrong way round, because the framing is the part a
# writer would drop and the list of names is the part they would keep. The two
# rules below read the shape of the list itself. Neither knows a single name.
#
# The first is the label. A bullet introduced by a class of this project's own
# category - a qualified or plural "file manager", "explorer", or "browser" -
# is dividing a field into groups, and there is no other reason to write it:
# every legitimate sentence here is about this one program, in the singular.
# The singular bare form is therefore permitted and only the qualified or
# plural label is refused. The label has to be closed, by emphasis marks or by
# a colon or dash, so that an ordinary sentence running through the same words
# is not read as a heading for a group.
prose_category_label_re="((([A-Za-z][A-Za-z0-9/-]*[[:space:]]+){1,3}(file[ -]managers?|explorers?|browsers?))|file[ -]managers|explorers|browsers)"
prose_emphasis_re='(\*\*|__|\*|_)'
report_matches "a list item is labelled as a class of file manager" \
    guard_corpus_grep -nI -iE \
    "^[[:space:]]*([-*+]|[0-9]+[.)])[[:space:]]+(${prose_emphasis_re}[[:space:]]*${prose_category_label_re}[[:space:]]*${prose_emphasis_re}|${prose_category_label_re}[[:space:]]*[:—–-])" \
    -- "${self_excluding_pathspec[@]}"

# The second is the enumeration. "A, B, C. Fast, but limited" is a survey of
# other work whatever A, B and C turn out to be, so the rule counts the shape
# and never resolves the names: three or more comma-separated items on one
# line, inside a block that also states a limitation.
#
# The limitation word usually sits on a later line of the same bullet, so the
# scan is block-scoped rather than line-scoped. A block is broken by a blank
# line, a heading, or the start of another list item, which keeps two adjacent
# and unrelated paragraphs from being read as one.
#
# The list also has to be the whole clause. A survey names its group and
# stops - the names end the sentence, or the line, and the assessment follows
# after. Correct prose here does the opposite: it names three inputs and then
# says what they do, so the list is the subject of a verb and the sentence
# runs on. That is the difference between "Right-click, Menu, and Shift+F10
# open the same shared context actions" and a group of names terminated by a
# full stop, and it is the reason the run must be closed by a sentence break
# or the end of the line. Without that condition the design document's own
# input list is reported, which is correct prose and would have to be worked
# around.
#
# The thresholds were measured against this corpus rather than chosen. Letting
# an item be any lowercase word reported five lines of correct prose, two of
# them in published record entries that can no longer be edited. Requiring
# three items with two capitalised still reported the design document's input
# list. The pair below is the widest reading that reports nothing at all:
# three capitalised items, or four items of which at least two are
# capitalised. It reports both halves of the enumeration that prompted it.
#
# The awk program is run on its own with its exit status read, and it reports
# how many lines it examined. A pattern that fails to compile prints nothing,
# and nothing is indistinguishable from a clean corpus once the failure has
# been swallowed.
block_enumeration_program='
BEGIN {
    capitalised = "[A-Z][A-Za-z0-9_+'"'"'-]*( [A-Z][A-Za-z0-9_+'"'"'-]*)*( \\([^)]*\\))?"
    lowercase = "[a-z][A-Za-z0-9_+-]*( \\([^)]*\\))?"
    item = "(" capitalised "|" lowercase ")"
    separator = ",[ \t]*(and |or )?"
    conjunction = "[ \t]+(and|or)[ \t]+"
    join = "(" separator "|" conjunction ")"
    closing = "([.;][ \t]+[A-Z]|[.;]?[ \t]*$)"
    enumeration = item separator item join item "(" join item ")*" closing
    limitation = "(^|[^A-Za-z])(but|though|however|yet|whereas|constrained|tied to|limited|restricted)([^A-Za-z]|$)"
    item_boundary = ",|[ \t]+(and|or)[ \t]+"
}
function count_items(run, capitals_only,   fields, index_, total) {
    total = 0
    for (index_ = split(run, fields, item_boundary); index_ > 0; index_--) {
        sub(/^[ \t]*(and |or )?/, "", fields[index_])
        # A comma immediately followed by the conjunction splits twice and
        # leaves an empty field between them. Counting it would inflate the
        # item total by one for every serial comma in the run, which is the
        # difference between a three-item list and the four-item threshold.
        if (fields[index_] ~ /^[ \t]*$/) continue
        if (!capitals_only || fields[index_] ~ /^[A-Z]/) total++
    }
    return total
}
function end_block() {
    if (enumeration_reference != "" && block_states_a_limitation) {
        print enumeration_reference ":" enumeration_text
    }
    enumeration_reference = ""
    enumeration_text = ""
    block_states_a_limitation = 0
}
{
    scanned++
    split($0, prefix, ":")
    path = prefix[1]
    line_number = prefix[2]
    text = substr($0, length(path) + length(line_number) + 3)

    if (path != current_path) {
        end_block()
        current_path = path
    }
    if (text ~ /^[ \t]*$/ || text ~ /^[ \t]*#/ ||
        text ~ /^[ \t]*([-*+]|[0-9]+[.)])[ \t]/) {
        end_block()
    }
    if (text ~ limitation) {
        block_states_a_limitation = 1
    }
    if (enumeration_reference == "" && match(text, enumeration)) {
        run = substr(text, RSTART, RLENGTH)
        capitals = count_items(run, 1)
        if (capitals >= 3 || (count_items(run, 0) >= 4 && capitals >= 2)) {
            enumeration_reference = path ":" line_number
            enumeration_text = text
        }
    }
}
END {
    end_block()
    print "guard_scanned_lines=" scanned + 0
}
'
if ! block_scan_output="$(
    guard_corpus_grep -nI -E '^' -- "${self_excluding_pathspec[@]}" |
        awk "$block_enumeration_program"
)"; then
    printf 'public_repository_guard: the block enumeration scan failed to run, so no block was judged\n' >&2
    failed=1
    block_scan_output="guard_scanned_lines=0"
fi

scanned_prose_lines="$(
    printf '%s\n' "$block_scan_output" | sed -n 's/^guard_scanned_lines=//p'
)"
if [[ ! "$scanned_prose_lines" =~ ^[0-9]+$ ]] || ((scanned_prose_lines == 0)); then
    printf 'public_repository_guard: the block enumeration scan examined no corpus line\n' >&2
    failed=1
fi

block_enumeration_matches="$(
    printf '%s\n' "$block_scan_output" | grep -v '^guard_scanned_lines=' || true
)"
if [[ -n "$block_enumeration_matches" ]]; then
    printf 'public_repository_guard: an enumeration of named items is qualified by a limitation\n%s\n' \
        "$block_enumeration_matches" >&2
    failed=1
fi

# A section that exists to credit other work, to survey it, or to place this
# project relative to it. The heading is the whole signal: the words below are
# unremarkable in running prose - one of them appears in this repository as an
# ordinary verb - and only become a survey of other projects when they head a
# section. "Attribution" is deliberately absent because commit attribution is a
# policy this project documents under that name.
report_matches "a credits, prior-work, or positioning section heading is tracked" \
    guard_corpus_grep -nI -iE \
    '^#{1,6}[[:space:]]+(credits?|acknowledge?ments?|inspirations?|thanks|related (projects?|work)|see also|alternatives|prior work|prior art|comparisons?|positioning|landscape|competition|market|where it fits|how it compares)[[:space:]]*$' \
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

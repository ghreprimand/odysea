# OdySea — Devlog

Public running record of OdySea development, in reverse-chronological order.
Each entry records what landed, how it was verified, and any known gaps. See
`docs/ROADMAP.md` for milestone status and `docs/DESIGN.md` for durable product
and architecture decisions.

This file holds the most recent entries. Older ones move verbatim into
`docs/devlog/`, either as `YYYY-MM.md` once a month has closed or, when the
record reaches the tracked-file line ceiling before that, as numbered
`YYYY-MM-partN.md` files holding consecutive stretches of an open month. A
month is archived one way or the other, never both. A new entry belongs at the
top of this file and never in an archive. Published entries are never edited,
reordered, reworded, or removed, whether they live here or in the archive;
`docs/devlog/published-entries.txt` records every entry heading in reading
order, and the archive gate compares it against what the files actually hold.

**Archived record**, most recent first:

- [2026-08 part 6](docs/devlog/2026-08-part6.md)
- [2026-08 part 5](docs/devlog/2026-08-part5.md)
- [2026-08 part 4](docs/devlog/2026-08-part4.md)
- [2026-08 part 3](docs/devlog/2026-08-part3.md)
- [2026-08 part 2](docs/devlog/2026-08-part2.md)
- [2026-08 part 1](docs/devlog/2026-08-part1.md)
- [2026-07](docs/devlog/2026-07.md)

---

## 2026-08-30 -- Lifetime gates outlive the objects they protect

A filesystem operation can continue after its directory model disappears. Its
progress observer used to consult an atomic flag stored inside that model
before queueing a report. The flag described the right policy but had the wrong
lifetime: once destruction freed the model, even reading whether it still
accepted reports was already an invalid access.

Operation progress now crosses a shared delivery gate that owns no model. The
gate protects a non-owning receiver while a report checks and queues, and model
destruction clears that receiver under the same lock before requesting
cancellation. Destruction therefore does not wait for filesystem I/O, while a
worker that resumes afterwards sees no receiver and touches no model state.
Queued deliveries consult the same gate again on the model thread.

The destruction case now holds the first progress report at a deliberate
rendezvous, destroys the model, and only then lets the report continue. This
makes the lifetime boundary an observed event rather than a race. Restoring the
freed-member read made that named case report a heap use after free in three of
three AddressSanitizer runs; the shared gate passed five of five runs before the
full batteries.

The transfer contract also distinguishes two failure shapes that need different
recovery. A crossing move can fail after installing a complete destination and
leave a source remainder beside it. Separately, failed replacement recovery can
leave an expected name absent: the former destination occupant remains under a
Replaced working name, and a source that could not be unwound remains under a
Prepared working name. Both are recognized through the public working-entry
classifier and recovered through those names; neither is debris to delete.

Release and AddressSanitizer builds and tests pass, as do scoped static analysis,
formatting, file-length, public-repository, development-record, and
key-construction gates. The compositor-only cases retain their declared skips;
real-compositor behavior was not measured.

---

## 2026-08-30 -- The formatter could write a row-key alias the guard admitted

The key-construction guard keeps row-key construction to a single counted
function, so a large-directory cost gate can read the key count and trust it.
Its second rule catches an entry's path taken as text, in two branches: a
conversion such as `entry.path.string()`, and an alias that binds `entry.path`
to a new name so the conversion can be spelled on the alias instead.

The alias branch was matched against a physical line, and the project's own
formatter defeated it. `.clang-format` is LLVM at a 100-column limit, and a
reference alias whose name pushes the declaration past that limit is wrapped
after the `=`, leaving `entry.path;` on the next line. A rule anchored on `=`
then `.path;` saw the two halves separately and matched neither, so an alias
was exempted for nothing more than growing long — no author intent, written by
the formatter that every commit runs. The pre-change guard accepted the
wrapped form; it now rejects it, reporting the declaration's start line.

The alias branch now matches a statement rather than a line. A declaration
whose last token is a bare assignment `=` is folded together with the line that
follows before matching, which is the only continuation the formatter produces
for these declarations: a long `std::move` initializer wraps the same way, and
a brace initializer is never wrapped. That was measured against each long form,
not assumed. The pattern also accepts a brace as the initializer introducer and
no longer requires `.path` to be the last token before the `;`, so the two
formatter-stable one-line evasions are caught as well — a brace instead of `=`,
and a `)` between `.path` and the `;` from a move. Three self-test scenarios —
the wrapped form, the brace form, and the move form — each fail by name when
the corresponding half of the rule is reverted.

The conversion branch is deliberately left line-oriented. The formatter keeps
`.path.string()` adjacent in every wrap it produces and rejoins a hand-split
chain, so a conversion cannot be separated from its receiver by the gate; only
the alias branch needed the statement view.

A claim about the guard's residuals is corrected here. The guard listed four
things it does not catch and stated that each was pinned by a scenario
asserting acceptance. Only two were — a path compared against a path, and an
implicit conversion. The typedef alias, where the bound name's type is a
typedef or alias template of the path type rather than `auto` or the type
written out, was described as a residual but pinned by nothing; it is pinned
now, and the pin is behavioural, since widening the branch to accept the
typedef spelling flips that scenario from accepted to rejected. The fourth
residual — a conversion member with a name nobody has written yet — is
unpinnable in principle, because any concrete name a scenario could spell is
either one the shape already catches or one that builds no key, and neither
tests the residual. Three of four are pinned; the fourth is declared as
unpinnable rather than described as covered.

The self-test grows from thirty-seven expectations to forty-one under its
count floor. The guard passes the real tree unchanged at seven covered sources,
two permitted normalizations, and three permitted entry-path spellings.

---

## 2026-08-30 -- Renaming many entries is two jobs, and only one of them writes

Bulk rename is split at the point where writing begins. Planning takes an
ordered set of entries and a rule and produces the whole old-to-new mapping,
reading the filesystem and changing nothing, so a caller can render it as a
live preview. Application takes a plan and performs it. A plan that reports a
problem is never applied.

The obvious collision is not the interesting one. Two entries given the same
new name is visible in a preview at a glance. The two that a preview cannot
show are a name already held by an entry outside the batch, and a name held by
another member of the batch.

Two separate guarantees cover the second, and separating them corrected a claim
this entry first made. Every rename the engine issues refuses an occupied name
instead of replacing it, so a batch performed in a bad order stops rather than
destroying anything — the filesystem primitive underneath replaces by default,
so that refusal is a decision, not a property of renaming. Sequencing is the
other half, and what it buys is completion: without it a batch that shifts a
run of names along by one refuses at its first step and finishes nothing. The
measurement said so plainly. Removing the sequencing failed twenty checks, and
every one of them was a batch that had not completed rather than an entry that
had been overwritten.

So a name held by a departing member is not an error, and refusing it would
have been the easy wrong answer. Planning marks such a step as deferred and
application orders the batch behind it; only a name held by something that is
not leaving blocks it. An entry that cannot take its own new name is not
leaving either, so a batch whose occupant is itself refused is refused as a
whole.

Some batches have no safe order at all. Exchanging two names is a closed cycle:
neither entry can move first. Application breaks it by moving one entry to a
working name, and it reserves that name through the same scheme the other
mutation primitives use, so an entry left standing under one can still be
recognised and explained rather than appearing as an unaccountable name. Two
logical renames cost three physical ones, and the reported result names where
each entry started and where it finally landed, never the working name it
passed through.

A name that differs from an existing entry only by case is a collision on a
filesystem that folds case and a free name on one that does not. It is decided
by asking rather than by guessing: the directory's literal names are read once,
and a proposed name that is absent from that listing but still resolves to
something has been folded onto an entry that is already there. The fold is
reported separately from an ordinary occupied name, because a preview cannot
otherwise explain it — the name the user is being warned about is not visible
in the listing they are looking at. Where the fold resolves to the entry being
renamed, it is a change of case in place, which is a real rename and not a
collision.

None of the filesystems available here fold case, so that check could never
have been reached by waiting for a real one, and a case that waited would have
reported a pass for a check that never ran. The two filesystem questions are
answered through an injectable step, and the cases supply a folding answer, so
the behaviour is measured on any filesystem.

A batch is not a transaction and the contract does not pretend otherwise. The
plan is rechecked against the filesystem immediately before the first rename is
issued, from the sources and the rule the plan carries, so a preview read
minutes ago is not applied to a directory that has changed underneath it; a
source that has vanished, or that now resolves to a different entry under the
same name, stops the batch before anything is written. After that the renames
happen one at a time against a filesystem other programs share, and one can
still fail. What was already done is reported exactly, oldest first, rather
than described by an absolute that cannot be held. An entry standing under a
working name when a batch stops is put back where it started if that name is
free, and reported if it is not, because an entry under an unexpected name can
be recovered and a removed one cannot.

The journal records one entry per completed rename rather than one for the
batch. A single record would either describe a batch that did not finish, which
is the defect a completed-operation history exists to prevent, or would have to
be discarded on failure and take the reversibility of the renames that did
succeed with it. Reversal is per record, newest first, which is the reverse of
the order the batch performed them in; since that order never writes over an
entry that has not left, unwinding it finds each name free again. A batch
containing a cycle is the exception, and it is settled when the records are
made rather than when a reversal is attempted: no single-record reversal can
retrace a hop through a working name, so every record of such a batch carries a
barrier and none of them is ever offered as reversible and then refused.

Forty-seven cases cover the engine, listed in one table with a count floor so a
case that stops being called fails by name. Each was measured by reverting the
check it depends on: thirty-two reverts, every one proven to have landed by
diffing the file before rebuilding, and every one caught. Five of them were
first rejected by the compiler for an unused parameter the revert had orphaned,
which is not a result — a revert that does not build has measured nothing — so
each was rewritten until it compiled and then measured.

---

## 2026-08-30 -- Quick preview owns its work until dismissal completes

The focused entry now opens in one modal quick-preview surface from
`Ctrl+Space` or the toolbar. Escape, the close control, and a press outside all
dismiss it, cancel its current load, and return focus to the originating
directory view without changing the current entry. Raster images are decoded
to a bounded surface, plain text is read up to a stated one-mebibyte limit, and
Markdown uses the shell's built-in document renderer. Formats that need a
renderer the shell does not link state that gap instead of showing an empty
preview.

Every load runs off the interface thread under a generation and a cooperative
cancellation flag. The model keeps each watcher owned until its worker
acknowledges completion, while a replaced or dismissed generation can never
publish stale content. The overlay uses the existing panel, well, ink, border,
focus, and selection tokens. Strong, Balanced, and Minimal retain the bounded
transition; Off, high contrast, and reduced motion remove its time without
changing content luminance. The same opaque surface and painted image path stay
interactive on the forced software renderer.

Verified: focused content-loader, input-parity, and software-fallback tests;
QML lint and formatting gates; release and ASan/UBSan batteries. A genuine
compositor frame was not measured because no declared isolated compositor was
available, and no test used a live session.

---

## 2026-08-30 -- Accent repairs explain themselves and large listings stay bounded

Accent selection now reports when its authored value cannot reach every rendered
surface without repair. The controller measures that pre-repair value with the
same window, selected-entry, hover, pressed, and panel samples used by the
resolver, then names each failing surface in the appearance panel. The resolver
continues to supply a contrast-cleared token; the warning is additional rather
than a weakened safety boundary. It is exposed after keyboard and pointer
selection, announced through the accessibility interface, and absent for an
authored value that already clears every floor.

The software visual validation fixture holds 2,000 neutral entries. It keeps
list and grid delegates within geometry-derived viewport bounds, leaves the
effect structure flat, and now proves that grid delegates request thumbnails
only for the visible batch, release that batch across an end jump, and request
the destination batch without scheduling the directory. The adapter cost case
uses separate synthetic directories: a serial release run measured 60,768 key
constructions at 4,000 entries and 123,456 at 8,000, a 2.03 growth against its
1.50–2.80 bound, with 55,526 and 113,924 microseconds of processor work across
the scan-and-refresh cycles.

Verified: serial release and ASan/UBSan CTest batteries; focused
theme-controller and input controls; software visual validation; and the
serial directory-model cost battery.

---

## 2026-08-30 -- A sixth August part keeps the live record clear of the ceiling

August is still open and the live record reached the point where a further
round of entries would have crossed the 2,000-line tracked-file ceiling during
integration rather than before it. The entries published on 2026-08-27 and
2026-08-28 move verbatim into `docs/devlog/2026-08-part6.md`, which becomes the
most recent archive part and is listed first in the archive index. The live
record keeps the entries published from 2026-08-29 onward.

Nothing is reworded, reordered, or dropped. The archive gate recombines the
published baseline across the live record and every part, so the split is
checked rather than attested: all forty entries remain present, in the same
reading order, with unchanged bodies. `docs/devlog/published-entries.txt` is
unchanged, because the moved entries sat at the end of the live record and the
new part is the first archive read, which is the same total order.

---

## 2026-08-30 -- Record moves reassemble exactly and family accents remain literal

The archive guard now recombines the live record and every archive part from
the published baseline before it accepts a split. Each baseline entry must
remain present with its body unchanged in the current collection. The shared
entry normalizer ignores only the trailing separator that legitimately moves
with an entry between files; the boundary census continues to judge the
physical separators in each file. The self-test covers a verbatim part split,
an altered entry body, and an entry that leaves the live file without reaching
its part.

Each family’s default accent now clears every rendered accent surface as its
authored sRGB value. A light-family source accent that required contrast repair
was minimally adjusted at the palette definition, and the complete family set
is now held by a test that checks the authored color, the rendered token, and
every window, selected, hovered, pressed, and panel surface. The other shipped
families cleared without a source change. Alternate accent presets remain
normalized at the token boundary because their hue is intentionally shared
across families.

The high-contrast effect override is now one branch: it disables all emissive
and temporal levels and restores the unmodified text lift, while reduced
motion still disables persistence only.

Verified: archive guard and its 75-scenario self-test; focused appearance and
theme-controller tests.

---

## 2026-08-30 -- Cancellation reaches trees with no file contents

Cooperative cancellation now has discriminating coverage at both traversal
boundaries. A pre-cancelled observed transfer stops during measurement and
never publishes the transfer phase. A directory-only tree cancels inside the
copy walk even though no regular-file loop can supply another checkpoint; the
working entry is discarded, the destination stays unchanged, and the source
tree stays intact. The control contract also verifies that cancellation
supersedes a pending pause in its public state, while the checkpoint predicate
remains the mechanism that wakes parked work.

The transfer guarantee now distinguishes failure before installation from the
later source-removal failure of a crossing move. No partial destination is
installed. If removal fails after a complete destination was installed, the
operation reports failure and the source or a remainder can remain beside that
complete destination. This is accepted because deleting the installed copy
could discard the only complete result; the completed-operations-only journal
does not record the failed move, so cleanup remains explicit.

The restored checkpoints pass the transfer suite. Removing the directory-walk
checkpoint makes only the directory-only cancellation case fail; removing the
measurement checkpoint makes only the measurement-phase case fail; and leaving
the pause request set after cancellation makes only the public-state case fail.
Release and ASan/UBSan verification pass with the repository guards.

---

## 2026-08-30 -- One spelling rule now covers the spellings around it

Row keys in the directory model must be built in one place, because the count
of them is what a large-directory gate reads. The rule that holds that had
three ways past it and one instrument that had stopped working. All four were
reproduced on the unmodified sources before anything was changed.

A member definition had no end. The enclosing-member lookup set a name when it
saw a definition at column zero and never cleared it, so a free helper placed
below a permitted member inherited that member's name and was admitted. It was
worse than admitted: it was counted as a permitted sighting, so the bypass
raised the number the rule's own emptiness check reads and made the guard look
healthier for holding a hand-spelled key. Every source in this model happens to
open with its anonymous namespace, which is the only reason nothing had landed
there, and nothing required that ordering. A member's body closes in column
zero and so does an anonymous namespace, so one rule ends both and a match
between two definitions now belongs to neither.

An alias erased the pattern in one line. The conversion rule needed an entry's
`path` member adjacent to the conversion, and `const auto& p = entry.path;`
separates them while leaving the same key free to be built from the alias. The
rule now also matches binding an entry's `path` to a name whose declared type
is `auto` or a filesystem path. It is typed rather than general on purpose:
the tab state in these files carries its own `path` member of interface string
type, and a rule matching any such binding would have rejected the shipped
sources on the day it landed. Both directions are pinned by scenarios.

The conversion list was five names where the standard offers thirteen.
`wstring`, `u16string`, `u32string`, the four generic forms, and `string<char>`
written as the member template it is were all accepted, and on this platform
the generic forms are byte-identical to the native ones, so each produced the
counted key exactly. The rule now matches by shape rather than by a list.

The comment above that list was the worst part of it. It said a conversion
outside the list would be reported by the guard's self-test, and no such check
existed — a sentence that tells the next reader not to look is worse than
silence, and eight of the conversions it implied were covered were already in
the standard when it was written. The enumeration it promised now exists: every
conversion is planted in turn and must be rejected, under a count floor, so the
list cannot shrink quietly the way the rule's did.

What the guard still does not catch is now written down beside what it does,
and each residual is pinned by a scenario asserting that the guard accepts it,
so the list cannot stop describing the guard. A path compared against a path
builds no string at all. An entry's path converts to its native string type
with no conversion member named anywhere, so `QString::fromStdString(entry.path)`
produces the counted key from a line that mentions no conversion; catching it
needs the argument's type, which is not on the line, and widening the rule to
any mention of the member was tried and rejected because these files hold
ordinary uses that would each have had to be permitted. An alias whose type is
spelled through a typedef is not recognized. A future conversion named after
none of `string`, `native`, or `c_str` is covered by no shape.

Separately, the check that the normalization rule has not gone dead could be
deleted with the whole suite green. The scenario named for it asserted only the
words the two emptiness checks share, and its fixture makes the other one fire
with those same trailing words, so the scenario passed on the other rule's
message. It now asserts the half that belongs to its own rule.

That scenario survived because the mutation battery was asymmetric: all seven
of its mutations targeted the newer rule and the older one had none. The
battery now covers both rules the same way — never applied, status swallowed,
permitted list widened, emptiness check removed — plus the end of a member
definition, the alias branch, the template-argument form, the file floor, the
include-following coverage, and the self-test's own conversion enumeration. It
plants 17 mutations where it planted 7, and catches all 17 with no survivors.
The self-test grows from 17 expectations to 37.

---

## 2026-08-30 -- A replacement arrives before a column listing leaves

Changing or collapsing the active column path no longer lets the shell's
shared actions, status strip, or filesystem dialogs observe a null listing.
The columns controller removes retired listings from the live model first,
publishes the replacement through `activeListingChanged`, and only then
reclaims the old QObject. The outgoing listing therefore remains cancellable
and short-lived without disappearing underneath bindings that still name it.

The standalone component harness now tears down dependent views and action
registries before the model and controller stand-ins they consume. A status
strip without an action registry also leaves its optional transfer controls
uninstantiated, which gives that supported standalone state an explicit
fallback rather than relying on invisible controls not to evaluate.

A verbose full release run exposed 2,199 TypeError lines before these lifecycle
changes. The same run emits zero afterward. The columns-model, reusable-shell,
and navigation suites pass directly, including a regression that proves a
replacement is published while the outgoing listing is still alive; the full
release and ASan/UBSan gates pass with their declared capability skips.

---

## 2026-08-30 -- Renderer fallback and accessibility sources are separately measured

Window presentation records the requested destination-alpha setting and the
initialized scene-graph renderer separately. The renderer mapping explicitly
classifies every Qt graphics API, and the forced software entry selects
`QT_QUICK_BACKEND=software` before accepting the opaque window path. A mutation
that allowed the software renderer to claim transparency failed both the
renderer mapping and the initialized-window assertion.

Reduced motion removes persistence and its duration while keeping static
material, bloom, and the context marker available. High contrast instead
removes every glow, bloom, scanline, vignette, and persistence source while
lifting text. The composed shell exposes the actual pipeline stages, bounded
glow emitters, and list and grid persistence sources to a hardware-scene test.
Starting from visible Strong-profile sources, that test distinguishes the two
overrides and requires every source to disappear only for high contrast and
`Off`; the launcher turns a post-probe software fallback into a failure. The
OpenGL entry ran for 76.36 seconds and passed, followed by the isolated
accessibility-source audit. The real-compositor entries remain declined by
their declared isolated-surface requirements, so no compositor or 2x result is
claimed.

Accent presets remain authored hue inputs. Before a value becomes a shell token,
the resolver aligns its luminance with the active family and clears the shared
render-site floors. There is no arbitrary custom-accent input, so the previous
selection warning could never reach a user and was removed. A near-black input
is now a registered control: it fails the raw samples and passes only after the
same resolver that supplies the shell token. This keeps contrast repair at the
token boundary instead of presenting an unreachable warning surface.

Large-directory validation now uses a deterministic 2,000-entry fixture and
geometry-derived list and grid bounds instead of a wall-clock threshold. The
list and grid cache mutations were built independently: the list realized all
2,000 entries and failed its 136-row bound, while the grid realized 255 cells
and failed its 140-cell bound. The restored checks passed at both declared
logical scales.

Verified: release build; `core_appearance` and the forced software capability
entry; normal and doubled-scale software visual validation; formatting and QML
checks. The declared RHI accessibility entry reported its unavailable display
server capability, not a passing hardware result.

---

## 2026-08-30 -- Where one entry stops is now written down and gated

The development record separates an entry from the entry below it with a blank
line, a horizontal rule alone on its line, and a blank line before the heading.
That convention was never written down. It had been followed unevenly since the
record started, and nothing checked it, so at this commit 81 of 142 published
boundaries do not meet it and two of the four changes landing alongside this
one arrived with a new entry pushed straight against the entry below it.

A rule that does not exist cannot be gated, so the rule is now in CONTRIBUTING
and governs new entries. It exists because a heading alone does not mark where
an entry stops: read in a diff, in a plain viewer, or scrolled past its
heading, a new entry reads as another section of the entry beneath it, and
every archive move depends on knowing exactly where one entry ends.

Published text is not rewritten to suit a rule written after it, so the 81
boundaries that were published without a separator are recorded by heading in
`tools/devlog_boundary_census.txt` and left exactly as they are. That file is a
census and not an exemption, and the difference is the whole reason it is
shaped this way. An exemption would say only that a listed boundary may lack a
separator. The census says which boundaries lack one, and the archive gate
reads it in both directions: a boundary that does not conform and is not listed
is a new entry breaking the rule, and a boundary that is listed and now carries
a separator is published text that was repaired in place.

The second direction is the one that needed the work. Repairing a published
boundary does not require anyone to decide to rewrite the record - resolving a
collision at the top of the record by replaying an entry and normalising the
whitespace around it produces exactly that edit, it reads as tidying in a diff,
and no other rule here notices: the entry text is untouched, the manifest still
agrees, and history still agrees. Resolving a collision in the record is a
byte-for-byte concatenation of whole entries and never a reformatting of them,
and the gate now fails when it is not.

Entries are keyed by heading alone, because a heading is fixed for the life of
the record while the file holding it changes at every split. A listed entry
that becomes the first entry of an archive part has no entry above it and so
has no boundary to compare; it keeps its census line, and the opening of the
file it now heads is held to the same three lines by a separate rule that no
census line can excuse, since a file's opening is written when the file is
created and that is always after this rule.

The check never opens a record file for writing. A gate that repaired the
record would be performing the same edit it exists to catch.

Two instruments carry floors, because both of them report a clean record and a
dead instrument identically. The scan is held to the record's own entry count,
so a heading pattern that stops matching fails by name instead of examining
nothing and finding nothing wrong. The census is held to a count floor
conditioned on an entry in a closed archive, because removing a line is how a
repair is made to look as though it was always permitted - and repairing a
boundary and deleting its census line in the same change is the one shape of
that edit which every other rule in the gate passes.

Three of the 81 listed boundaries carry a horizontal rule with the wrong
spacing around it. A check asking only whether a rule is present would accept
all three, so the shape is compared exactly and those three are listed like the
rest.

Whether the 81 published boundaries are ever repaired is deliberately not
decided here. They are pinned, not touched.

The guard's self-test grows from 59 scenarios to 72. Both directions of the
census, the file-opening rule, a rule with the wrong spacing, a missing census,
a census naming an entry no file holds, a listed entry that has become the
first of a file, the floor at its boundary, and the branch where the floor does
not apply each have a scenario. Two more damage the guard itself rather than
the fixture, because a scan that examined nothing and a census reader that
counted nothing cannot be produced from a record: each copy is compared against
the original first, so a patch that failed to apply cannot report a pass for a
mutation that never landed. Thirteen further mutations were planted in the
guard and removed again, each one verified to have changed the file; all
thirteen were caught, with no survivors.

---

## 2026-08-30 -- Transfers that report, hold, and stop

Copy and move now reproduce a tree entry by entry and chunk by chunk in
Qt-free `core/` rather than in a single call into the standard library. That is
what makes progress observable at all, and it is deliberately not bought by
driving another program: the operation stays here, unit-testable and free of
anything that has to be installed alongside it.

A transfer reports bytes done, entries done, the entry in hand, and the totals
it measured beforehand. Progress is counted in bytes plus a fixed charge per
entry, measured at 16 KiB from copying 20,000 empty files against one large
one, because a bar counting bytes alone sits at zero through a directory of
small files and then jumps. Rate and time remaining are estimates and are
typed as estimates: each carries its own known flag rather than a sentinel, the
rate is averaged over a two-second sliding window and withheld until the window
covers half a second, and no time remaining is offered without both a rate and
measured totals. A tree too large to measure inside its budget is transferred
with unknown totals, which is reported as unknown rather than filled in.

Reporting is bounded in time, never in work. With the clock held still a
transfer makes exactly the two reports it owes -- one per phase -- whether it
moves fifty entries or two thousand. Measured against the same transfer with
no observer, reporting costs 1.17 times as much on 4,000 entries, and the
engine itself runs at 1.14 times the standard library's copy on 8,000 empty
files and 0.87 times on bulk data. Both figures are held by tests: an exact
count of reports, and a ratio of processor time between two runs of the same
size back to back.

Pause, resume, and cancel share one control, and what each leaves on disk is a
tested guarantee rather than a consequence. A transfer parks between chunks,
holding its descriptors and its working entry. A cancel wakes a parked
transfer rather than queueing behind it, because nobody may be left to resume
one. Cancellation is reported as an ordinary failure and takes the ordinary
recovery: the working entry is discarded, so nothing reaches the destination
and the source is untouched. A source removed while a transfer is parked, and
a destination that stops accepting writes while it is parked, both end the
same way. The one step that is deliberately past the last checkpoint is the
removal of the source in a crossing move, because stopping there would leave
the entry in two places and call it cancelled.

The journal is the other half of that. A transfer that did not complete
installs nothing, so nothing is recorded: the history holds completed
operations only, and there is no state in which a reversal would undo part of
one. A tree holding a socket, a device node, or a named pipe is refused rather
than partly copied, so a copy is never reported as done while arriving
incomplete.

Hold, resume, and stop are declared once in the shared action registry, so the
same three definitions reach `Ctrl+Shift+H`, `Ctrl+Shift+R`, `Ctrl+Shift+X`,
the canvas menu, the command palette, and three buttons in the status strip
beside a progress bar that reads indeterminate when the totals are unknown.

Verified: 19 headless core cases covering the unit, the cadence, the
estimates, cancellation at three points, pause and the three states reachable
from it, the journal, and the cost. Release and ASan/UBSan batteries pass
through the reconciling runner; formatting, QML formatting and linting, scoped
static analysis, and the public-repository guard are green. Known gap: a
same-filesystem move is a rename and reports once, because there are no bytes
to report on.

---

## 2026-08-30 -- A cost bound that measured the machine

The large-directory gate held five bounds on what acquiring a listing may
cost. Two of them were ratios of clock readings: how long the load took
against how long the same process needs to build the keys it reported
building, and how the elapsed load grew when the directory doubled. The
second failed under a parallel battery at a growth of 5.63 against a ceiling
of 2.80, while the key count in the same run read 2.03. The scan had behaved;
only the wall clock had moved.

Rather than widen the ceiling, the quantity was measured. Both directory sizes
were loaded 1,092 times, 546 at each size, in the release and instrumented
builds and at four levels of company: alone, beside three other copies of the
case, beside thirty-one of them on a thirty-two-way machine, and beside five
in the instrumented build.

- The key count read 60,768 at 4,000 entries and 123,456 at 8,000 in every one
  of those loads. The same two figures, 546 times each, in a build that is
  twenty-two times slower as well as in the fast one.
- Wall-clock growth read 1.90 to 2.26 alone and beside three, and 1.40 to 4.05
  beside thirty-one.
- Processor-time growth, which discounts the intervals the machine spent
  elsewhere, read 1.67 to 3.60 beside thirty-one: better than the wall clock
  and still past the ceiling.

A shared machine becomes superlinear in directory size on its own, because the
larger load has the larger working set and pays more of the contention. A
growth ratio of any clock therefore mixes the model's exponent with the
machine's and cannot say which one moved. Taking the cheapest of several
attempts, which the case did, lowers both readings together and leaves the
ratio between them where it was.

Both clock ratios are gone, and the three attempts per size with them. What
remains is exact: the count of key constructions per entry and its growth
across a doubled directory, plus a budget of processor time rather than
elapsed time, so the entry's result no longer depends on how many tests run
beside it. Elapsed time is still reported next to every figure and is used for
one thing, the settle timeout, which sits four times above the budget.

The removed bounds covered something the count does not, and that is closed
where it belongs. Scanned paths are already absolute and normal, so a key
spelled by hand is byte-identical to the counted one and goes uncounted; a
linear rescan restored that way was once measured at 12.9 times the healthy
load with the key count unchanged. `key_construction_guard` held one spelling
of a key inside the functions allowed to build one, and now holds both: an
entry's path converted to text may appear only in the three members that hand
a path to the interface, none of which is a reconciliation site. Planted in
the member where the original was demonstrated, that defect is now rejected by
name, and the cost case still passes it -- which is the measurement that says
the rule, not the clock, is what covers it.

A third gate turns the guard's self-test on itself. `key_construction_mutation_guard`
removes one of the guard's checks at a time and requires the suite to object by
name: seven mutations, seven caught, no survivors, in under three seconds. One
of them deletes a scenario from the self-test, which the new expectation floor
catches; without that floor the other six could all be caught by a suite that
had quietly stopped running most of its cases.

Verified: 6 defects planted in the cost case and its subjects, 5 caught and 1
deliberately not, that one caught by the guard instead. A publishing interval
that stops growing with the listing is caught twice over -- by the per-entry
rate ceiling first, and, with that ceiling lifted to isolate it, by the growth
bound at 3.69. Release and ASan/UBSan batteries pass with the reconciling
runner; formatting, scoped static analysis, and the public-repository guard are
green. Known gap: a comparison written against filesystem paths directly builds
no string and is invisible to both spelling rules and to the counter; what
holds that case is the shape of the update, where a search per delivered entry
is a visible change to the structure rather than a spelling inside it.

---

## 2026-08-30 -- Give the live development record room before the ceiling

The live development record was again approaching its tracked-file line
ceiling, with more headroom needed than the pending entries would leave. Its
oldest consecutive stretch now lives verbatim in
`docs/devlog/2026-08-part5.md`, while the live file retains the newest entries.

Reading order remains the live record followed by archive parts from newest to
oldest. Recombining the live record with the new part reproduces the previous
file byte for byte: the part supplies the separator at the boundary, and no
entry text is altered. The manifest keeps the same sequence for every moved
heading and adds this entry at the front, so the archive comparison covers the
same corpus it did before. History-derived validation continues to associate
each moved heading with the commit that originally published it.

---

## 2026-08-29 -- Seven ground hues extend the Odyssey palette family

The live palette roster grows from seven to fourteen with one additional dark
family per ground-hue group: `odyssey-midnight`, `odyssey-harvest`,
`odyssey-lagoon`, `odyssey-plasma`, `odyssey-borealis`, `odyssey-crimson`, and
`odyssey-fuchsia`. The roster order and every stable identifier are asserted,
so adding, removing, renaming, or reordering a shipped family requires an
explicit test change.

Each new family retains its canonical ground, deep ground, frame, match, focus,
warning, and success values. Secondary ink follows the established foreground
and inactive-ink blend. Application-only wells, panels, primary and faint inks,
file-type inks, metadata, and selection beds are tuned on the surfaces that
paint them; selection beds remain independent application colors rather than a
terminal selection role.

All seven candidates clear the complete render-site matrix. Blue- and
magenta-weighted inactive inks lose too much luminance under the icon emission
cap, so their toolbar roles move toward neutral while retaining the family
hue. The new capped inks stay above the navy family's accepted 2.104:1
pressed-bed margin, leaving that default-state pair as the shipped binding
case. The fuchsia file mark clears its separate 3.0:1 non-text floor, and the
harvest high-contrast danger ink receives the smallest lift that clears its
selected-row text floor. No candidate needed to be removed or recolored beyond
these role-local adjustments.

Verification: release registered 76 entries; 72 executed and passed, with four
declared GPU or compositor capability entries skipped. ASan/UBSan registered
the same 76, disabled static analysis by preset policy, and passed all 71 that
executed, with the same four capability skips. Static analysis passed in the
release preset with no diagnostics. The application smoke passed in both
presets.

---

## 2026-08-29 -- A gate that has finished is not one that cannot be identified

The isolated-compositor harness records every child it starts by pid and start
time, so teardown and the later reaper can tell that number apart from a reuse
of it. The start time is read straight after the child is started, and an empty
reading meant one thing: refuse the run. That is right for a child still
running and wrong for one that has already finished. A gate that exits in a few
milliseconds can be reaped by the harness's own shell before the reading
happens, which removes its process record; the compositor had come up, the gate
had run, and the status it exited with was still recoverable, yet the run was
reported as a failure.

Measured beside a four-way parallel run of the project's own tests: 2 refusals
in 800 runs of a fast-exiting gate, and 5 in 400 under the sanitizer build,
against none in 60 runs on an idle machine. The harness's answer depended on
what else the machine was doing, which is the one property a gate must not
have, because it teaches every reader of a summary to re-run rather than to
look.

An unreadable start time now settles into three answers rather than two. Read
and recorded, as before. Alive and unidentifiable, which still refuses, still
stops the child, and still leaves nothing behind. Or already finished, which
records nothing, because there is no process left to stop, and reports the
status the gate exited with. Liveness is asked of the kernel rather than of the
process-record source, so a source that has stopped answering cannot make a
running child look finished. The wait bounds a settle and never a comparison: a
child on its way out is briefly both signalable and unreadable, and answering on
the first reading would put that window back into the result.

Both answers are covered by one self-test scenario, and it does not wait for a
race to happen. The compositor is recorded before its socket exists, so the
scenario holds the socket back, replaces the process-record source in between
with one that answers for the compositor alone, and only then releases it. The
gate is the single thing the harness cannot read. A gate that exits immediately
must be a completed run; a gate that keeps running must be refused by name,
stopped, and leave no run directory. Six deletions of the new distinction were
planted one at a time and diffed to prove each landed: all six fail the suite,
including a faithful revert to the single reading.

Verified: the self-test at 21 scenarios, 19.4s; 800 runs of a fast-exiting gate
inside a parallel run of the full tests with no refusal, against 2 in the same
800 before the change; release and sanitizer builds warning-clean with the
tests green. The suite now states in its own header that its result does not
depend on how many tests run beside it, and records the margin of the one
wall-clock budget a passing run still has: the stub advertises its socket in
0.10s against a budget of 10s, measured both serially and inside a four-way
parallel run.

---

## 2026-08-29 -- Identity and palette claims match their measured contracts

The symbolic application icon is now tested for the property its desktop role
requires: its stroke follows consumer-supplied color. The raster gate injects
red and green at the SVG root and requires the rendered ink to follow both. A
literal-grey mutation must reject both consumers, so a neutral but fixed-color
asset cannot pass. A source-level assertion also binds the desktop and symbolic
SVG paths to the in-application vector path.

The mark's accent traversal is explicitly a controller-routing invariant:
accent selection cannot reach semantic icon ink. Separate assertions prove the
two intended theme routes, with palette selection changing the mark and high
contrast promoting it to primary text ink.

The icon-ink cap is constrained by the default state, not high contrast. The
navy family's pressed-control ratio is independently computed at 2.107:1 and
measures 2.104:1 through the runtime color path, making it the shipped set's
tightest margin above the 2.0 floor. High contrast replaces the capped role
with primary text ink. The semantic matrix now traverses all five accent
presets in both contrast states and samples panel-strip composition from zero
through full surface opacity in 0.2 steps.

Release registered 76 entries; 72 executed and passed, with four declared GPU
or compositor capability entries skipped. ASan/UBSan registered the same 76,
disabled static analysis by preset policy, and passed all 71 that executed,
with the same four capability skips. Static analysis passed in the release
preset with no diagnostics. The application smoke passed in both presets. The
identity scene rendered at monitor scale 1x; the scaled-layout entry asserted a
declared device-pixel ratio of 2 while explicitly declining empty child-item
grabs, and the raster gate exercised the shipped SVGs at exact 1x and 2x device
sizes.

---

## 2026-08-29 -- Chrome bounds and window alpha share explicit contracts

Toolbar and path compaction now derive their switch points from independent,
always-labelled measurement rows. The measurements use the rendered controls,
spacing, margins, and stretch behavior, so the labelled form fits at the
reported width and compacts exactly one pixel below it. Each chrome strip
reserves an interior perimeter around its row. Component and shell-scene gates
now reject a control that crosses its strip, neighboring chrome bands that
crowd each other, and a label that exceeds its own container.

The application requests a destination alpha channel before it creates a
window, then enables a transparent window ground only after both the negotiated
format and the initialized scene-graph renderer report support. Missing alpha,
unknown or software rendering, the `Off` profile, and high contrast retain an
opaque ground; reduced motion retains the still material. Chrome and directory
grounds remain opaque contrast beds. The former surface-opacity control is now
an opaque surface blend with a 0.45 lower bound, including for persisted older
values, so it changes visible chrome color without placing readable text over
an unknown desktop background.

Verification covers the labelled and compact strip boundaries across densities,
the full-shell strip geometry at supported sizes, and the alpha-enabled and
opaque fallback states. Deliberately undersizing the toolbar measurement made
the appearance control breach its right boundary; deliberately disabling the
window-transparency binding made the capability test fail. The renderer and
alpha negotiation remain runtime conditions, so a full visual acceptance pass
on a declared compositor is still required for hardware-specific behavior.

---

## 2026-08-29 -- Undo is one action across every shell surface

The shell now declares Undo once. `Ctrl+Z`, the toolbar, the blank-canvas
context menu, and the command palette all invoke that declaration against the
active listing adapter. The same declaration reads the adapter's live
availability, so an operation in progress and a journal state that does not
offer undo both disable every surface together.

Disabled presentation keeps the adapter's current reason. Toolbar hover text,
the context-menu accessibility description, and the command-palette reason
therefore describe the same action state rather than each surface inventing
its own condition.

Verification adds an adapter scenario for a copied entry that is removed by
Undo and for an overwrite state that disables it with the recorded reason.
Action, palette, component, and full-shell input cases cover the shared
declaration, disabled state, canvas-menu pointer route, toolbar pointer route,
palette route, and `Ctrl+Z` route.

---

## 2026-08-29 -- What protects a copied file cannot depend on how deep it sits

Reversing a copy removes what the copy created, so every check standing between
a reversal and a file is the difference between an undo and a deletion. Two of
those checks were wrong, and a third class of them was untestable.

A file inside a copied tree was protected more weakly than the copy's own root.
The root was recorded with its identity, its modification time and its size; an
entry inside the tree was recorded without a size, and the comparison that
decides whether a tree is unchanged was built from the recorded fields, so the
size was neither stored nor compared. The consequence is destructive rather
than cosmetic. A file rewritten to a different length, with its modification
time falling in the same whole second as the copy, is refused at the root and
was deleted one level down -- the same edit, the same timing, only the position
differing. Entries inside a tree now carry a size and the comparison reads it.

A second check barred something it was never meant to describe. A move between
filesystems copies the data and removes the original, so a file that had more
than one name arrives as a new one and the reversal is refused. The condition
was read from the entry's link count, which for a directory does not count
names at all: the kernel refuses a user hard link to a directory, and the number
reports how many subdirectories it holds. Filesystems disagree even on that,
some counting the children and some reporting one regardless -- so on one
filesystem every crossing directory move was barred with a recorded reason that
was not the reason, and on another none was, which is why the machine this is
developed on could not see it. The barrier now applies to files, and the
directory case is covered by a scenario that records what the filesystem it ran
on actually reports rather than assuming a convention.

The third problem was that neither of those could have been caught by the tests
as they stood. The suite shows that a reversal refuses; it cannot show which
check produced the refusal, and a check that stops being read leaves every
scenario green. So the checks are now deleted one at a time by an automated
gate that requires the suite to fail for each -- twenty-one of them, in about
eighteen seconds, compiled in a scratch directory so nothing tracked is touched
and a parallel run cannot see it. The gate holds itself to the same standard it
imposes: a mutation that does not change the source, or that fails to compile,
is reported as having measured nothing rather than counted either way, and a
floor fails the gate if it measures fewer checks than it lists.

The two checks with no reachable failing state are declared to that gate rather
than left out of it, and are required to survive. If one ever starts being
caught the gate fails, because the declaration is then stale and the record
describing it is wrong. That replaces a claim in the previous entry, which said
a battery re-measured them while the battery itself lived outside the
repository and could not be run by anyone reading it.

Verification. The gate found a second instance of the first defect while being
written: with the size added, the modification time of entries inside a tree
could still be dropped with the suite green, because the new scenario moved
both fields at once. It is now two scenarios, one moving the size alone and one
moving the time alone. Both fixes were confirmed to discriminate by reverting
each in turn and watching the suite fail by name. Thirty-six scenarios, with
the table floor and the declining preconditions unchanged.

Known gaps. A rewrite that changes neither the modification time nor the size
is still invisible, and times are still compared in whole seconds. Identity is
only as distinct as the filesystem makes it: where no creation time is
reported it degrades to a device and inode pair, which can be reissued once the
original is gone, and a reversal matching a recycled pair would remove an entry
the operation never created. That is narrow, since the time and size must match
too, but its consequence is destructive and it is now recorded where the
destruction lives rather than only where identity is defined.

---

## 2026-08-29 -- The shell palette family carries the flagship terminal ground

The Odyssey palette family is shared across the suite, and two of its members
were not represented faithfully here.

`odyssey-default` had drifted from the family definition on four roles while
the rest of the family was carried across exactly. The window ground, the
frame, the search bed and the inactive icon ink now match the family values.
The search bed is the visible one: it had become a warm brown where the family
defines a green, so a match highlight read as a different colour from every
other surface in the same theme. Measured readability improves rather than
holding: primary text against the ground moves from 11.93:1 to 12.46:1.

`odyssey` itself was absent. It is the family's navy ground with cool neutral
text. The derivation is the established one: ground, deep, frame, search,
cursor and status roles carry across directly, secondary ink is the measured
blend of the family foreground toward its inactive tone, and the selection
bed is chosen for this application instead of reusing a terminal role.

One role needed a value the family does not state. Icon ink is capped to a
maximum channel so toolbar symbols stay below the bright-pass threshold, and
a blue-dominant ink loses too much luminance under that cap to clear the
pressed-control floor in high contrast. A less saturated ink of the same hue
clears it at 2.12:1 against a floor of 2.0. The cap is what the ink has to
survive, so the ink is chosen in its capped form.

The palette roster assertion moves from six to seven deliberately. It exists
so a family member cannot be added or lost without a decision being recorded,
and this is that decision.

---

## 2026-08-29 -- One ring and one wave carry the application identity

OdySea now has a compact application mark: an O-shaped ring crossed by one
horizon wave. The toolbar renders it through the shared `VectorIcon` component
and the semantic icon ink, so the mark gains the existing high-contrast stroke
lift, follows every fallback profile, and stays independent of the selectable
accent. It is orientation rather than an action, so the window title remains
its accessible name and the mark adds no inert focus stop.

The desktop form uses the default family's window-ground and primary-text
roles. A matching symbolic SVG renders the same geometry as one neutral stroke
for monochrome and high-contrast icon themes. Both scalable assets install into
the desktop icon theme; the installed theme remains authoritative, with the
bundled desktop asset as the application window's fallback. No raster fallback
or second in-application drawing path was added.

The rendered-shell gate exercises 16, 20, 24, 32, and 48 logical-pixel marks,
all effect profiles, every accent preset, high contrast, and toolbar bounds.
Its ordinary pass records a real 1x child-item render. The software scaled pass
proves that Qt applied the declared 2x monitor scale and checks logical geometry
and requested SVG source size, but explicitly declines child-item pixel grabs
because that backend returns them empty at 2x. A separate non-vacuous raster
gate renders both shipped SVGs at the exact 1x and 2x device sizes and asserts
the symbolic output uses one neutral ink.

Release registered 75 entries and passed all 71 that executed. ASan/UBSan
registered the same 75, disabled static analysis by preset policy, and passed
all 70 that executed. Both presets declined the four declared rendering-
capability entries: the presentation and visual-validation RHI launchers and
their real-compositor counterparts. The offscreen software smoke launch passed
in both presets.

---

## 2026-08-29 -- All-or-nothing was all-but-one, and vocabulary meant file contents

The enumeration rule stands a candidate down when every name in it is already
part of this project's own vocabulary. It was checking every name but one.

The names reach the check in reverse order and arrive without a trailing
newline, and the loop reading them stopped as soon as the final read reported
end of input. The last name read is the run's first name, so the first name of
every enumeration went unexamined. The consequence is not theoretical: a run
whose single unrecognised name sat in that position stood down, so a survey of
four names this program implements plus one it does not passed the guard as
long as the foreign name was written first. Moving the same name one position
to the right reported it. The loop now accepts the final unterminated read, and
a fixture places the foreign name exactly where it used to be lost. A second
fixture holds the same shape with every name recognised, so the first is known
to report the name rather than the position.

Vocabulary now means what the code says rather than what its files contain.
The rule's own argument is that a survey cannot establish vocabulary because
vocabulary is established by the code that implements a word, and documentation
was excluded on exactly that basis. Comments and string literals were not, so
the same sentence moved from a paragraph into a comment above a function
established vocabulary after all. Comments and prose literals are now removed
before a name is looked up, which takes this repository from 1,710 distinct
capitalised words to 976. A quoted literal is kept when it holds no whitespace,
because a shortcut or a role name is a token the program uses, and dropped when
it holds whitespace, because a sentence in a string is prose in the only sense
that matters. The removal can only ever delete text, so a name it wrongly
discards is reported rather than excused; it cannot invent vocabulary.

The vocabulary is now read once into a single index rather than re-scanned for
every name, which is also what the rule always should have done: a candidate
carries a handful of names and the corpus carries hundreds of sources.

Verification. The self-test grows from 117 scenarios to 129, and the count
floor caught an off-by-one in that arithmetic while the scenarios were being
added. Nine new corpus scenarios pin the first-name position, the case, whole
word and whole-corpus properties of the lookup, and each direction of the
comment, block-comment, prose-literal and token-literal strip. Two patched-guard
scenarios pin the scan-integrity branches: a scan that examined nothing over a
non-empty corpus, and a scan that failed to run. The corpus-size read needs no
fixture of its own, because the first of those two has a non-empty corpus and an
examined count of zero and would stop reporting if the size stopped being read.

A mutation battery plants sixteen defects one at a time and requires the suite
to fail for each: fourteen are caught and two are declared equivalent. The
literal-match flag is one of them, and the reason is measured rather than
asserted -- grep reads basic expressions by default, and no character a name may
contain is a metacharacter in them, so removing the flag changes no lookup. That
argument is a precondition rather than a proof, so the precondition is enforced:
a scenario reads the name grammar out of the guard and fails if it ever admits a
metacharacter, which is the moment the flag stops being free insurance. The
other is a branch guarding a candidate with no names, which cannot occur while
both thresholds require two capitalised items and every capitalised item
contributes a name.

Known gaps. Only capitalised items are collected, which is consistent with every
threshold, so no candidate is currently judged on a subset of its names. It is
recorded at the collection site as the seam a future relaxation would open: a
threshold that starts counting lowercase items has to start collecting them too,
or the all-or-nothing condition silently becomes all-but-some again. A survey
laid out inside a source comment remains outside the table rule, which reads
prose files only.

---

## 2026-08-29 -- Context glow is one bounded phosphor source

The active tab, focused directory pane, and current selected entry now add a
transparent accent emitter to the existing bright-pass and dual-blur pipeline.
The normal tab, pane, and current-item outlines remain in their own surfaces,
so keyboard and pointer orientation stays crisp when no emitter is available.
No additional blur, shadow, or animation path was added.

The reusable context frame treats active-tab, focus, and selection as one
boolean source, not three additive gains. A current selected item inside a
focused pane therefore cannot become brighter by satisfying more than one
state, and selection supplies that source only at the current entry. Adding
more selected entries changes neither its intensity nor its presentation cost.

The emitter uses the resolved accent token already measured at its rendering
surfaces. Profiles with no bloom, high contrast, shader fallback, and software
rendering omit the emitter rather than dimming it; the semantic outline stays
available. The context source has no transition of its own, while the existing
current-item ring remains governed by the reduced-motion duration.

Component and rendered-shell tests assert the one-source composition, active
tab routing, current-selection bound, effects-off absence, software fallback,
and the existing emission capability boundary.

---

## 2026-08-29 -- Scaled-layout validation proves the scale it claims

The software scaled-layout entry now declares the scale it expects alongside
`QT_SCALE_FACTOR=2`. Before any layout result is credited, the device-scaling
suite asserts that Qt reports that exact ratio on the software backend. This is
an assertion about the logical-scale condition, not the grabbed frame: the
offscreen surface continues to rasterize at logical resolution and contributes
no doubled-device-pixel evidence.

The discriminator was measured in both directions. With the factor present,
the entry passed at a reported ratio of 2. Removing only `QT_SCALE_FACTOR=2`
made `DevicePixelScaling::initTestCase` fail with an actual ratio of 1 against
the declared 2; restoring it passed the same entry. The ordinary 1x entry stays
undeclared and unchanged.

The device-scaling filter remains complete, so its logical well-mask geometry
function still executes on the RHI entry, but it is not counted as GPU
evidence: it performs no frame grab and measures only rectangle equality. The
four frame-dependent functions skip by design inside each passing software
entry and are reported as such. High contrast remains an independently tested
axis rather than a cross-product with layout, focus, effects-off, or large-
directory virtualization.

The refresh ran from current main on the authorized `DISPLAY=:0` path. The RHI
surfaces reported device-pixel ratio 1.0; no compositor was started and no test
window was activated in the login session. Release registered 73 entries and
passed all 71 that executed. ASan/UBSan registered the same 73, disabled static
analysis by preset policy, and passed all 70 that executed. Both batteries
declined only the two declared real-compositor entries. Genuine 2x framebuffer
rendering, fractional frame-ratio divergence, independent per-axis rounding,
the strict software-fallback failure branches, and the presentation suite's
real-compositor frame sentinel remain explicitly unmeasured.

---

## 2026-08-29 -- Completed operations carry the terms of their own reversal

Copy, move, rename, and delete-to-trash are now recorded in a bounded journal
in the Qt-free core, and the newest record can be reversed. The journal
performs the operations rather than being told about them afterwards, which is
what lets it see a destination before an operation runs and the result
immediately after. That difference separates an entry a copy created from one
that was already there, and reversing a copy removes only the first.

Whether an operation can be reversed is settled when it is recorded, and the
reason travels in the record instead of surfacing as a refusal later. An
operation that replaced an existing entry discarded what it replaced, so the
whole reversal is refused rather than restoring half of a state that never
existed. An operation that resolved to the entry it was given changed nothing,
and treating its result as something the operation created would delete the
only copy. A copied tree larger than the journal will remember cannot be
confirmed unchanged afterwards. A move between filesystems copies the data and
removes the original, so an entry that had more than one name arrives as a new
one that nothing rejoins to the names that kept the old data.

A reversal that cannot be certain does nothing. The result must still be the
entry the operation produced, compared by identity rather than by path, because
a path can be reoccupied by something that looks exactly like what left it.
Removing what a copy created is held to a stricter test because it destroys:
every entry the copy made must still carry the identity and the modification
time it had when the copy finished. Every step refuses a name that is already
taken rather than displacing what holds it, so a reversal can fail but cannot
overwrite. A reversal across two directories is two relocations, and one that
completes the first and fails the second reports the failure, keeps its record,
and points that record at where the entry actually is, so a second attempt
starts from there rather than from a path that no longer holds anything.

Verification. Thirty-three headless scenarios exercise recording, the bounded
history, every barrier, every refusal, and the partial reversal in turn. The
suite fails by name when it runs fewer scenarios than its table holds, and a
scenario that cannot run on a given machine prints the precondition it needed
instead of disappearing. A mutation battery plants twenty-nine defects one at a
time, requires each to change the file before it is built, and requires the
suite to fail for each: twenty-six are caught, one is equivalent because the
bound on the tree scan and the comparison that follows it both refuse a grown
tree and the bound decides only which of them reports it, and two guard states
no scenario can produce and are declared below rather than counted as caught.
One instrument was removed rather than left standing after the battery showed
it could not change an outcome — an explicit check that the origin was free
duplicated what every relocation step already refuses on its own.

Known gaps. There is no keyboard or pointer surface for undo yet; this is the
core contract, and the shell surface follows it. A rewrite that leaves the
modification time undisturbed is invisible to the copy check, and modification
times are compared in whole seconds, so a rewrite within the same second as the
copy is not detected. Two guards have no automated coverage, and the battery
measures that rather than the record asserting it: a result that cannot be
examined at all is recorded as unreversible, and so is an operation that did
not reach the destination it was expected to reach. Both need the filesystem to
change underneath a running operation, or a result that a just-completed
operation cannot examine, and neither can be produced from a test. They are
kept because without them an operation that quietly replaced something could be
offered as reversible, which is the one outcome the design exists to prevent.

---

## 2026-08-29 -- Accent coverage makes the accepted matrix explicit

An accent preset's model color is an authored hue input, while the active
family resolves the displayed accent at the surfaces the shell renders. The
resolved color retains the preset's hue direction, applies profile and
accessibility lift first, then moves only as far as the shared render-site
measurement requires to clear the indicator floor. The active accent is the
single value for chrome, focus, selection, emission, previews, and any future
swatch; raw model color is not a displayed-token contract.

File-type and status colors remain a controller-routing invariant. The test
dynamically enumerates every current preset across every family, effect
profile, and high-contrast state while pinning generic-file, directory,
symlink, metadata, match-bed, icon, selection-ink, error, warning, and
success roles. The shipped roster test remains deliberately exact: a new
choice must update its identifier, display name, and acceptance coverage in
one reviewable change.

Every shipped accent now clears the same five render sites used by the
appearance warning — window ground, selected entry, hovered surface, pressed
surface, and panel — across every shipped family, effect profile, and
high-contrast state. The warning, resolver, and acceptance test share those
sites and their measurement; they cannot drift into separate contrast rules.

Release and ASan/UBSan suites passed. The appearance input test rendered on
the software scene graph at logical scales 1x and 2x. No monitor scale was
measured, and the declared RHI and compositor entries were not started.

---

## 2026-08-29 -- Entry outlines carry one semantic type contract

Directory, file, and symbolic-link glyphs now pass through one entry-icon
component. The component selects both its code-native outline geometry and its
ink from entry metadata, using the existing directory, neutral-file, and link
roles. List and grid delegates no longer repeat that mapping, so a view cannot
quietly assign a different color meaning to the same entry type. No palette
role or second file-type vocabulary was introduced.

The normal outline is thinner than the general chrome stroke, while high
contrast deliberately strengthens it. Effects-off and software paths retain
the same geometry and semantic ink. Rendered tests exercise every entry kind,
prove the outline stays open at one- and two-times logical size, keep the
geometry stable under high contrast and effects-off, and verify the consumer
ink remains fixed while every live accent preset changes. The shell tests also
assert that both list and grid delegates expose the centralized semantic name
and ink.

Release and ASan/UBSan builds are warning-clean. Release registered 73 entries
and passed all 71 that executed. ASan/UBSan registered the same 73, disabled
static analysis by preset policy, and passed all 70 that executed. Both
batteries declined only the two declared real-compositor entries. The 1x RHI
records were exact: a 640x480 frame for a 640x480 logical surface and a 320x240
frame for a 320x240 logical surface, both at device-pixel ratio 1.0. Those
passing entries declared three narrower paths unexercised: fractional
frame-ratio divergence, independent per-axis rounding, and the function that
requires real-compositor GPU frames. The software scaled-layout entry remains
logical-scale evidence only. The isolated real-compositor 2x entry is still
declined by its declared session-safety boundary, so genuine
doubled-device-pixel rasterization remains unmeasured.

---

## 2026-08-29 -- A table walked past every category rule, and shape was never the discriminator

Five rules read the shape of prose: a survey sentence, a placement verb, a
section heading, a group label, and a run of names qualified by a limitation. A
Markdown table walks past all five at once. The label rule needs a list marker
that a table row does not have, and a run has to close its clause, while a run
inside a cell is closed by a cell wall instead. A column of names beside a
column of assessments therefore passed cleanly, and it is the shortest form the
claim has — the form left over once prose is refused.

A table is now read as its own block. Its cells are the items, a limitation
anywhere in it qualifies it, and it ends where the rows do. Two details keep
ordinary documentation out of the report. The header row and its delimiter are
not counted, because a header names the columns rather than the subject. The
item-count half of the prose threshold is not applied either, because the
number of cells in a table is decided by how many columns it has rather than by
what was written, so only the count of name-shaped cells carries a signal
there.

A cell is a name, or a list of names, or prose, and only the first two count.
Reading names out of the middle of prose counts the first word of every
assessment written beside a name, since an assessment starts a sentence and a
sentence starts with a capital — which turned a two-name table into a
three-name report on the first attempt. A cell contributes its comma-separated
parts only when every one of them is name-shaped.

The harder half was that the enumeration rule reported this project's own
writing. The design document's bullet on the effect profiles names five of
them, closes the list with a full stop, and then says what happens on a weak
GPU. That is the same shape as a survey, and it is correct prose written here
constantly. It survived only by accident: the accent preset bullet wraps
between its last comma and its final name, and the effect profile names carry
backticks that put them outside the name pattern. Reflow either paragraph and
the guard reports the design document.

Shape was never the discriminator. What separates the two is the names. The
profile names are identifiers this program implements and they appear
throughout its sources, while a name from elsewhere appears in exactly one
place: the sentence that mentions it. So a run stands down only when every name
in it is already part of this project's vocabulary, and vocabulary means the
product sources — C and C++, QML, build and resource definitions. Documentation
deliberately does not count, because if it did a survey would authorise itself
by naming a group in a table and mentioning it once in a paragraph. The
vendored dependency tree does not count either, since an upstream's identifiers
are its vocabulary rather than this one's. The condition is all or nothing, so
a survey cannot be smuggled in beside four words that happen to be settings.

Measured against the tracked corpus with its sources present: the design
document's effect-profile and accent-preset bullets, reflowed onto single
lines, are accepted; the same bullet with one foreign name substituted is
reported at its line number; and the comparative section removed earlier,
restored into the corpus for the measurement, is still reported by the
enumeration rule itself rather than only by the rules that read framing.

The two shape rules, and only these two, now read a narrower corpus. They match
on form rather than word choice, so they can land on text this project has no
authority to rewrite: the license, reproduced verbatim as its own terms
require; the vendored tree, where an upstream license requires verbatim
distribution and editing it to satisfy a check here would breach it; and the
archived record, where a separate gate refuses any in-place modification of a
published entry, so a hit there is a contradiction between two hard gates with
no move between them. The boundary is authority, not convenience. The live
record and every documentation file are still scanned, which puts the seam in
the right place: an entry is held to these rules while it can still be
reworded, and stops being scanned only once it has been archived. The cost is
stated rather than hidden — a survey that survives review into the archive is
beyond these two rules afterwards — and it is bounded, because every other rule
in the guard still reads the archive.

Those exclusions currently excuse nothing. Dropping all three and re-running
reports the same clean corpus, and so does lowering the threshold a notch, so
they are preventive rather than remedial. An exclusion that excuses nothing
cannot be shown to be narrow by the corpus, so it is held to its width by
scenarios instead: the identical text one directory outside each scope is
reported, and the same text in the live record is reported while the archived
copy is not.

Narrowing the corpus introduced a state that could not exist before. A tree
holding nothing but a license, or nothing but archived entries, leaves these
two rules with no line to judge, and the floor that fails when nothing was
examined then fires on a corpus that is legitimately empty. The size of the
narrowed corpus is now measured first and is what decides which of the two
conditions holds, because reading both off one number makes a broken scan
indistinguishable from a license file on its own.

Two apparatus faults were closed alongside. The hosted-source search ended in a
tolerated failure eleven lines below the comment warning against exactly that,
so a pattern that will not compile looked identical to a clean corpus; its
status is now read, with an empty result accepted and a failure to search
reported. That scan and the own-owner filter are both pinned by scenarios that
damage a copy of the guard deliberately, compare the copy against the original
first so a patch that failed to apply cannot read as a pass, and assert the
exit status and the reason.

One fixture was a decoration rather than a scenario. It was named for the
blank-line block boundary, but its limitation sat past a blank line and past
the start of another list item at once, so the list-item boundary alone held it
accepted and the blank-line clause could be deleted with the suite green. One
character was the difference. The criterion that missed it — whether the
fixture carries a list the rule matches — is necessary and not sufficient. What
a fixture has to prove is that the condition it is named for is the one
deciding its outcome, and the fixtures added here were built to that criterion.

Self-test 95 to 117 scenarios with the floor raised. Thirty-five mutations were
planted, each one diffed against the file it edits so an edit that failed to
apply reads as unmeasured rather than as a survivor; thirty-three are caught by
name. One had to be rewritten: reverting the hosted-source scan by breaking its
quoting left the guard unparseable and failed every scenario at once, which
measures nothing about whether a status is read, so it was replaced by a
faithful revert to the tolerated failure and is caught by that one scenario
alone.

Two survived, and both are disclosed rather than argued away. Counting the
delimiter row of a table changes nothing, because a delimiter cell holds only
dashes, colons and spaces and can never be name-shaped. Treating a candidate
that carries no names at all as recognised vocabulary changes nothing either,
because every threshold that produces a candidate already requires two
capitalised names. Both are defaults that no corpus can reach, and both stay:
removing the second would turn an impossible state from a report into a silent
pardon, which is the wrong direction for a default to fail in.

The strongest accepting fixture caught the contributor guide while this was
being written. It copies the real tracked files that cite upstream dependencies
into a repository holding nothing else, so those files are judged with no
sources present and no vocabulary to belong to. A profile list quoted verbatim
into the guide was reported there — correctly, and it is a constraint worth
stating: tracked prose in those files must stand on its own shape rather than
lean on the vocabulary stand-down, because a reader of one of them has no
sources in front of them either.

The stated limits are unchanged and worth restating. The table scan reads the
two extensions tracked prose is written in, so a survey laid out inside a
source comment is outside it; the alternative reads a continued shell pipeline
as a table row, and two tracked lines are exactly that. The vocabulary lookup
is a whole-word search of the sources, so a name that happens to be a word this
program already uses is excused, which is why the rule is a floor under review
rather than a replacement for it. And a run of lowercase names is still not
refused, for the reason recorded before: loosening the item shape to reach it
reports 484 lines across 105 files.

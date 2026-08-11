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

- [2026-08 part 3](docs/devlog/2026-08-part3.md)
- [2026-08 part 2](docs/devlog/2026-08-part2.md)
- [2026-08 part 1](docs/devlog/2026-08-part1.md)
- [2026-07](docs/devlog/2026-07.md)

---

## 2026-08-10 -- The session interlock names the compositor it authorises

The interlock added with the compositor gate's refusal was a presence check on
a single environment variable. Presence is not a declaration. An empty value
passed it, and so did the string `false`, which meant a harness that failed to
create the socket it meant to name would export exactly the marker that
authorised the run it had not prepared.

Worse, the marker authorised a run without constraining what that run targeted.
The gate went on to read the ambient environment for a display, and its second
choice was X11. A declaration set alongside an inherited `DISPLAY` therefore
resolved to an X11 session the harness never created and would not tear down.
Measured rather than reasoned about: with a declaration present and a
nonexistent X display, the gate passed the interlock and reached the OpenGL
probe, failing only because that display does not exist. A working one leads to
the exec.

The declaration now carries the Wayland socket, so it says what it authorises
instead of merely that something is authorised. An empty value is refused.
`WAYLAND_DISPLAY` must equal the named socket, so an authorisation for one
session cannot be spent on another. The X11 path is gone rather than
deprioritised, because the declaration cannot name an X display and any such
fallback could only reach a session the harness did not create; `DISPLAY` is
removed from the environment before the suite is exec'd.

The suite binary enforces the same rule itself. Where a suite renders was
decided entirely by CMake test properties and by the launcher, and neither is
part of the binary, so running it directly -- the first thing anyone does when
investigating a compositor failure -- reached the ambient session with nothing
in the way, and with no platform pinned Qt falls back to it unprompted. The
scenes call `requestActivate()`. The shared runner for the GPU-path suites now
permits only a non-rendering platform or a declared isolated compositor, and
exits 77 with both permitted forms named so the next step a reader takes is a
safe one. A policy enforced for the registered entries and documented for
humans is the shape of the practice this exists to end.

Verified by discrimination on each closed hole: empty declaration, the literal
`false`, a declaration whose socket disagrees with `WAYLAND_DISPLAY`, and a
declaration alongside an inherited `DISPLAY` all refuse, the last one refusing
under the required-mode override too. The distinction the refusal was built to
preserve still holds -- a matching declaration pointed at a socket with nothing
behind it reports an inability, not a refusal, and still exits 1 under that
override. Direct runs of the suite binary against an ambient display refuse,
while the offscreen form every registered entry uses is unaffected: release
59/59 with the two compositor entries declining, and the offscreen GPU gates
still running at full duration, 76.27 s and 25.54 s.

---

## 2026-08-10 -- The compositor gate will not run against a session it does not own

The real-compositor validation gate rendered onto whatever display server was
advertised in its environment, and the suite it launches asks its window to be
activated. Running the test suite inside a session someone was using could
therefore move that session's input focus onto a test surface, and if the run
ended without restoring it, the focus stayed moved. Where the surface was not
visible, the result was a session that ignored keyboard and pointer input while
the compositor, the kernel, and the drivers were all working normally. That
outcome was reached, and recovering from it required restarting the session.

The gate now declines any compositor that was not declared as started for the
run. `ODYSEA_ISOLATED_COMPOSITOR` is that declaration, and the check runs before
the environment is read for a display, before the OpenGL probe is forked, and
before any window can exist, so declining touches nothing at all. A test that
can take over an input device is opt-in against a disposable compositor, not
opt-out against whatever happens to be there.

`ODYSEA_REQUIRE_COMPOSITOR` does not override the refusal, which is the part
worth stating precisely, because that override exists to stop a skip reading as
a pass. An inability to run still turns red under it: with the interlock
satisfied and no display advertised, the gate reports FAIL and exits 1 exactly
as before. A refusal exits 77 whether or not the override is set. The two are
different states with different exit texts, because a gate that punished the
refusal would only teach the next reader to delete the refusal.

What this costs is stated rather than absorbed: no harness starts an isolated
compositor yet, so the entry declines everywhere and the real-compositor path
is unmeasured. Its earlier results are not a baseline either. That gate was
observed passing five of five runs on one day and failing four of five on
another with nothing changed between them, because its outcome depended on
which output the window reached and on the set of outputs being changed around
it. Results gathered against an unowned session are treated as uninformative in
both directions rather than retained as history.

Verified by exercising all four exit paths. No declaration, exit 77 with the
refusal text. No declaration with the override set, exit 77, unchanged. The
declaration present with no display advertised, exit 77 with the inability text
and the old skip reason. The same with the override set, exit 1. The refusal is
what stops the first two: satisfying the interlock alone moves the gate to a
different exit path with a different message.

---

## 2026-08-10 -- The record is bounded by history, not by a file it ships with

The manifest introduced with the part split was described as bounding the
record from below. It does not, and the claim was wrong in the tracked text as
well as in the guard. `docs/devlog/published-entries.txt` is a tracked file, so
the change that drops a published entry drops its manifest line in the same
change, and every remaining rule then holds over a record with a hole in it:
nothing duplicated, nothing misordered, no month misfiled, manifest and files
in exact agreement. Removing one entry and its line took one edit and the gate
reported success by name over the result. A record cannot be bounded by a file
whoever removes the entry also writes.

The baseline is now the published branch's own history, which the change under
test cannot edit. The guard walks every commit that touched the record, reads
every state the record has ever been published in, and requires each published
entry to be present and to still carry the text it was last published with.
The comparison is non-forgetting: an entry deleted several commits ago is still
demanded today, which a comparison against the previous commit alone would not
do. An entry history has never seen is new, and a new entry that sits in an
archive rather than the live record is refused - that is how an entry ends up
published where nobody reads first.

Three constraints shape it. Bodies are compared with boundary blank lines and
the horizontal rule between entries normalised away, because moving an entry
changes exactly those and nothing that was written; a byte-exact comparison
would call every part split a rewrite and could not have landed. The baseline
is one branch and never every ref, because refs that are not ancestors of it
hold record states that were never published, and a baseline drawn from those
would import unpublished material into the standard the public record is judged
by and then demand entries nobody ever published. And an entry that history
holds in more than one form is measured against the most recent one, because
against its oldest the gate would demand text the branch itself no longer
carries.

What is checkable divides by environment, and the guard now states which side
it is on. In a repository, loss and rewriting are both caught mechanically,
because history is the copy of what was published that the comparison needs. A
source tree extracted from a release archive carries no history, so the run
says the published record is unchecked there by name and checks the internal
agreement that remains. The earlier claim that no check could see a rewrite was
true of an archive and false of a repository.

The first version of the comparison carried the defect it was written to
prevent. It read two streams as two files and told them apart by record count,
which silently reads the second file as the first when the first is empty: with
nothing ever published, every entry present was treated as published and every
published entry as missing. The scenario that publishes nothing found it, and
the streams are now tagged and read as one. The same defect is recorded as
still living in the static-analysis drift comparison, where an empty recorded
baseline makes a new diagnostic report as one that no longer occurs.

Verified: 31 self-test scenarios, including an entry dropped three commits back
with its manifest line, a verbatim move, a move whose boundary whitespace
changed, a move whose text changed under an unchanged heading, an unpublished
entry written straight into a part, an entry published in more than one form, a
commit on a ref outside the published branch, and a baseline that read nothing.
12 planted mutations of the baseline, the comparisons, the normalisation and
the floor, 12 caught. One survivor is not counted as coverage: excluding the
manifest from the historical read is defence in depth rather than behaviour,
because a tree whose manifest names an entry no file holds is already refused,
and within a commit the record files are read before it. ASan/UBSan 58/58 and
release 58 of 59 with a display attached, warning-clean. The one failure is
`shell_visual_validation_compositor_2x`, which is unstable on this display
independently of this change: it fails two runs in five on the unmodified
parent commit, and in three different checks across runs - a device-resolution
comparison, a well-border colour, and an over-protected device pixel - while
reporting that the window manager did not apply the size the layout audit
requested. Nothing outside `tools/`, `CMakeLists.txt`, and the record itself
differs from that parent.

---

## 2026-08-09 -- Guards a build from a release archive would not have run

Fourteen of eighteen gates declined to run in a source tree without repository
metadata, and the summary still read as a complete pass: a skipped test is
reported outside every line that counts, so a battery that ran a third of
itself prints what one that ran all of it prints. The condition was single -
every guard derived its corpus from Git, and every one was gated on the same
predicate. That is the environment a source archive is compiled in, and
packaging is on the roadmap, so the first packaged build would have reported a
green battery having run neither the privacy guard, nor the attribution
checks, nor the file-length ceiling, nor static analysis.

The corpus is now resolved in one place behind one interface. Where a
repository is present it is the tracked set read through the index, which is
the stricter reading and is what a commit would publish. Where it is not, it is
the source tree, less version-control metadata, the ignored workflow directory,
and every CMake build tree beneath the root - recognised by the cache file
inside it rather than by its name, so a build directory called anything at all
is still excluded. Matching is shared and only enumeration differs, and the
accompanying self-test requires the two enumerations to answer identically over
the same tree rather than checking each against a description of what it ought
to return.

Two things are genuinely unavailable without a repository: the index, and the
history the attribution checks read. Those checks now state that they did not
run and why, rather than the whole guard disappearing. Everything else runs.

No gate declares a skip return code any more. Each one resolves a corpus and
runs, or states a precondition and fails. The two self-tests that declined when
Git was absent now report that as a failure, because a self-test that ran
nothing has established nothing, and their prerequisites are recorded as
requirements of a checked build rather than of a developer checkout.

Verified: every gate executed in a tree extracted from an archive carrying no
repository metadata, the privacy guard among them, and it found an address
planted there. 11 planted mutations of the enumeration, the build-tree
exclusion, the index read, the root inference, the restored skip and the
floors; 10 caught. The eleventh removes a conditional whose effect is not
observable - the checks behind it read an empty history either way - and it is
recorded as such rather than counted as a catch. Release 59/59 and ASan/UBSan
58/58 with a display attached, warning-clean.

## 2026-08-09 -- Gates that measured nothing reported success

Two gates passed over an empty corpus. The formatting gate printed that zero
files had passed and called it a pass; the file-length gate printed no count at
all, so its success line read the same over the whole tree as over none. Every
file in an empty corpus is formatted correctly, and every file in it is under
the ceiling: both results were true and neither meant anything. The corpus is
enumerated from a repository root, which is exactly the kind of thing that can
resolve somewhere unintended with no visible sign.

Both now refuse an empty corpus by name and state the size of what they
measured, so the success line can be checked by reading it. The file-length
gate carries a second floor, because a corpus holding only binary assets
measures nothing either. Static analysis gained the same floor, where the
coincidence is sharper: with nothing analysed there are no diagnostics, and an
empty set matches an emptied baseline exactly.

The three gates without a self-test were the file-length ceiling, the QML lint
pass, and static analysis, and two of those three were the two that passed
vacuously. Each now has one. The ceiling is exercised at exactly 2,000 and
2,001 lines, separately in the working tree and in the index. The lint
self-test requires a scene with an unqualified access to be rejected, and
counts the corpus in its success line. The static-analysis self-test holds the
baseline in both directions: an unrecorded diagnostic is refused as new, and a
recorded one that stopped occurring is refused as well. Every scenario requires
a specific message rather than a bare non-zero exit, because a gate failing for
an unrelated reason would satisfy the weaker check.

Verified: 13 planted mutations of the floors, the ceiling boundary, both halves
of the index comparison, the fatal report, the drift comparison and the lint
threshold; 13 caught, keyed on exit status. Release 58/58 and ASan/UBSan 57/57
with a display attached, warning-clean.

Known gap: with an empty recorded baseline, the static-analysis comparison
reports a new diagnostic in the wording meant for a cleared one. The gate still
fails, so nothing passes that should not; the message is wrong. The self-test
keeps clear of that path rather than pinning it.
## 2026-08-09 -- The record is archived in parts before the month closes

The development record had 105 lines of headroom under the 2,000-line ceiling
and was growing by roughly forty lines an entry. The rule that keeps it small
moves a month into `docs/devlog/YYYY-MM.md` once the month closes, and August
has not closed, so the only sanctioned relief was unavailable for three more
weeks. Nothing further could be recorded, and shortening prose to fit is
explicitly not a way out of a length ceiling.

The rule now permits archiving an open month in numbered parts. What made this
the smallest available change is that the file already describes the mechanism:
entries move verbatim, and a published entry is never edited, reordered,
reworded, or removed. Moving entries was already sanctioned; only the timing
was not. A month is archived whole or in parts, never both, parts are numbered
from one without gaps and hold consecutive increasing stretches, and they are
not merged back when the month closes — a published part is as settled as a
closed month.

August is now held in three parts and the live record keeps the newest entries.
`DEVLOG.md` falls from 1,895 lines to 145, so the ceiling is no longer near.
The move was a transcription: recombining the four files reproduces the
pre-split body byte for byte, 114,683 bytes with md5 checksum
58ab090854827896176b0d0becf6fe01, and that was checked rather than eyeballed.

Structure alone cannot protect a record split across files. Every arrangement
check here — naming, linking, ordering, month membership — is satisfied by a
record with a stretch missing from it, and the missing stretch is exactly what
a split or a later rebase loses. So the record now carries
`docs/devlog/published-entries.txt`, every published heading in reading order,
and the gate requires it and the files to agree exactly. That bounds the record
from below as well as above: an entry cannot be dropped, and equally cannot be
reworded, reordered, or duplicated.

Stated plainly, because a manifest invites more confidence than it earns: it
pins headings, not bodies. Rewriting the text under an unchanged heading passes
untouched, and nothing here can see that without a copy of what was published
to compare against, which a source tree extracted from a release archive does
not carry. The rule against editing a published entry rests on review; what is
mechanical is that no entry silently disappears, changes place, or changes
name.

Verified: 21 self-test scenarios covering each refusal and an accepted layout;
18 planted mutations of the manifest comparison, the part rules, the ordering
and the floors, 18 caught. Two needed re-planting before they counted. Widening
the archive name pattern to anything beginning with a month survived the first
round, because the only misnamed fixture did not itself begin with one; the
scenario that discriminates it was added and the mutation planted again. The
second was re-planted because the code it targeted had been rewritten under it,
and a mutation that no longer applies has measured nothing. Release 55/55 and
ASan/UBSan 54/54 with a display attached, warning-clean.

## 2026-08-09 -- Restate the columns gap to the one that remains

The roadmap described two gaps behind the columns view. One of them has since
closed: entry actions resolve against the focused column listing, and the pane
header names that listing. The roadmap text still asserted the closed gap, so
it described a defect the tree no longer has. It now states only the remaining
one, that the shell's location does not follow the column chain. The entry
stays unclaimed.

## 2026-08-09 -- A directory is watched before it is read, not after

A listing was acquired by reading the directory and then establishing a watch
over it. Those two steps describe adjacent stretches of time, and in that
order they do not meet. A reading reports what was there while it ran; a watch
reports what happens after it exists. Between the reading passing an entry and
the watch coming into being there is an interval that belongs to neither, and
a change made in it is not late but absent: it is not in the listing that
replaces the presented rows, and no event ever describes it. The listing stays
wrong until something forces another reading.

The interval was not a narrow one. Establishing the watch only queued the
request for another thread, and the request was made after the reading
completed, so the gap ran from the moment the reading passed an entry to some
point after completion. One code path acquires every listing, so every
navigation, refresh, and post-operation reload carried it.

The order is now reversed. A reading starts only once the watch over the
directory reports itself established, so a change is either already in what
the reading returns or is reported as an event, with no moment that is
neither. The watch service gained that report: a request to watch a directory
is answered when the watch has been attempted, carrying the failure when there
was one, so a caller waiting on it is never left waiting for a watch that will
not exist. A service that has been stopped answers immediately for the same
reason.

Reversing the order moves the events into the middle of the reading, where
applying them would be wrong: the completed listing replaces the presented
rows outright, so a change applied to the rows being assembled would be undone
by the completion that follows it. Deliveries that arrive while a reading is
in flight are therefore held and applied on top of the listing it produces,
before the model reports itself idle. Idle now means the listing is current,
not that the reading finished.

Two smaller consequences came with it. The model can no longer report itself
idle before the watch exists, because the watch is established while it is
already busy. And a reading that has been superseded is still delivering
during the moment its replacement spends establishing a watch, so the current
reading's token is cleared when the replacement is asked for rather than when
it starts; without that, a superseded delivery would be published under the
new directory's name.

Verified by eight planted defects, each rebuilt and run: reading before
watching, holding nothing, holding without applying, not reporting the watch
established, reporting it established before establishing it, a stopped
service accepting a request it cannot answer, and leaving the superseded
token current. All eight were rejected. Reading without waiting for the report
was rejected only by the check that the reading has not started while the
report is outstanding; no case observes the residual interval it leaves, which
is one thread hop wide, and none is claimed to. The branch that reports an
established watch when no watcher could be created at all is also uncovered:
nothing in the suite makes the kernel refuse one.

Release and sanitizer suites pass warning-clean with the GPU-path gates run
against a display, and both binaries completed silent offscreen smoke launches
on the OpenGL and software paths.
## 2026-08-09 -- Harden columns ownership and focused-view actions

Column listings now remain under one explicit C++ owner when exposed to QML,
so tearing down the shell with a live columns chain destroys each adapter once.
The sanitizer shell-load gate covers that exact lifetime boundary with a real
directory listing active at engine teardown.

The workspace adapter continues to own tabs, history, and the location from
which a columns chain begins. Entry operations instead follow the focused view:
copy, move, rename, trash, select-all, operation dialogs, status, and cross-pane
transfers use the active column listing while columns mode is visible. The pane
header identifies that active listing's path, so an operation cannot resolve to
a selection retained invisibly by the list or grid model.

Text filtering applies only to the active column and releases descendants
before filtering can remove their anchor. Shared presentation settings still
fan out over the live chain, so their work remains proportional to the number
of visible columns; the per-keystroke filter path no longer pays that factor.
The model gate bounds key construction and row-index rebuilding above and below
using deterministic counters, and separately proves that selecting an already
open branch reuses its live listing. The five-level, 5,004-row fixture loaded in
253 ms in release and 502 ms under ASan/UBSan; filtering its active 1,000-row
listing built exactly 1,000 keys and one row index in both presets. Mutations
that disabled same-branch reuse or tripled filter work failed their dedicated
gates.

The warning-clean release build passed all 50 executed checks, including the
189.62-second static-analysis gate; two RHI probes skipped because the offscreen
platform supplied no usable RHI context. ASan/UBSan passed all 49 executed
checks, with static analysis disabled and the same two RHI probes skipped.
Release and sanitizer binaries completed silent eight-second software smoke
launches. The changed shell interaction ran at the test environment's default
1x logical scale; it adds no pixel or frame-comparison claim.

## 2026-08-09 -- Unclaim the columns view pending action and location wiring

The roadmap claimed the Miller columns view. Two gaps make that claim
premature and it is withdrawn until both close.

Entry actions resolve their target through the list model, not through the
view that holds focus. With the columns view focused and holding its own
selection, a delete, copy, move, or rename acts on the list model's selected
rows in a different directory, and nothing on screen indicates the mismatch.
A filter applied while a chain is open also narrows ancestor columns, so the
row anchoring a live column can leave the listing while the chain stays open.

The columns view also does not drive the shell's location: its current path
and activation signal have no consumers, so the path navigator and the rest
of the shell stay where they were while the chain moves.

The view itself is unchanged and remains reachable. Its own navigation,
accessibility declarations, virtualization, and retained-state bounds hold as
recorded.


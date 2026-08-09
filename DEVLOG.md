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


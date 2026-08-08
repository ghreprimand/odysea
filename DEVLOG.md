# OdySea — Devlog

Public running record of OdySea development, in reverse-chronological order.
Each entry records what landed, how it was verified, and any known gaps. See
`docs/ROADMAP.md` for milestone status and `docs/DESIGN.md` for durable product
and architecture decisions.

This file holds the entries for the current calendar month. When a month
closes, its entries move verbatim into `docs/devlog/YYYY-MM.md`, which is then
linked below. A new entry belongs at the top of this file. Published entries
are never edited, reordered, reworded, or removed, whether they live here or in
the archive.

**Archived months**, most recent first:

- [2026-07](docs/devlog/2026-07.md)

---

## 2026-08-08 -- Render-site contrast matrix, mask over-protection bound, and grab sentinels

The measured contrast matrix is now derived from render sites instead of the
role vocabulary. The previous matrix asserted twelve of its twenty-eight
pairs against roles no tracked surface paints — ten on an unpainted well bed
— while omitting the beds that carry most glyphs: the pane ground under
transparent rows, and the selection, hover, and pressed interaction beds.
The rebuilt matrix measures every painted foreground role on every bed
variant the compositor can present, including the deep-field gradient
extremes of the pane ground and the chrome strip's composited panel, in all
six families and both override states.

The honest matrix reported real defects, and the palettes moved to fix
them. The faint ink — file icons on every row state — could not clear its
non-text floor on the selection bed in four families or on the hover bed in
two; it brightened in the dark families and darkened in parchment. Icon ink
is now a curated per-family hue under the unchanged emission cap: the cap
limits the brightest channel, so the graphite and aurora families, whose
faint ink peaks in the low-luminance blue channel, landed their capped
symbols below the pressed-bed floor; their curated icon hues are neutral
enough to stay measurable on every control bed, and the other families keep
byte-identical icon ink. Each family also gained a curated high-contrast
danger variant, because five of six base danger inks could not reach the
4.5:1 the override promises on the selection and hover beds.

Protected-well masking gained its missing direction. The device-pixel gate
verified that a mask too small leaves a processed seam on the well border,
but nothing required a pixel just outside a well to be processed, so an
oversized mask of any magnitude passed every assertion while silently
exempting surrounding chrome. A second well, separated from its emitter
frame by a dark gutter, now arms an outward sweep: the ring one device
pixel outside the well sits on receiving material and must differ from the
plain path. A painted-mask oversize of one device pixel with unchanged
mirror geometry — invisible to the geometry and inner-border assertions —
fails the new sweep at its first pixel.

The presentation suite's frame claims were all relative — equalities and
inequalities between grabs — which a scene that renders nothing satisfies
perfectly; the regression guard for the viewport-escape defect and both
shader-latch cases passed with the scene hidden. Every grab in that suite
now passes an absolute-value vacuity sentinel on a coordinate the harness
fixes on every profile and backend: the protected patch in the pipeline
scenes and the white patch in the latch scenes. With the scene hidden, all
six grabbing tests fail loudly through the sentinels, with grab dimensions
and content verified rather than trusted.

Verification: the rebuilt matrix passes all families and states with the
moved colors and fails with named palette, pair, bed, and state when any
fix is reverted; the outward sweep runs in the real-display GPU gate
alongside the existing entries. The warning-clean release build passed all
46 checks including both real-display RHI entries; a fresh ASan/UBSan build
passed all 45 enabled checks; release and sanitizer binaries completed
silent eight-second smoke launches on the software and OpenGL paths.

---
## 2026-08-08 -- Storage usage deduplicates an inode however it was reached

The counting policy says an inode reached twice is counted once. Storage usage
only held that for entries with more than one link, on the reasoning that a
single link means a single path. A bind mount of one file breaks that
reasoning: the inode appears at two paths with a link count of one at each, on
one device, under two different directories. Nothing else in the walk noticed.
The boundary check does not fire, because a bind mount of the same filesystem
shares its device number; and directory deduplication does not apply, because
the two paths sit under different directories. The bytes were counted twice
and `deduplicated_entries` stayed at zero, so the inflation was not merely
wrong but undetectable by the field that exists to report it.

Every known non-directory identity is now recorded. Link count says nothing
about how often an inode can be reached, so it no longer gates the guarantee.
The cost is one identity per counted entry rather than per multiply linked
one: measured over a synthetic tree of 200,000 files, peak resident memory
went from 4.6 MB to 16.7 MB and the walk from 142 ms to 174 ms, or about sixty
bytes per counted entry. That trade is stated in the header and the design
document rather than left implicit, because a measurement that is wrong
without saying so is worth more memory than one that is right.

The case that pins it performs the bind mount for real. A forked child enters
a private user and mount namespace, binds one file over another, and reports
what the walk counted; the namespace dies with the child, so nothing is left
mounted and nothing outside the case can observe it. The child asserts the
fixture before trusting it: unless both paths report one device, one inode,
and one link, it reports the fixture as unreproduced rather than letting the
walk agree with a broken implementation for the wrong reason. A kernel without
unprivileged mount namespaces cannot build this fixture, and the case says so
in its output instead of quietly reporting a guarantee it never tested.

Four planted defects were rejected, keyed on exit status: restoring the link
count precondition, recording no counted inode at all, binding something that
is not the shared inode, and skipping the bind mount entirely. The last two
fail the fixture assertions rather than the counting check, which is what
shows the case cannot pass without the repeat actually happening.

The warning-clean release build passed all 47 checks, including scoped static
analysis and both real-display RHI entries. A fresh ASan/UBSan build passed
all 46 enabled checks; static analysis is disabled in that preset. Release and
sanitizer binaries also completed silent eight-second smoke launches with the
software and OpenGL rendering paths.

## 2026-08-08 -- The scanned listing carries its own name index

A folder-watch delivery no longer searches the scanned listing once per name
it carries. Renames, removals, and updates each ran a linear search over the
whole listing per delivered name, and the update search rebuilt every
candidate's key inside the comparison, so a burst cost the product of its size
and the directory's. Identity matching for a departed entry counted the whole
listing per removed name on top of that.

The listing now carries a name index maintained only by the three functions
that may change it, mirroring the discipline the presented rows already
follow. Entries are indexed by name rather than by key: every entry in a
listing has the scanned directory as its parent, and a delivery is discarded
unless it describes that same directory, so an entry's key is its name
prefixed by a path both sides have already agreed on. Matching on the key and
matching on the name therefore select the same entry, which is what lets one
index answer a search that previously tested both, and the name costs neither
a path normalization nor a string allocation to probe. That premise is
asserted directly rather than assumed: every scanned entry's key is checked
against its name under the scanned directory, and the listing is checked to
hold one entry per name.

The delivery itself is indexed once as well, since a burst can carry as many
entries as the directory holds. Identity counts over both collections replace
the per-name linear counts; the spelling they use carries every field the core
identity compares, and the fields it omits when creation time is unknown are
never written in that case, so two entries share a spelling exactly when the
core calls them one entry.

Measured on a delivery of 400 changed names against an 800-entry directory:
241,400 key constructions before, 800 after, which is the presented row count
and nothing more. The same instrument bounds it in the suite, at a ceiling of
four per entry the update could legitimately have to key. Loading also
benefits, because the scan path merges through the same index: key
construction over a load and a refresh fell from 25.9 per entry to 15.4.

The presented row index cannot serve this purpose and was not reused. It
describes the rows on screen, and the listing differs from them by exactly the
hidden and filtered entries, so answering a watch delivery from it would make
removals and updates of withheld entries invisible. A case pins that
difference by removing a hidden entry through a watch burst.

Ten planted defects were rejected by the suites, keyed on exit status: a name
index left stale after a removal renumbers the listing, a merge that appends
instead of replacing, a listing replaced without its index, a rename resolved
against the wrong collection, a departed entry followed without a unique match
on the delivered side, a departed entry keeping its selection, its cursor, its
range anchor, or its rubber-band membership, and unknown identities counted so
that two of them match each other.

Three planted defects survived, and each is recorded rather than papered over.
Holding the last row of a repeated name instead of the first cannot be
observed, because the listing holds one entry per name and that is now
asserted. Erasing a remapped name from the listing cannot be observed, because
the remap has already moved the entry's keyed state before the removal pass
runs, which makes that guard a redundant second defence. Counting unknown
identities in only one collection cannot be observed, because the decision
requires a unique match in both and an early return rejects an unknown
identity before either count is read; removing all three defences together
does fail.

The cases these two changes added carried the model's suite past the
2,000-line ceiling for a tracked file, so it is split here by responsibility
rather than by size. One half covers how a listing is acquired and kept
consistent: scanning, publication, folder-watch deliveries, entry identity,
and the derived indexes. The other covers what the model does when a person
drives it: selection and cursor contracts, direct path entry and completion,
activation and breadcrumbs, drop acceptance, and filesystem operations. Both
halves build the same kinds of fixture, so the fixture builders moved into a
shared header instead of being duplicated or arbitrarily owned by one side.
The suites run as two checks rather than one, and every case moved verbatim.

The warning-clean release build passed all 47 checks, including scoped static
analysis and both real-display RHI entries. A fresh ASan/UBSan build passed
all 46 enabled checks; static analysis is disabled in that preset. Release and
sanitizer binaries also completed silent eight-second smoke launches with the
software and OpenGL rendering paths.

## 2026-08-08 -- Directory loads publish on a growing interval

Loading a directory no longer costs the square of its entry count. Merging a
delivered batch into the scanned listing and reconciling the presented rows
both cost work proportional to the whole listing rather than to the batch, and
both ran once per fixed-size delivery, so a load performed listing-sized work
once per fixed number of entries. Key indexing had already lowered the
constant by roughly two orders of magnitude, but the exponent came from the
number of passes rather than from what a pass does, so it survived.

A scan now holds delivered entries until they amount to a share of the listing
already presented, then publishes them together. Successive publications
happen at geometrically growing sizes, which bounds their number by a
logarithm of the entry count and their summed cost by a constant multiple of
it. The interval never falls below one delivered batch, so the first content
still reaches the view as soon as the scanner has produced any, and a refresh
of an already large listing no longer republishes it once per delivery.
Entries still held when a scan completes are merged before the completion path
replaces the listing, and entries held by a superseded scan are dropped rather
than merged into the directory that replaced it.

Key construction is the machine-independent measure of reconciliation cost,
and it is now flat per entry: 25.4 constructions per entry at 4,000 and 25.9
at 8,000, staying within a percent of 26 out to 128,000. It was 26.7 per entry
at 2,000 and 385 at 128,000 before. Release wall clock over a load and a
refresh of the same directory: 4,000 entries 85/153 ms becomes 31/29 ms, 8,000
becomes 63/69 ms, 16,000 1712/2912 ms becomes 174/140 ms, and 32,000 entries,
which previously took 7.0 s to appear, now takes 302 ms.

The regression gate holds the shape rather than the constant. It measures a
load and a refresh at two sizes, one twice the other, and bounds three things:
the flat per-entry rate, the ratio between the sizes, and wall clock for the
catastrophic case. The ratio is a usable instrument only because the healthy
state is now linear; while both the healthy and the defective states were
quadratic every reading landed near four and no threshold separated them.
Measured 2.04 against a 2.80 ceiling, with 3.74 for a publishing interval that
stops growing.

Ten planted defects were each rejected by the suites, keyed on exit status:
losing entries at completion, carrying a superseded scan's entries into the
next listing, a fixed publishing interval, no interval floor, republishing
without clearing, dropping entries from the completed listing, publishing
without reconciling, presenting entries before they are published, merging
only the first held entry, and appending a redelivered entry instead of
updating its row. The last of those is invisible to anything that inspects a
settled model, because completion replaces the listing outright, so it is
pinned by a delivery driven directly at one publishing interval.

The warning-clean release build passed all 46 checks, including scoped static
analysis and both real-display RHI entries. A fresh ASan/UBSan build passed
all 45 enabled checks; static analysis is disabled in that preset. Release and
sanitizer binaries also completed silent eight-second smoke launches with the
software and OpenGL rendering paths.

Known gap: this bounds how often the presented rows are reconciled, not what a
reconciliation does. A single pass still inspects every presented entry rather
than only what changed, so an update remains proportional to the listing. A
pass that were proportional to the change instead would need the presented
rows in a structure supporting ordered insertion without renumbering, since
maintaining a sorted flat vector under scattered insertions costs the square
of the entry count in element moves however the work is described.

## 2026-08-07 -- Directory view accessibility and roadmap alignment

Directory lists and icon grids now identify themselves to assistive
technology and distinguish the active pane from an inactive pane. Realized
rows and cells expose list-item roles, file-kind names, selection state, and
the current entry. The accessible name is derived from the entry's displayed
name and whether it is a file, folder, or symbolic link; it never substitutes
an implementation index for the content a person needs to identify.

The tracked roadmap now describes directory scanning without promising that
large-directory reconciliation cannot stall. Its M3 section also includes the
storage-map, hardened entry-identity, and capability-gated filesystem work,
with completion state matching the behavior currently shipped.

Verification covers list and grid roles, non-empty view and entry names,
selection updates, current-entry state, and active-pane label changes in the
complete shell. The warning-clean release build passed all 46 checks,
including scoped static analysis and both real-display RHI entries. A fresh
ASan/UBSan build passed all 45 enabled checks; static analysis is disabled in
that preset. Release and sanitizer binaries also completed silent eight-second
smoke launches with the software and OpenGL rendering paths.
## 2026-08-07 -- An honest palette list and alias-aware shortcut conflicts

The command palette now lists only what it can reach. A declaration whose
enablement is a per-target predicate — open this entry, remove this Place,
focus this pane — can never be satisfied by the global context the palette
supplies, so its row sat permanently disabled, and most such rows stated no
reason. The registry now omits target-scoped declarations from contexts that
cannot carry their target, and every remaining declaration that can disable
states why: no earlier or later history for Back and Forward, the filesystem
root for Up, an empty recent list, and a single-pane layout for the pane
commands. The suites pin the invariant itself — every listed row is enabled
or states a reason — rather than row counts, so future declarations inherit
the obligation instead of silently growing dead rows. The omission is
per-context, not a blocklist: the same registry lists a target-scoped
declaration when the supplied context carries its target, and a test proves
it from entry and Places contexts. Palette rows also now disable at the item
level, so assistive technology reads a disabled row as unavailable instead of
actionable; the gate is the same live enablement that drives the row's
rendering, and the registry still revalidates at trigger time.

Shortcut-conflict detection now compares key sequences as Qt parses them
rather than as raw strings. `Delete` and `Del` are one physical key wearing
two spellings, and the raw-string comparison reported no conflict for that
pair — latent rather than active, since the shipped set is conflict-free
under normalization, but a gate that misses the collision it exists to catch.
Each declared sequence is normalized through Qt's own parsing before keying;
an unparseable sequence keys on its raw string instead of colliding with
other unparseable ones as an empty key; a planted `Delete`/`Del` pair now
fails the suite.

Two focus-restoration assertions were tightened to the behavior they guard,
with no behavior change: pointer dismissal of the palette returns focus to
the summoning toolbar button, asserted as that button rather than as focus
landing anywhere in the window, and the pointer-dismiss case now verifies the
palette actually took focus before asserting it was returned, matching its
keyboard twin.

Verification: release build and tests 46/46, including the GPU-path
presentation and validation entries on a live display; AddressSanitizer and
UndefinedBehaviorSanitizer battery 45/45; QML formatting, lint, module, and
scope gates; repository guards; silent smoke launches on the software and GPU
paths. Deliberately planted defects each fail the suites: removing the
reachability filter, removing a stated reason (the failure names the
offending action), reverting the normalized comparison, and dropping the
palette's focus grab on open. Planted palette-side focus theft after
dismissal is absorbed by the scene's own restoration and is caught at the
component level; the tightened scene-level assertion guards the integrated
focus path.
## 2026-08-07 -- Directory loads stop growing with the square of the listing

Opening a large folder was unusable: 4,000 entries took 14.4 s to appear and
36.1 s to refresh, and 8,000 entries never finished. The cost was in
reconciliation, which runs once per scan batch and matched each presented
entry to its current row with a linear search. Every comparison in that search
rebuilt the candidate's key, and building a key normalizes a path and
allocates a string, so a load performed key constructions on the order of the
listing size cubed. A second search of the same shape deduplicated each
delivered scan entry against everything scanned so far.

Both searches are now indexed by key, and each key is built once per entry per
update rather than once per comparison. The same 4,000 entries load in 0.09 s
and refresh in 0.17 s, 8,000 load in 0.32 s, and 32,000 load in 7.0 s.

Reconciliation is still quadratic in the listing size, by design rather than
by oversight: it runs once per scan batch and inspects every presented entry,
so this change lowers the constant by roughly two orders of magnitude without
moving the exponent. Retiring the remainder means applying a batch as a
difference against the presented rows instead of re-presenting the whole
listing on every batch, which is a change to how updates are published and is
kept separate from a fix that must not alter behavior.

Row keys and the key index are derived state, so the rows may only be
replaced, extended, or truncated through three functions that update all three
together; no other code may touch the presented rows. Where two rows resolve
to a single key, which a pending rename remap can produce, the first row wins,
which is what the linear searches returned. Taking the last would report an
unchanged entry as changed and repaint rows that did not move.

The suite had no case anywhere near a size where any of this was visible, so
the fix ships with one. It bounds two quantities. Wall-clock time catches a
catastrophic regression, with a separate budget for the sanitizer build, whose
instrumentation costs about twenty times the release build and would otherwise
force a bound too loose to mean anything. The count of key constructions
carries the algorithmic shape: it is independent of machine speed and of load,
and over this case it reads 0.43 M healthy against 16.2 M and 366 M for the
two searches this change removed. A growth ratio across two sizes was
considered and rejected, because both the healthy and the defective code are
quadratic and the difference between them is the constant, not the exponent.

A directory-model invariant case covers the rows, their keys, and the index
across reordering, filtering, insertion, removal, rescan, a watch burst, and
an empty listing. It also checks them from inside the row-insertion and
row-removal signals, because that is when a view asks the model for data and
because a stale key repaired before the update ends is invisible from outside
it. Thirteen deliberate defects were planted; every one is now caught, but
four of them survived the first version of these cases and the mid-signal
checks exist because of them.

Known gap: the watch-update path still searches linearly by name and by key
for each renamed, removed, or updated entry in a delivery. That cost grows
with the delivery size times the listing size, is not on the load path, and is
not covered by the new bounds.

## 2026-08-07 -- Cancellable recursive storage-usage accounting

The core can answer "what is taking up the space here" for a subtree, as
running totals per immediate child, delivered while the walk is still going
and abandoned the moment the caller looks elsewhere. This is the Qt-free half;
the interactive map and its accessible list equivalent follow separately.

The counting policy is stated rather than implied, because a usage figure that
does not say what it measures is not a measurement. Apparent and allocated
sizes are reported side by side and never blended: a sparse file claims far
more than it occupies, a compressed one less, a small file usually occupies a
block more than it claims. Every entry counts, hidden ones included.
Directories count their own metadata as well as their contents. A symbolic
link counts at its own size and is not followed unless the caller asks. The
same inode reached twice is counted once, the first reach owning the bytes and
later reaches reported as deduplicated, so a set of hard links cannot inflate
a subtree; the consequence is that a child's total is "what removing this
child would free" only when nothing outside the subtree also links to its
files.

Crossing a filesystem boundary is a caller's decision and never implicit. A
walk stays where it started unless told otherwise, because a walk from a
system root would otherwise wander into pseudo-filesystems, removable media,
and network mounts. Two consequences follow from the boundary being a device
number, and both are recorded in the design rather than left to be
rediscovered: a Btrfs subvolume carries its own anonymous device number, so a
nested subvolume reads as another filesystem and is reported as a skipped
boundary rather than measured; and a bind mount of the same filesystem shares
its device number, so the boundary setting does not govern it at all.

Termination over cycles is structural, not a depth limit or a timeout: a
directory already entered in this walk is never entered again. Directory
identities are tracked unconditionally rather than only when links are
followed, precisely because of the bind-mount case above — such a subtree
re-presents directories the walk has already entered, with no link involved
and no boundary crossed, and tracking only some of the time would count it
twice by default. The cost is bounded by the number of directories, far below
the number of files.

A subtree that cannot be read degrades to a reported partial result rather
than aborting the walk or being dropped as though it were empty. The failure
is attributed to the child subtree it happened in as well as to the whole
walk, and the unreadable directory still counts its own metadata. Only a root
that cannot be examined or listed is an error, and a root that is not a
directory is refused rather than half-measured.

Cancellation is polled per entry, so it is prompt at any depth rather than at
the next directory boundary, and it is polled between directories too, so a
queue of unreadable ones is abandoned rather than drained. The walk keeps one
directory open at a time and carries the rest as pending paths, so depth costs
no file descriptors.

Verification: thirteen cases cover aggregate totals against a fixture tree whose
per-entry sizes are measured with a plain `lstat`, independently of the
`statx` the scanner reads; sparse files keeping the two sizes apart; a hard
link counted once with the repeat reported; a symbolic-link cycle terminating
in both follow modes; an unreadable subtree reported as partial while its
readable sibling is measured in full; cancellation at depth stopping within a
few entries rather than at the end of the directory; cancellation abandoning a
long queue of unreadable directories; progress reports as growing snapshots;
a child reported as settled in a tree too small to reach the progress
interval; hidden entries counted; and unusable roots reported. Thirteen
deliberate defects were planted across this work and its shared seams and all
thirteen were caught, keyed on the test binary's exit status rather than on
counting failure lines, because a mutation that crashes or hangs a suite
prints no failure line at all.

Two measurement errors are on the record because they invalidated earlier
numbers. A first probe run restored mutated sources with their original
timestamps, which left the build convinced the stale mutated objects were
current, so several later measurements ran against a library that still
carried an earlier mutation; the harness now stamps restored files and
verifies the bytes. With that corrected, two mutations that had appeared
caught survived — cancellation polled only between directories, and the
report delivered when a child settles — and both are now covered by cases
written for them.

Known gaps, stated rather than glossed. The boundary decision is verified
through a directory symbolic link that resolves onto a second filesystem,
which is the only form of it that can be set up without privileges; a real
mount point inside a fixture tree cannot be. The decision is one shared
function rather than a condition repeated at each descent site, so the covered
case and the uncovered one cannot drift apart, but the uncovered call site is
uncovered. A file with a single link reached through a bind mount is
deduplicated only because the directory above it is; a bind mount that
exposes a single file has no coverage. Both remaining cases need a privileged
mount to test at all.

Gates: release tests pass 42/42 and the sanitizer suite 41/41, both from wiped
build directories and warning-clean under `-Werror`; formatting, scoped static
analysis against the baseline, the QML and module guards, the public-repository
guard, the file-length guard, and the devlog archive guard pass; offscreen
smoke launches on the release and sanitizer builds, on both scene-graph
backends, run silently. The static-analysis baseline moves by six lines: four
relocated with the extracted seam, and four categories the test suites share
with the rest of the corpus. Two fatal diagnostics were fixed rather than
recorded.


## 2026-08-07 -- One place reads an entry's own metadata

Reading an entry's own facts — identity, type, apparent and allocated size,
link count, modification time, none of them a symbolic link target's — was a
private detail of directory listing. Usage accounting needs the same facts,
and reading them a second time would mean a second chance to reassemble the
device number wrongly. That failure mode is invisible by construction: a
wrong reassembly stays consistent with itself for every entry, so nothing
observable fails.

There is now one implementation, in an internal core seam that is not
installed and not part of the public API, and one test that pins the
reassembled device number against what `lstat` reports for the same entry.
Listing consumes it for identity and modification time; the block count and
link count it also reports are what usage accounting needs. Two accessors
exist: one that describes the entry itself, and one that resolves a symbolic
link, used only where a consumer has explicitly decided to treat a link as
its target.

Verification: the core listing and identity suites pass unchanged, including
the existing case that requires identity to carry a creation time exactly
when the filesystem reports one. Known gap, unchanged by this work: the
`lstat` fallback taken when `statx` is unavailable still has no automated
coverage, because it cannot be provoked without a seccomp filter or an older
kernel.


## 2026-08-07 -- One cancellation contract for long walks

Directory listing owned a single-worker, newest-request-wins queue: one
private thread, monotonic request tokens where starting a request cancels
everything issued before it, exactly one completion callback per request
including one replaced before it ever started, no callback delivered once
teardown begins, and a cancellation flag the running walk polls. Recursive
storage accounting needs precisely those guarantees, and a second
implementation of them would be a second set of edge cases. The edges are
where two implementations would disagree quietly: a superseded request that
never ran, a teardown while a walk is mid-flight.

The contract now lives in one internal core seam that carries a request type
as a parameter. The directory scanner keeps its public API byte for byte and
holds the worker behind a pointer, so the public header no longer exposes
threading internals at all. The seam is not installed and is not part of the
public API.

Verification: a suite drives the seam directly rather than only through a
walk that happens to use it. It pins that work runs off the calling thread,
that four rapidly queued requests produce five callbacks between execution
and abandonment with no request answered twice and no token reused, that a
running request can observe its own cancellation from inside the walk, that
an explicit cancel reaches the request in flight, and that teardown stops
delivery before it joins. The teardown case observes the flag flip from
another thread while a request is still executing, which is the only moment
the ordering is visible; a request queued in that window is refused with a
zero token and receives no callback. Deliberate defects were planted and
caught: dropping the callback for superseded requests, leaving delivery
enabled through teardown, and letting a new request fail to supersede the
running one each failed the suite. The existing directory-scanner suite
passes unchanged, which is what shows the extraction preserved behavior
rather than redefined it.


## 2026-08-07 -- Dual-pane transfers

The workspace now expands from one directory view into two live panes. Each
pane has its own directory adapter, so location, selection, sorting, filtering,
tabs, and navigation history remain independent while both sides stay visible.
Exactly one pane is active: its frame carries the focus color, `F6` and pane
header presses switch it, and the toolbar, path navigator, tabs, action row,
status strip, dialogs, menus, command palette, and declared shortcuts all bind
to that pane's model.

The divider can be dragged or moved with `Ctrl+Alt+Left` and
`Ctrl+Alt+Right`. Dynamic bounds preserve a usable width for both panes, and
the single/dual state plus divider ratio persist in the existing versioned
settings file. The settings schema advanced to version 3 and remains tolerant
of older and newer files. Resetting appearance and navigation preferences also
returns the workspace to one pane with an even divider.

Transfers reuse the existing filesystem-operation adapter. `Ctrl+Shift+C` and
`Ctrl+Shift+M`, along with copy and move buttons on the active pane, send its
selection to the opposite pane. The same declared registry actions serve both
directions and revalidate the destination at invocation. Simultaneous models
also namespace their in-memory thumbnail identifiers, so releasing a thumbnail
from one pane cannot remove the other pane's copy from the shared image
provider.

Verification covers two independent model stand-ins in the complete shell:
keyboard and pointer pane activation, single/dual toggling, bounded divider
resizing, both copy and move in both directions through both input paths, and
active-model routing while the inactive pane retains its state. Core and Qt
adapter tests cover settings round trips, clamping and notification, and
thumbnail-provider independence. The warning-clean release build passed all
41 checks, including scoped static analysis. A fresh ASan/UBSan build passed
all 40 enabled checks; static analysis is disabled in that preset. Release and
sanitizer binaries also completed silent eight-second smoke launches with the
software and OpenGL rendering paths.

---


## 2026-08-07 -- Visual foundation acceptance

The visual system now has an automated acceptance matrix, and running it
found and fixed three real defects.

Two new test entries drive the full shell scene at 1x and at a forced 2x
scale factor: at the narrowest supported window (720×480) and at a wide
layout, every chrome control must stay visible, non-degenerate, and inside
the window, chrome rows must not overlap, and no visible label may overflow
without eliding. With every effect off, buttons and fields must show the
accent focus ring, a focused directory view must turn its pane frame stroke
to the accent, and tab traversal must cycle without dropping focus. Reduced
motion must zero the effective persistence and the shared motion token while
chrome geometry stays byte-stable. A 2,000-entry directory must realize a
viewport of delegates in both views while the effect layer's structure and
the well registry stay viewport-sized. A companion device-pixel suite pins
well-mask geometry as logical-coordinate math and, on the GPU path, sweeps a
protected well's entire border byte for byte against the plain path, with an
emitter ring outside the well so a mask misaligned in any direction feeds a
border row and fails — and a vacuity sentinel that rejects any environment
whose grabbed frame does not carry the rendered scene. A new theme test
measures contrast ratios for every foreground role on the beds it renders
on, in all six families, in both override states: 4.5:1 for text roles and
3.0:1 for non-text indicators under high contrast, the same floors on the
reading pairs by default, and documented anti-regression bounds where the
accepted appearance is deliberately subdued.

The matrix caught three defects. First, text lift multiplied chromatic inks
toward white on light families too, where inks are dark marks on bright
grounds — the focus and accent indicators of the light families measured
below 3.0:1 and a link ink below 4.5:1 under the shipped Balanced profile.
Light families are now exempt from the lift, which restored every measured
pair. Second, the toolbar's labeled workspace toggles and the action row's
labeled operation buttons overflowed the window at the claimed 720-pixel
minimum width; both strips now drop labels below a measured width bound
that scales with the interface, keeping icons, accessible names, and
tooltips. Third, on a real 2x surface the protected-well guarantee leaked:
linear sampling of the mask edge lands on a half-texel phase at fractional
device scales, and the proportional protection mix let roughly a quarter of
the added light onto the well's outermost device row. Protection is now
binarized at the sampling site in both the bright pass and the composite —
any pixel at least half covered by the mask is protected outright — which
the border sweep verifies at 1x in the automated gate and which was
confirmed seam-free on a live 2x surface.

One environment truth is recorded rather than papered over: the offscreen
platform never allocates a genuine high-density framebuffer. Under a forced
scale factor it reports a device pixel ratio of two while rasterizing at 1x,
and a grabbed frame is a device-sized canvas without the scene at device
resolution — which is exactly the false-pass shape the vacuity sentinel now
rejects. The automated pixel gate therefore runs on the GPU path at 1x, the
layout and geometry assertions run at both scales, and full-density pixel
verification is a run of the same suite on a windowing system with a real
2x surface, performed for this record and passing.

Verification: release and ASan/UBSan batteries green including the three new
test entries and the GPU-path launcher; formatting, static-analysis, QML,
module, file-length, and public-repository guards green; smoke launches on
the software and RHI backends silent. Five planted defects — the light-family
lift exemption removed, the action row's compact mode disabled, the focus
ring bound to the resting hairline, the composite's mask sampling shifted by
a texel, and list virtualization defeated by an unbounded cache — each
failed exactly the suite written to catch it and were reverted.

## 2026-08-07 -- Command palette

The shell has a command palette: every labeled action the registry declares,
searchable by label or id, opened with `Ctrl+Shift+P` or a toolbar button and
dismissed with Escape or a press outside. The palette is a registry surface
in the same sense as the menus and the toolbar — it enumerates from the
registry at open and on every filter change and holds no command list of its
own, so an action declared in the shell is reachable in the palette with no
palette-side change. Each row shows the action's declared key sequence, read
from the declaration rather than restated. Disabled actions stay listed and
render disabled with the reason their declaration states; keyboard navigation
skips them, and activation — row click or Return — routes through the
registry's trigger-time revalidation, so a listed row can never fire a dead
action. The dual-pane toggle moved from `Ctrl+Shift+P` to `F3`, with its
pointer path unchanged, to free the sequence for the palette; the
shortcut-conflict assertion covers the new declaration set.

Focus behavior: the filter field owns focus while the palette is open, and
arrow keys steer the list from the field. Restoration on dismissal is the
modal focus popup's platform behavior; a removal-style probe showed
palette-side restore code is not observable over it, so none ships, and the
full-shell input-parity suite pins the behavior instead — after Escape, focus
returns to the directory view the palette was summoned from.

Verification: a dedicated palette suite drives the popup standalone against
recording stand-ins, including a declaration added to the registry at runtime
becoming reachable and triggerable with no palette-side change, and a
sequence-ownership case asserting `Ctrl+Shift+P` maps to the palette and `F3`
to the dual-pane toggle. Deliberate defects were planted and caught before
restoration: a palette holding a hardcoded command list, an activation path
bypassing trigger-time revalidation, a dropped focus grab on open, and a
reintroduced duplicate key sequence each failed at least one suite. Release
tests pass 39/39 with the forced-OpenGL presentation entry running in full;
the sanitizer suite passes 38/38; QML formatting and lint, the module
manifest guard, the public-repository guard, and the file-length guard pass;
release smoke launches on both the software and hardware scene-graph backends
run silently.

## 2026-08-07 -- Entry identity survives recycled device and inode numbers

Selection, focus, and the selection anchor follow entries by filesystem
identity, and that identity was the device and inode pair alone. The pair is
not unique over time. On Btrfs every subvolume root carries inode number 256,
and the device half is an anonymous number the kernel allocates at runtime,
returns to a pool when the subvolume is removed, and hands to the next
subvolume created. A removed subvolume and an unrelated new one therefore
present exactly the same pair.

That is not caught by requiring an unambiguous match. Reconciliation already
declined to follow an entry whose identity appeared more than once on either
side, but a recycled pair appears exactly once on each side, so the match looks
clean and selection moves onto an unrelated directory.

Identity is now a single comparable value in the core rather than loose fields,
and it carries the entry's creation time alongside the device and inode
numbers. Creation time is the one timestamp that neither a rename nor a content
write disturbs, so it separates a recycled identifier from the entry that held
it before while leaving an entry that merely moved or changed matching itself.
Listing reads it with `statx` in place of `lstat`: the same single syscall per
entry, so nothing about scanning got more expensive. Filesystems that record no
creation time degrade to the device and inode pair, which is what identity was
before. Comparison for following an entry also rejects unknown identities
outright, because two entries whose metadata lookup failed hold identical
zeroed fields and would otherwise compare as the same entry.

Measured on Btrfs before the change: four sibling subvolume roots all reported
inode 256 while their device numbers differed, so live siblings did not in fact
collide; removing a subvolume and creating another reproduced the earlier
device and inode pair on three attempts out of three, roughly two milliseconds
apart, with creation times that differed in every case.

Reconciliation runs on two paths and both were covered. The watcher path
compares identities directly and now does so through the core, replacing a
second copy of the counting and matching rules that had been restated in the
application layer. A completed rescan groups whole listings by a hashed
spelling of the identity, so that spelling has to carry every field the
identity carries.

Verification. New focused core exercises cover both halves of the contract:
distinctness, including a recycled pair fed directly at the model boundary, an
unknown identity matching nothing, and a partially known one refusing to match;
and stability, that a renamed and rewritten file, and a renamed directory, keep
the identity they had. Three model-level exercises drive the collision through
each reconciliation path and require selection to be dropped rather than moved,
including one that pins the two creation times to the same second so that only
the sub-second component separates them, matching what the measurements showed.
A further exercise covers the requirement that a match be unique, using hard
links, which share one identity because they are one file while remaining
entries a user selects separately.

Seven independent mutations confirm the exercises discriminate: dropping the
creation time from the rescan key, and dropping only its sub-second component,
each fail one model check; comparing only the device and inode numbers fails
four core checks and one model check; never reading the creation time fails two
core checks and one model check; transposing the major and minor device numbers
fails one core check; allowing unknown identities to match fails one core
check; and removing the uniqueness requirement crashes the model suite outright,
because that requirement is also what guarantees the subsequent search finds
something to dereference.

Known gaps. The `lstat` fallback taken when `statx` is unavailable has no
automated coverage, because `statx` cannot be made to fail from a test without
a seccomp filter or an older kernel; the device reassembly on the preferred
path is pinned against `lstat` instead, so it cannot pass by being consistently
wrong. Identity remains session-scoped and must not be persisted or compared
across a remount.

---

## 2026-08-03 -- Shared context-aware action system

Every user-facing action is now declared once, in a registry the whole shell
renders from. A declaration carries the action's identity, label, icon role,
destructive classification, applicable surfaces, enablement, key sequences,
and handler together; menus, toolbar and action-row buttons, the tab strip,
pane surfaces, and the shortcut table all consume the same declaration, so a
surface can no longer gate or word an action differently from the rest of the
shell. The previous structure had already demonstrated the failure mode this
removes: the entry menu, the keyboard menu, and the toolbar each carried their
own enablement expressions until a gate caught one drifting.

Enablement is computed from an immutable context snapshot — a frozen object
keyed by kind and target path or index — and never from the asking surface.
Live capabilities are read at evaluation rather than captured, and triggering
revalidates enablement before the handler runs, so a stale menu or a shortcut
on a contextually dead action is a safe no-op. Each pane, the tab strip, and
the inactive-pane placeholder host one shared menu instance parameterized per
invocation; keyboard invocations anchor it to the focused item, never a
pointer position. Destructive actions render last, after a separator, with
labels stating the exact target count. Disabled actions stay visible, render
disabled with an accessible reason, and are skipped by menu key navigation.
Blank-canvas menus open from a right press on the empty region below the
entries — mirroring the rubber-band area's geometry rather than restating
it — and from the menu key when no entry is current. Breadcrumb, Places, and
device surface kinds are declared with location actions ready for the
navigation chrome that consumes them, and the registry enumerates and filters
labeled actions for the command palette that lands next.

The shortcut table is instantiated from the declarations, one `Shortcut` per
declared sequence, with a numbered family (tab ordinals) declared as one
action carrying per-sequence arguments. A sequence declared by two actions is
a test failure. A dedicated suite covers registry mechanics, cross-surface
enablement equality, trigger revalidation, destructive ordering and counts,
context immutability, palette enumeration, and the shared menu's rendering,
live re-enablement, focus restoration, and per-context rebuild; the parity
and component suites assert the same declarations through the rendered shell
and the standalone chrome.

Verification covered warning-clean release and sanitizer builds, both full
test suites including the real-GPU presentation entry, C++ and QML
formatting, QML lint/module/runner-scope checks, public-repository and
file-length guards, and silent software and OpenGL RHI smoke launches.
Deliberate defects were planted and caught before restoration: a duplicated
key sequence failed the conflict case, an inverted enablement predicate
failed the action, component, and parity suites together, and a keyboard menu
re-anchored to a pointer-style position failed the anchoring case.

---

## 2026-08-03 -- Split the development record by calendar month

The development record is now a live file plus a dated archive. `DEVLOG.md`
keeps the entries for the current calendar month; entries for closed months
live in `docs/devlog/YYYY-MM.md` and are linked from the live file, most recent
first. July 2026 moved first, byte for byte: no entry was reworded, retitled,
re-dated, reordered, condensed, or dropped, and the concatenated result
reproduces the previous file exactly.

The split is preventive. A single record was approaching the 2,000-line
tracked-file ceiling, and that ceiling is enforced across the whole corpus, so
crossing it would have blocked every commit rather than only the one that grew
the file. Splitting on a calendar boundary keeps each part well inside the
limit without compressing published prose, which the ceiling rule forbids as an
evasion.

A new `devlog_archive_guard` holds the arrangement mechanically. It requires
every archive file to be linked from the live record and every archive link to
resolve, archive filenames to match the month of every entry they contain, live
entries to be newer than every archived month, and the archive list to stay in
reverse-chronological order. Its self-test builds throwaway repositories for
each of those failure modes plus an accepted layout, so the guard cannot pass
by never reaching its own checks. The file-length guard already enumerates the
tracked corpus rather than a fixed set of directories, so the new directory is
covered without changing it; that was confirmed by planting an oversized
tracked file under `docs/devlog/` and observing the guard fail.

Verification covered warning-clean release and sanitizer builds, both complete
test suites, scoped static analysis, C++ and QML formatting, QML lint, module,
and runner-scope checks, public-repository, file-length, and devlog-archive
guards, and silent headless smoke launches on the software and OpenGL RHI
backends.

---

## 2026-08-03 -- Add calm path orientation and direct navigation

The always-visible address field is replaced by a reusable path navigator that
keeps breadcrumbs calm during ordinary browsing. Ctrl+L and the Location
button summon the full editor; Escape or the Hide button returns to the calm
surface while retaining and advertising an unfinished draft. Absolute and
tilde-prefixed input is validated before navigation, invalid destinations
report through the existing status surface, and next-segment directory
completion is accepted with Tab or its visible pointer control.

The same component provides configurable Places and recent destinations.
Places can be added, removed, and reordered from pointer controls or keyboard
actions. Recents are newest-first, de-duplicated, bounded, and clearable from
both input modes. Breadcrumb ancestors, Places, and recents jump directly to
their stored path. Breadcrumb and Place context requests expose immutable path
data and focused anchors for the shared action registry without introducing
private menus.

Appearance and navigation preferences now share the existing versioned file.
Counted, percent-encoded Place and recent records preserve arbitrary UTF-8
labels and absolute paths, cap collection sizes, ignore duplicates, and fall
back safely when stored values are unknown, incomplete, or corrupt. Defaults
contain only the filesystem root; user- and machine-specific paths are learned
at runtime and no such values appear in tracked fixtures.

Verification covered warning-clean release and sanitizer builds, complete core,
adapter, component, and rendered-shell tests, scoped static analysis, C++ and
QML formatting, QML lint/module/runner-scope checks, repository-safety and
file-length guards, and silent software and OpenGL smoke launches.

## 2026-08-03 -- Close entry-interaction parity gaps

Context menus now use one shared popup per list or grid instead of constructing
a popup and five actions for every realized delegate. Secondary-button presses
select the target and open the shared menu immediately at the press point.
Menu-key and Shift+F10 requests preserve the existing selection and anchor the
same popup to the current row or cell, so keyboard placement no longer depends
on the last pointer position. Closing the popup still restores view focus.

Transfer modifiers now state both sides of the contract: Ctrl requests a copy
and Shift explicitly requests a move in list and grid gestures. A real
horizontal pointer gesture is covered from press through drag start and
release. Same-parent moves remain rejected as no-ops, while same-parent copies
select collision-safe automatic renaming and create a sibling duplicate. A
direct regular-directory activation case now complements the symlink-directory
coverage, and the breadcrumb binding documents its deliberate dependency on
the current path.

The rendered-shell parity suite is split along responsibility boundaries.
Pointer, operation, menu, drag, selection, appearance, and icon cases occupy
one populated leaf runner; keyboard navigation, type-ahead, tabs, and view
state occupy another. Shared synthetic model and window support lives outside
both runner scopes, keeping each test file comfortably below the source-length
ceiling without duplicating the harness.

Verification covered warning-clean release and sanitizer builds, both complete
test suites, scoped static analysis, C++ and QML formatting, QML lint/module/
runner-scope checks, public-repository and file-length guards, and silent
software and OpenGL RHI smoke launches.

---

## 2026-08-03 -- Reusable shell chrome components

The shell window carried its chrome inline: the toolbar, tab strip, action
row, status strip, pane placeholders, and the button and field styling all
lived as scene fragments inside one file, and the two directory-view
instantiations each repeated some twenty theme bindings. Surfaces still to
come — richer navigation, the shared action system, the command palette —
would have grown by copying those fragments again.

The chrome is now a set of reusable module components, each driven by the
semantic theme roles through a single bound theme object: a shared button and
text field, the translucent chrome-strip material, the navigation toolbar,
tab strip, action row, status strip, inactive-pane placeholder, and the
directory pane. The pane is the single site that maps theme roles onto the
directory views' granular color and font properties, so a scene binds one
theme instead of twenty values, and it adds no clip and no transform between
the views and the presentation layer — thumbnail well registration names the
grid as its only clipping viewport, and the mask layer's mapping assumes that
single clip level. The views' interiors, including the rubber-band gutter
geometry and grab ownership the design records as load-bearing, are unchanged.

A dedicated component suite instantiates every chrome component standalone
against a recording model stand-in and drives it through pointer and keyboard
paths, so a component that silently depends on the shell scene fails even
while the full-shell suites stay green. Deliberately broken wirings — a copy
action stripped of its selection gate, an inverted view toggle, a pane
component missing its view-mode binding — were each caught by the suite that
owns them and reverted.

The breadcrumb bar and entry context menu keep their granular color-property
interfaces for now; migrating them onto the shared theme object is deferred
work, not an accident.

Verified with the QML format and lint gates across the enlarged corpus, the
test-scope gate, release and ASan/UBSan builds and full suites including the
real-GPU presentation entry, static analysis held at its baseline, the
repository guards, and silent smoke launches on the software and OpenGL
scene-graph backends.

---

## 2026-08-03 -- Permit shell expansion syntax under the at-sign ban

The public-repository guard banned the at-sign in every tracked file, and shell
expansion syntax cannot be written without it. Two consecutive gate scripts had
therefore been written with index loops and star subscripts in place of the
array-at and positional forms. Nothing was broken by that: every element read
was quoted, and the star form is equivalent where only a count is taken. The
cost was that the guard steered contributors away from the one correct idiom
for arguments that contain spaces, which eventually produces a defect that
looks like a bug in the script rather than a consequence of the rule.

The ban is now lifted for the syntax rather than for the files that contain it.
Permitted: the array-at subscript inside a parameter expansion, the braced
positional form, and the bare positional form, which covers the quoted and
unquoted spellings because the quotes sit outside the token. Every other
at-sign in a shell file is still rejected, and the ban is unchanged everywhere
else.

Excluding whole files was considered and rejected. An excluded file is one the
guard stops reading, and the exclusion list would have grown by one entry per
script until the rule survived only in the comment describing it. The scan
instead removes the permitted forms from a line and reads what is left, so an
address sitting beside a legitimate expansion on the same line, or two lines
below one, is still reported.

The three scripts that had been written around the ban are restored to the
natural idiom, so the permitted forms are exercised by ordinary use rather than
only by tests.

The guard had no self-test. It has one now, covering both directions: the six
permitted forms are accepted, and an address is rejected on its own, beside an
expansion on one line, in a file that also contains permitted forms, and in a
non-shell file. Each scenario asserts the specific reason for rejection, so a
case cannot pass because an unrelated check happened to fire. The planted
address is composed at run time from its byte value, because a literal one in
the tracked self-test would have required exactly the file-level exclusion the
carve-out exists to avoid; the permitted forms in that file are written out
literally, so its own tracked text is part of what it demonstrates.

Verification: removing the shell scan, excusing a whole line, excusing a whole
file, and restoring the blanket ban each fail the self-test, in the first three
cases on the negative scenarios and in the last on the permitted forms.

---

## 2026-08-02 -- Make a new static-analysis diagnostic visible

The static-analysis gate passed while printing several hundred advisory
diagnostics. Zero of them were fatal, so the gate was doing its job by its own
definition, but a genuinely new diagnostic would have appeared as one unfamiliar
line among hundreds of unchanged ones. Analysis nobody can read is not a gate.

The check policy already separates the two kinds: `bugprone-*`, owning-memory,
no-malloc, and the clang analyzer are errors, and the rest are advice. The
advisory set is now held against a recorded baseline of one count per file and
check. The gate fails when a file gains a diagnostic it did not have, and
equally when it sheds one without the baseline being updated, so the recorded
set can only move downward and cannot quietly drift. Advisory output is
summarized to a single line; only movement is printed in full.

Counting is per file and check over deduplicated locations. A header is
re-analyzed by every translation unit that includes it, so counting raw
emissions would have made the baseline shift whenever an unrelated source
started including a header, and counting per line would have restated the
baseline on any edit above a recorded diagnostic.

One category is excluded rather than recorded, scoped to `app/tests`. A Qt test
case is a private slot invoked through the meta-object system, which cannot
invoke a static member function; converting one as
`readability-convert-member-functions-to-static` advises would remove the case
from the run while leaving the file compiling and the suite green. That is a
structural conflict, not noise, and the check stays enabled elsewhere — the ten
occurrences in application sources remain recorded. No category was disabled for
being loud: the remaining 255 advisory diagnostics stay visible in the baseline
as work not yet done.

All translation units are now analyzed before the gate reports, so one run lists
every fatal diagnostic instead of stopping at the first file that has one.

Verification: a planted advisory diagnostic is reported by file, check, and
count; a planted fatal diagnostic in an application source is reported while a
later core source is still analyzed in the same run, which is what demonstrates
the early stop is gone; a hand-edited baseline reproduces both the increased and
the no-longer-occurring directions.

## 2026-08-02 -- Give each QML test runner its own directory

Qt Quick Test scans its source directory recursively and offers no exclusion,
and the rendered-shell runner was pointed at the parent of two other runners'
directories. It therefore adopted their cases: the presentation suite and the
software-fallback suite executed a second time inside the shell test entry. The
cost was not only duplicated work. A planted presentation failure was reported
by the shell entry, so a presentation regression named a suite that does not
contain the fault and sent a reader to the wrong file.

The shell scenes move to `app/tests/shell`, giving every runner a leaf
directory that no other runner scans. The shell entry now runs its three own
suites; the presentation entries are the only owners of the presentation cases.
A planted failure is reported by the presentation entry alone, and the shell
entry passes.

The layout is checked rather than left to inspection, because both ways of
getting it wrong are invisible in a passing run. A nested scope duplicates
cases under the wrong entry, and a runner aimed at a directory holding no
`tst_*.qml` prints nothing and exits successfully, so its entry stays green
while covering nothing. `qml_test_scopes` reads the declared scopes and rejects
both, along with two runners sharing one directory and a scope that does not
exist. It also refuses to report success over a build file in which it found no
runner at all, since a parse that silently matches nothing is the same vacuous
pass in a different place — a defect the self-test caught in the check's own
pattern before it was registered.

Verification: the pre-fix scope is rejected by the new gate, which names both
nested directories independently; `qml_test_scopes_self_test` enforces eight
layouts; a planted presentation failure is attributed to the presentation entry
and, under the old scope, to the shell entry.

## 2026-08-02 -- Semantic typography and vector iconography

Victor Mono 1.561 now ships inside the static shell resource and is registered
before the QML engine starts. Regular, Italic, Bold, and Bold Italic cover the
shell's used styles without carrying unused display weights; the upstream
provenance and complete SIL Open Font License accompany the four OpenType
files. The bundled source is therefore independent of a system font install,
while System and Named sources keep their prior fixed-width fallback behavior.

Typography is expressed through content, chrome, path, caption, and long-form
roles instead of shared body and metadata sizes. Density and UI scale drive
each role, while font-source changes leave entry row and grid-cell geometry
fixed and bound the fallback reflow with real font metrics. Long-form dialog
and error copy uses a proportional system face, open leading, wrapping, a
bounded measure, and primary neutral ink whose contrast is checked across all
shipped palettes.

A shared vector component now supplies directory, file, symlink, navigation,
view, tab, selection, transfer, rename, delete, and open symbols to entry
delegates, toolbar actions, and context-menu actions. The paths use semantic
ink and one coordinate space at 1x and 2x; high contrast increases stroke
weight and promotes the ink. Normal dark-palette chrome stays below the Strong
profile's bright-pass threshold, preventing unexpected emission. The app
adapter exposes symlink identity explicitly so file-type icon selection does
not depend on a target's current state.

Verification covered the font resource registration, role and fallback
metrics, palette contrast, icon geometry and recoloring, entry type mapping,
input-parity surfaces, presentation behavior, release and sanitizer suites,
QML and source checks, public-repository and file-length guards, and software
and OpenGL RHI smoke launches.

---

## 2026-08-02 -- Record load-bearing invariants and deepen the lint scan

Three behaviors that existed only as working code are now stated as
requirements, because each looks removable to a reader who has not seen what it
costs to remove it.

`docs/DESIGN.md` records that an entry's kind describes the directory entry
itself and never its link target, that symlinks are therefore classified before
directories on purpose, and that any call site deciding whether an entry is
navigable must ask the resolved question instead of reading the kind as a proxy
for it. It also records the layout a pointer rubber-band depends on: a
permanent interactive gutter along each directory view's trailing edge,
delegates narrowed by exactly that width, delegate and gutter ranges adjacent
rather than overlapping so the gutter never covers a row, a press that claims
the pointer grab for the whole drag, and a press origin anchored in content
coordinates. Without the gutter a drag-rectangle cannot be started at all once
entries fill the viewport, and the loss does not show in a short directory.
These constraints apply to reusable components factored out of the current
views.

`docs/STACK.md` records why the tracked formatter settings leave property
normalization and import sorting off: declaration order carries meaning the
formatter cannot see, reordering is its least stable behavior across releases,
and enabling either setting rewrites every tracked scene at once for no
behavioral gain.

The lint gate's build-ordering check no longer caps how deeply it looks for
module manifests. The shell module already produces a second manifest one level
below its own, and a capped scan skipped it silently, leaving missing type
descriptions to surface as a lint import warning that named neither the
manifest nor the ordering. `qml_lint_order_self_test` now holds the check to
seven throwaway import roots: a built module, an unbuilt one, a manifest with
no trailing newline, a module declaring no type descriptions, a nested
manifest, a manifest four levels down, and an import root that does not exist.
Removing the scan fails four of them, restoring the depth cap fails two, and
dropping the unterminated-line continuation fails one.

The shell-load diagnostic's placeholder-location assertion moved to the case
that produces one. An absent module reports an error carrying no file and no
line, and it is that case the engine's aggregate error string decorates with a
placeholder position; the assertion sat instead on the located-error case,
where it could not fire.

Verification: release 30/30 including the new gate, ASan/UBSan 29/29 enabled,
warning-clean builds under `-Werror`, all guards green, and silent release and
sanitizer smoke launches on the software and OpenGL paths.

---

## 2026-08-02 -- Resolve hooks from any working directory

The hook path is now configured as an absolute path. Git resolves a relative
`core.hooksPath` against the current working directory rather than the
repository root, so the previous relative value found no hooks whenever work
happened in a linked worktree or a subdirectory: enforcement appeared
configured while nothing ran. A commit carrying a deliberately incorrect
identity was accepted in a linked worktree before this change and is rejected
after it.

`tools/install_hooks.sh` now resolves the main checkout through the common
Git directory, so the recorded path stays valid when the command is run from a
worktree that will later be removed. The hooks self-test gained four checks:
the configured path is absolute, each required hook is present and executable
at that path, and an incorrect identity is rejected from inside a real linked
worktree.

Verification: release 28/28, ASan/UBSan 27/27 enabled, warning-clean builds,
all guards green, and silent release smoke launch.

---

## 2026-08-02 -- Enforce owner-only commit attribution

The public-repository guard now requires the account-scoped GitHub no-reply
address -- the numeric account id, a plus sign, the account login, and the
GitHub no-reply mail domain -- for both author and committer on every commit,
requires the whole history to carry exactly one identity, and rejects
attribution trailers. The previous check accepted any
address ending in the no-reply domain, which admitted addresses built from a
project or role name; such an address is syntactically valid but resolves to
whichever unrelated account owns that login, so unrelated accounts could appear
as repository contributors.

Enforcement moved into the tracked tree. `tools/hooks/` holds `pre-commit`,
`commit-msg`, and `pre-push`; `tools/install_hooks.sh` points `core.hooksPath`
at them, which is required once per clone because hooks under `.git/` are never
cloned and a fresh checkout would otherwise have no enforcement. The hooks
derive the expected identity from the existing history rather than storing an
address, so no address enters tracked text. `commit-msg` rejects
`Co-Authored-By`, `Assisted-by`, `Generated-by`, `Created-by`, `Authored-by`,
`Signed-off-by`, and `On-behalf-of`; `pre-push` re-checks every outgoing commit
so history arriving by merge, cherry-pick, or rebase is covered.

`attribution_hooks_self_test` exercises the hooks against a throwaway
repository and asserts that an owner-attributed commit is accepted, that an
unscoped no-reply identity, a second account-scoped identity, and each trailer
are rejected, and that a trailer named inside a stripped comment line is
ignored. Verification: release 28/28 including the new gate, ASan/UBSan 27/27
enabled, warning-clean builds, all guards green, and silent release and
sanitizer smoke launches.

---

## 2026-08-01 -- Correct commit attribution metadata

Published `main` history was rewritten so every author and committer resolves
to the repository owner's verified GitHub no-reply identity. The rewrite
preserved all 64 file trees, commit messages, author and committer timestamps,
merge-parent topology, and application content; only attribution metadata and
the resulting commit identifiers changed. The remote tip moved from
`c49df3d` to `237cff7` with an exact force-with-lease check.

Verification repeated against the rewritten tree: all 27 release tests passed,
including static analysis and the public-repository guard; all 26 enabled
ASan/UBSan tests passed; release and sanitizer smoke launches remained alive
and silent. GitHub resolves the rewritten tip to the repository owner for both
author and committer and reports no unrelated contributor identity.

---

## 2026-08-01 -- Presentation hardening: viewport masking and the GPU gate

Two correctness holes and two gate holes in the presentation pipeline are
closed.

Protected-well mirrors now stay inside the viewport that clips their well. A
grid keeps cache-buffer delegates realized beyond its visible bounds, so a
loaded thumbnail could scroll out of the grid while staying registered; its
mask mirror followed the mapped position onto surrounding chrome, where the
composite exempted a drifting rectangular patch from bloom, scanlines, and
vignette. Each registration now names its clipping viewport and the mirror
intersects its mapped rectangle against it, collapsing to nothing outside.
An end-to-end GPU regression scrolls a registered well out of a clipped
viewport onto a bright chrome band and requires the frame to render
byte-identically with and without the registration; a geometry test pins the
clamped intersection exactly, and the rendered-scene test asserts a loaded
thumbnail's registration names the grid and dissolves when the view does.

Well registration became incremental. The previous mask rebuilt every mirror
delegate whenever the registered set changed — registering well N+1 recreated
all N existing mirrors, quadratic item churn across a directory load. Each
well now owns exactly one mirror object, created on registration and
destroyed on unregistration; a creation counter proves that registering one
well among sixty creates exactly one object and leaves every existing mirror
untouched. Records whose well was destroyed without unregistering collapse
immediately and are swept on the next registration.

The GPU frame comparisons are now part of the shipped gate. `ctest` runs the
presentation suite on the default backend, where the offscreen platform
selects the software scene graph and the frame tests skip; previously
deleting the protected-well exemption from the composite shader passed every
shipped test. A new gate entry probes for a usable OpenGL context and
re-executes the presentation suite under the forced OpenGL RHI scene graph
when one exists, exiting with the CTest skip code when it does not. The same
exemption deletion now fails `ctest` outright on any machine that can render
it, and skips cleanly — not silently passes — on machines that cannot.

The shader-failure latch is exercised rather than trusted. Both vertical
blur passes gained the failure handlers their horizontal twins already had,
and two gate tests point a live stage's shader at a missing `.qsb`, require
the latch to stand the pipeline down, and require the latched frame to be
byte-identical to the plain path. Removing the latch from the pipeline
availability condition was verified to fail both tests, and deleting the
viewport intersection was verified to fail both new masking tests.

Qt Shader Tools is now documented as a required build component in the
README and stack notes. The leak-checker suppression file gained
third-party entries for the one-time allocations that surface only when
the sanitizer build renders the real OpenGL scene graph. The GPU-driver
entries name one module per supported driver family — the NVIDIA
proprietary core library, the Mesa Gallium modules for AMD and Intel, the
Mesa software rasterizer, and the Mesa GLX and GL dispatch libraries — so
the sanitizer gate holds across vendors rather than only on the machine
that first measured it. The D-Bus entry names the exact message-loader
function inside libdbus rather than the whole client library, so an
allocation leaked by project code inside a D-Bus dispatch callback stays
fatal. A planted leak in project font-resolution code was verified to
fail both the ordinary sanitizer entries and the forced-GL entry with
these suppressions active.

The public-repository guard's narration check previously matched only
full process phrases, so a bare role word in a comment passed it. A new
case-insensitive pattern rejects bare process-role vocabulary at word
boundaries; words with legitimate engineering meanings — thread-pool
workers, the builder pattern, the C++ `operator` keyword — stay out of
the bare list and are matched only in phrase forms, and the guard was
verified to reject planted role text while passing every existing
operator overload. Two test comments and one gate-script comment were
rewritten to state their measured facts impersonally.

The mask registry ignores a failed mirror creation instead of recording
it, since the stale-entry sweep dereferences each record's mirror and one
null record would abort the sweep mid-loop. The mirror clamp's two
standing assumptions — a single clip level between well and mask layer,
and transform-free shell items — are now stated at the clamp site so
dual-pane or columns work revisits them deliberately.

Verified with the full gate set: formatting, scoped static analysis, QML
lint/format/module guards, warning-clean release build with 28 passing test
entries (GPU path included), ASan/UBSan build and tests, file-length and
public-repository guards, and silent headless smoke launches on both the
software and OpenGL RHI backends.

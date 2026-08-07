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


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

- [2026-08 part 5](docs/devlog/2026-08-part5.md)
- [2026-08 part 4](docs/devlog/2026-08-part4.md)
- [2026-08 part 3](docs/devlog/2026-08-part3.md)
- [2026-08 part 2](docs/devlog/2026-08-part2.md)
- [2026-08 part 1](docs/devlog/2026-08-part1.md)
- [2026-07](docs/devlog/2026-07.md)

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

---

## 2026-08-28 -- Two residuals in the category rules, and one exemption

The serial comma is optional in English, and omitting it defeated the
enumeration rule outright. Without the comma before the final conjunction the
last two names merge into a single item that begins with a lowercase word, so
it stops counting as a name and the run falls below the threshold. One
character, in the more common of the two styles. The conjunction is now a join
in the pattern and a boundary in the count, and both spellings are pinned.

Counting had to absorb that without inflating anything. A comma immediately
followed by the conjunction is two boundaries in a row and leaves an empty
field between them, and counting that field pushes every three-item list with a
serial comma over the four-item threshold — which is correct prose being
reported for its punctuation. Empty fields are skipped, and the case is a
fixture rather than a comment.

The second residual was a live published line. A decision attributed to a peer
implementation named by its standing, rather than by its name, passed every
rule: the existing phrase requires a desktop or terminal token in the middle,
and the line wraps between the qualifier and its noun, so the half that
survives on one line matches nothing. The qualifier now carries the rule on its
own. Calling an implementation established, mainstream, conventional,
comparable or popular is a claim about where it sits among others, and that
narrow set is the point — "other" and "another" are ordinary English here, used
in interoperability statements about what other software can read, in the
policy's own description of what it forbids, and in the license text. Widening
the noun phrase to admit them reports six lines of correct prose, three of them
in published entries. The narrow set reports exactly one line across the whole
corpus, and that line is the instance.

That line is in the archived record, where entries are never reworded, so the
rule would stand permanently red over text nobody may edit — and a permanently
red gate is one that gets deleted. The correction is published above as a new
dated entry stating the specification that actually fixes the escaping, and the
archived line is exempted by its exact text. The cost is recorded rather than
hidden: the exemption necessarily writes the refused phrase into the guard,
which is the one file the corpus scan excludes. Two floors keep it from
becoming a habit. It must still match something wherever the record exists, so
it cannot outlive its subject, and it is one exact line rather than a pattern,
so a longer line containing it is still reported.

Self-test 80 to 95 scenarios with the floor raised; 15 mutations planted and
diffed, 15 caught. Three first survived on fixtures that could not discriminate
— a merged final item is invisible when three capitalised names already meet
the threshold, and a record that reports something either way cannot tell an
exact exemption from a widened one. The fixtures were rewritten until each
mutation had a scenario that could only fail for it.

The enumeration gap that remains is unchanged and worth restating plainly: the
guard refuses the labelled-group idiom and the qualified enumeration, and it
does not refuse a prose enumeration of lowercase names. Loosening the item
shape to reach that form reports 484 lines across 105 files, so there is no
narrowed position to retreat to, and the rule stays where the measurement puts
it.

---

## 2026-08-28 -- What fixes the thumbnail cache file name

The thumbnail cache is a shared resource, and its file name is not an internal
choice. The freedesktop thumbnail specification defines the name as the MD5
digest of the source file's URI, and it fixes the URI itself: which bytes stand
literally and which are percent-encoded. That specification is the whole
authority for the escaping OdySea writes, and it is the reason the escaping is
what it is.

The consequence is why the detail carries weight. The name is the digest of
exactly those bytes, so an escaping that diverges anywhere produces a different
digest and therefore a different file name. The result is a cache no other
application can find, while every thumbnail already on disk stays invisible to
this one. Nothing fails: both sides do the work twice, hold duplicate copies,
and report success. That silence is what makes the byte sequence worth stating
precisely rather than approximately.

The trash specification is a separate specification with a separate purpose,
and its escaping is not interchangeable with this one — reusing it would encode
characters the thumbnail URI leaves literal, and the digest would move. The
literal set OdySea writes is the alphanumerics together with the punctuation the
thumbnail specification exempts, and the tests assert that set directly rather
than asserting agreement with any implementation of it.

Validity remains a separate question from naming. A digest carries no collision
guarantee, so a stored thumbnail is accepted only when the source URI recorded
inside it matches, the recorded modification time matches, and the file is
readable; a name alone is never treated as proof.

---

## 2026-08-28 -- QML-facing model roles use the type the registry understands

The fuzzy-find, Miller-columns, and storage-usage adapters exposed their item
roles through registered enums backed by a fixed-width standard-library type.
The runtime engine still resolved the enum keys and values, but the generated
QML type descriptions named a type the declarative registry could not resolve.
Those app-layer enums now use built-in `int`, matching the integer role API and
leaving every Qt-free core type unchanged.

A linked-module test imports the production QML module and reads the first and
last role values from all three types. A separate storage contract rejects a
non-`int` backing type, so the generated type descriptions cannot drift back to
an unresolved alias while runtime enum visibility continues to pass.

---

## 2026-08-28 -- Action-bar compaction measures its full row

The action bar now derives its labeled compact breakpoint from an independent
measurement row built from the same controls, margins, spacing, action labels,
and stretch reserve as the rendered row. That row stays labeled while the
rendered row is compact, so the compact state cannot lower the threshold that
selects it. Button implicit widths, density, and interface scale therefore
flow through the same layout contract rather than a separately maintained
width sum.

The reusable-component test checks Compact, Cozy, and Comfortable density: at
the measured width every labeled action remains within the bar, and one pixel
less selects compact chrome. The visual validation ran on the software scene
graph at logical scale 1x and with logical scale 2; these runs verify layout
only, not a device-resolution framebuffer.

---

## 2026-08-28 -- Accent presets keep file meaning stable

Tideglass, Beacon, Ember, Orchid, and Verdant now select a live shell accent.
Each preset persists a stable identifier while the appearance surface resolves
its display name, so a later label change does not disturb an existing stored
choice. The accent drives active chrome, focus, rubber-band selection, and
the existing bright-pass emission path; Full, Balanced, Minimal, and Off keep
their established effective-level behavior.

Directory, generic-file, symlink, error, warning, and success inks remain in
the semantic palette matrix. The controller test iterates its entire preset
model for every palette, so a newly added preset must preserve those roles.

The render-site contrast arithmetic now has one shared implementation. The
appearance gate and the accent-selection warning call that implementation,
which reports the specific surface below the indicator floor. The appearance
panel previews selection immediately, supports standard pointer and keyboard
paths, and reset returns the shipped Tideglass choice.

Core appearance persistence and controller tests passed. The appearance input
test passed on the software scene graph at logical scale 1x; it verifies live
control bindings and input paths, not device-resolution presentation.

---

## 2026-08-28 -- A group survey is refused by its shape, not by its framing

Every category rule shipped so far keys on framing: a survey sentence, a
placement verb, a crediting heading. Delete the framing and keep the labelled
groups and the names under them, and the same claim is made in fewer words and
passes every rule. That was measured directly against the remediated corpus:
two labelled groups, seven names, a stated weakness for each, appended to the
design overview and staged, accepted at exit zero with the summary line
unchanged. The framing is the part a writer drops; the list is the part they
keep.

Two rules now read the shape of the list itself, and neither knows a name. The
first is the label. A list item introduced by a class of this project's own
category — qualified or plural — divides a field into groups, and there is no
other reason to write it, because every legitimate sentence here is about this
one program. Singular self-reference is therefore permitted and only the
qualified or plural form is refused, and the label must be closed by emphasis
marks or by a colon or dash so that a sentence running through the same words
is not read as a heading.

The second is the enumeration: three or more comma-separated names on one line,
inside a block that also states a limitation. The limitation usually sits on a
later line of the same item, so the scan is block-scoped, and a block is broken
by a blank line, a heading, another list item, or the end of a file. The list
must also be the whole clause — closed by a sentence break or the end of the
line — because a survey names its group and stops, while correct prose names
three inputs and then says what they do.

The thresholds were measured against the tracked corpus rather than chosen.
Letting an item be any lowercase word reported five lines of correct prose, two
of them in published record entries that can no longer be edited. Requiring
three items with two of them capitalised still reported the design overview's
own input list. Three capitalised items, or four items of which at least two
are capitalised, reports nothing across the corpus and reports both halves of
the enumeration that prompted the rule. Both rules also fire independently on
the historical text this project has since rewritten: three of its items by the
label, two by the enumeration, before any framing rule is consulted.

The block scanner runs on its own with its exit status read, and reports how
many lines it examined, because a pattern that fails to compile prints nothing
and nothing is indistinguishable from a clean corpus. Self-test 60 to 80
scenarios with the floor raised alongside; 21 mutations planted and diffed, 20
caught by name, 1 equivalent and recorded. Six of those mutations first survived
because the fixture that should have caught them carried a list the rule never
matched, so the condition under test was never reached; the fixtures were
rewritten to carry a matching list, not the rule relaxed to reach them.

Every planted name in the self-test is invented: a real one written into the
file whose purpose is to suppress it would publish exactly what the rule exists
to keep out.

---

## 2026-08-28 -- The public record describes this project in its own terms

The design overview opened with a survey of a field, a named list of projects in
it, a stated weakness for each group, and this project placed in the gap between
two of them. The readme carried the same argument with the names removed. Four
shipped source comments and one test name justified a decision by what other
software does. All of it has been tracked since the first commit.

The category rules added alongside this change passed every line of it, because
none of that text carries a hosted-source reference, a derivation phrase, or a
crediting heading. That was recorded as the rules' stated gap; it turned out to
be a live one.

What replaces it says the same useful things without the comparison. The design
overview now opens with what OdySea is — local and immediate, GPU-rendered,
keyboard-complete, and integrated through the freedesktop specifications rather
than through any one desktop's conventions — followed by scope boundaries stated
as scope rather than as a rebuttal. The readme describes what the application is
for. The thumbnail comments now cite the authority that actually binds them: the
cache file name is the digest of an exact byte sequence, so the escaping is
fixed by the shared cache key, and a divergence produces a private cache nothing
else on the system can read. That is a stronger statement than the one it
replaced, because it says why the bytes are what they are.

Two rules were added so the structure cannot return. The enumerated peer group —
a qualifier such as "most" or "existing" in front of the project category — is
refused, while interoperability prose about what other software can read is not,
because the shared cache makes that a required subject. And a positioning
argument is refused by its structure: a field noun attached to this project's
category, a placement into a gap, the two-poles-and-a-middle construction, and a
section heading whose subject is placement. Written without a single name, that
paragraph passes every other rule, and it makes the same claim.

Self-test 51 to 59 scenarios, floor raised with it; 26 mutations planted and
diffed, 25 caught, 1 equivalent and recorded. Two limits stand. A bare name with
none of these structures around it is still undetected, which is the cost of
enforcing a category rather than a list. And the tip is not the history: the
removed text remains in every commit that published it, and in the archived
record, where entries are immutable by rule.

---

## 2026-08-28 -- Tracked text describes this project and no other

Tracked text states what OdySea does, why, how it was verified, and where it
falls short. It does not identify another project of the same kind, and it does
not present a decision as taken from, measured against, or in contest with one.
Upstream dependencies are the opposite case: a toolkit, a build system, a
compiler, a bundled typeface and its license are named as dependencies because
a reader needs to know what the build requires.

`public_repository_guard` now enforces that category. The rule it enforces is
the category itself, never a list of instances. A tracked deny-list of names
would publish those names in the file written to suppress them, and would also
disclose that the suppression exists; hashing the list fixes neither, because a
short known name falls to a wordlist and the list's presence is itself the
signal. The only name the guard carries is this repository's own forge owner,
which is the one owner a tracked hosted-source reference may name.

Three rules. A hosted-source reference is a forge host followed by a separator
and an owner; any owner but this repository's own is refused. Derivative and
rivalry framing is a curated phrase set, each phrase stating a relationship to
another project rather than a property of this one. A section heading whose
subject is crediting or surveying other work is refused; `Attribution` is
deliberately outside that set, because commit attribution is a policy this
repository documents under that name.

The tuning matters more than the patterns, because a check that reports correct
prose is one contributors learn to route around. Three candidate rules were
measured against the tracked corpus and cut back:

- A bare two-component path-shaped token was dropped entirely. The corpus holds
  eighty-three distinct ones, and they are overwhelmingly real paths and
  either-or prose: build directory names, a branch and its remote, a pair of
  keys, a pair of view modes. Nothing distinguishes those from an owner and a
  repository, so nothing is matched. The hosted-source forms are.
- A weak comparative is matched only in the construction that makes it a
  comparison, with the comparative word immediately governing a set this
  project is being placed within or against. On its own it is ordinary
  engineering prose: a watch is incremental rather than polling, a load is
  quicker than the linear scan it replaced.
- The category noun is not matched at all on its own. Thumbnail cache
  interoperability is defined by what the surrounding desktop produces, and
  saying so is a specification statement that the record and two core sources
  already make.

Two scoping decisions are stated rather than left implicit. The vendored
dependency tree is excluded, because the files there are an upstream's own
provenance and license text reproduced verbatim as its terms require; that is a
path scope, and the identical text one directory outside it is still refused.
And the search is line-oriented, so a phrase broken across a wrapped line is
not matched — a gap in the pattern rather than in the rule.

The own-owner filter earned a floor of its own. Its first form used a delimiter
that collides with the alternation inside the expression it carries. The filter
failed to compile, printed nothing, and the surrounding pipeline ended in a
tolerated failure, so a corpus that had never been judged reported as clean.
The filter now runs on its own with its exit status read, and a filter that
cannot compile fails the guard by name instead of passing silently.

The self-test grew from twenty-five scenarios to fifty-one and gained a floor
on its own count, so a scenario that stops reporting fails the suite by name
rather than leaving the rest green. Every planted violation is assembled from
adjacent string literals at run time, for the same reason the at-sign and the
conflict markers are: the file is scanned by the guard like any other tracked
source, and a literal violation in it would need exactly the file-level
exclusion this guard is built to avoid. The strongest accepting scenario copies
the real tracked files that cite dependencies — the readme, the contributor
guide, the stack notes, and the bundled typeface's license and provenance —
into a throwaway repository and requires the guard to accept them unchanged. A
paraphrase would only have proved that the rules accept a paraphrase, so the
fixture carries a floor on how many of those files it found.

Twenty mutations were planted, each diffed before it ran so a failed edit could
not read as a survivor. Nineteen were caught, naming the scenario that caught
them; one survived and is equivalent. Removing the filter's failure floor while
the filter still compiles changes nothing observable, because the floor is only
reachable together with an expression that fails — the paired mutation, which
breaks both, is caught. The rest cover each rule silently disabled, each rule
widened until it reports correct prose, the owner constant emptied, the
separator loosened until a bare host reads as a repository, the path scope
turned into a name pardon, a scenario stopped, and the dependency fixture
losing a file.

The known gap is deliberate. The guard enforces a category, so a name that
appears without a hosted-source reference, without derivative framing, and
without a crediting heading is not detected. Closing that would mean tracking
the names, which is the outcome the rule exists to prevent.

---

## 2026-08-28 -- Give the live development record room before the ceiling

The live development record was approaching its tracked-file line ceiling. Its
oldest consecutive stretch now lives verbatim in
`docs/devlog/2026-08-part4.md`, while the live file retains the newest entries
with enough space for continued milestone records.

Reading order remains the live record followed by archive parts from newest to
oldest. The manifest keeps the same sequence for every moved heading and adds
this entry at the front. History-derived validation continues to associate each
moved heading with the commit that originally published it.

## 2026-08-28 -- A bare refusal line cannot turn a GPU gap green

The battery reconciler previously trusted a registry entry's `refusal`
tolerance and a syntactically valid `DECL -- declined` line independently. An
RHI entry that could not create an OpenGL context could therefore be relabelled
as a refusal and print that line, turning the same missing GPU coverage into an
accounted pass.

A refusal now needs a matched pair from the entry's captured output: its
declaration and an `REFUSAL-PROOF` line from the isolated-compositor interlock,
with the same gate name. The compositor launchers emit that proof when the
interlock rejects a session before renderer setup; the platform-selection
contract checks it. A GPU launcher that cannot create a context never reaches
that policy boundary, so changing only its registry tolerance and output leaves
an unproven refusal that fails the reconciler.

Capability failures also compare the declaration's precondition with the
entry's captured first line. No shared significant token produces a visible
warning that the declared precondition does not resemble what the entry
reported. This is deliberately partial: token overlap can expose an unrelated
pointer, but cannot establish that two descriptions mean the same thing.

The reconciler self-test now has 30 scenarios. It rejects the bare-RHI refusal
fixture and a compositor proof copied under another gate name, while a matched
interlock refusal still passes. It also checks the partial precondition warning
and retains the foreign skip-code rejection.

---

## 2026-08-28 -- The static-analysis baseline records what was said, not only how often

The advisory half of static analysis was held to a recorded count per file and
check. A count cannot tell a diagnostic that was fixed from one that was
replaced, and the two together are indistinguishable from no change at all.
Demonstrated rather than argued: in `app/src/thumbnail_backend.cpp`, whose
recorded entries are three `readability-math-missing-parentheses` and two
`cppcoreguidelines-pro-bounds-pointer-arithmetic`, parenthesizing the megabyte
rounding in one function and writing `rowBytes + height * bytesPerPixel` in
another leaves both counts exactly where they were. The gate's output was
byte-identical to a clean tree -- the same 114 diagnostics across the same 74
entries -- with a precedence defect in the file it was watching. The first
edit is a correct tidy-up on its own, which is what makes the pair a plausible
accident rather than an attack.

Each recorded entry now carries a digest of that entry's diagnostic text
alongside its count, and an entry whose count holds while its text moves is
reported as such, naming both digests. The digest is taken over message text
only. Messages carry no line or column, so the property the per-file-and-check
key was chosen for is kept: moving a diagnostic down its file still does not
restate the baseline, and a reader is not trained to regenerate the baseline on
sight, which is how a real swap gets waved through.

Collapsing duplicate reports needed care that was not obvious beforehand. A
header is re-analysed by every translation unit that includes it, and one
location can be worded in more than one way: a unit that sees only a
destructor's declaration reports a class as defining "a destructor", a unit
that sees its definition reports it as defining "a non-default destructor".
Putting the message into the identity that collapses duplicates would have made
that header's count depend on which unrelated sources include it -- the exact
instability the collapse exists to prevent, and it showed up as the recorded
total moving from 114 to 115. A location is one occurrence, and every wording
observed at it is carried into the digest together.

Sorting is pinned to the C collation order everywhere the digest depends on it.
The digest is committed and has to reproduce on another machine, and default
collation orders punctuation differently between locales, so an unpinned sort
would have made a recorded value a property of the checkout's environment.

Three floors sit under the mechanism, because a digest that stops being
compared fails the same way a counter that stops counting does. The digesting
tool is required rather than optional, since an empty digest compares equal to
every other empty digest. A measured entry whose digest is not well-formed
fails the run instead of being compared. A recorded entry without a digest --
the shape of every baseline written before this change -- is refused with the
command that regenerates it, rather than being read as having no digest to
disagree with.

The gate's self-test grows from 13 scenarios to 19. The load-bearing one
records a baseline over a file holding two occurrences of one check, then
analyses a file where one of them has been fixed and another introduced: same
count, same entry, different text. Two more require it to be reported as
changed text rather than as one entry arriving and another leaving, and one
requires a diagnostic that moved down its file to still be accepted.

Known limit, and it is a real one: two occurrences of one check in one file
whose messages are byte-identical remain interchangeable, because outside their
locations nothing distinguishes them. Closing that would mean recording a line
or column and giving up insensitivity to edits above a diagnostic. The fatal
tier is unaffected either way -- `bugprone-*`, the clang analyzer,
owning-memory, no-malloc and `modernize-use-scoped-lock` fail the translation
unit outright and never reach this comparison.

Verified by reproducing the substitution on the unmodified gate and confirming
it passed, then confirming the same payload is rejected by the new one; by the
19-scenario self-test with a floor on its own reported count; and by the
release and sanitizer batteries over the regenerated baseline.

---

## 2026-08-27 -- A heading's date is checked against its commit

The record's reading-order rule compares entries against each other using the
dates the headings carry, so it cannot see a wrong date: a record where every
entry is misdated by the same day is perfectly ordered. That hole was not
hypothetical. Twelve of this record's entries were published carrying a date
other than the commit that added them, and the shape recurs -- an entry written
near midnight is dated in UTC while the commit is recorded in local time, so the
heading runs a day ahead of its commit. It had been corrected by hand rather
than by a rule.

`devlog_archive_guard` now compares each heading's date against the date of the
commit that first added that heading. The comparison uses the author date
rendered in the commit's own recorded timezone, so it does not change meaning
when a different reader runs it, and the walk takes the oldest addition rather
than the newest, so an entry removed and restored, or moved into an archive, is
still dated by the commit that published it.

This check reads local history, which is the opposite of every other history
check in the guard and is the point. The others read `origin/main` because it is
the one ref the commit under test cannot have written. A misdated entry, though,
is fixable only while it is unpushed: published entries are never edited, so a
check that waited for the published branch would first fire on text it is
already too late to correct and would then stay red forever over an entry nobody
may touch. A permanently red gate over unfixable text is a gate that gets
deleted. Reading `HEAD` reports the mistake while amending is still allowed.

It is bounded by a baseline entry, for the reason the ordering rule is bounded:
the twelve existing disagreements stay visible in the record rather than being
rewritten out of it. Fifteen entries sit at or above the baseline and all
fifteen agree with their commits. An entry that is in no commit yet is named as
unchecked rather than counted as agreeing, and a walk that compared nothing at
all fails rather than reporting success over an unchecked record.

Verified in both directions. The self-test grows from 50 to 59 scenarios, with a
floor on its own reported count. Eight mutations were planted, each diffed to
confirm the edit landed before it ran, and each was caught by its own scenarios:
the comparison removed, the comparison made one-sided, the walk keeping the
newest addition instead of the oldest, the baseline bound dropped, an uncommitted
entry silently treated as agreeing, the nothing-compared floor removed, an absent
baseline comparing the whole record, and the commit date re-rendered in the
reader's timezone. Two of the eight first survived against fixtures that could
not tell the difference, and the fixtures that discriminate were added rather
than the mutations being reported as equivalent.

Known gaps, stated: a clock that was wrong at commit time satisfies the rule,
because both sides of the comparison are things that commit asserts; nothing
below the baseline is checked; and the timezone mutation is detected only where
the reader's offset differs from the fixture's, though the rule itself holds
everywhere.

---

## 2026-08-27 -- Visual acceptance distinguishes scaling from device resolution

The visual-foundation matrix now names only the rendering it performs. The
offscreen software entry with `QT_SCALE_FACTOR=2` is called the scaled-layout
entry: it exercises logical geometry under doubled scaling while its surface
still rasterizes at one device pixel per logical pixel. It is not a 2x frame.
Genuine device-resolution 2x remains unmeasured until a declared, isolated
compositor can allocate that surface, so the roadmap acceptance item is open.

Everything that can be measured without a compositor is asserted on the 1x
software path. The layout suite sweeps the narrowest supported window, a wide
window, every density's live compact breakpoint, and interface-scale extremes.
Pointer clicks must focus a button, field, and directory view and expose their
effect-independent focus indicators; a separate keyboard traversal must cycle
through several surfaces. Reduced motion must zero both effective persistence
and the shared motion duration without moving chrome. The Off profile must
zero every effective presentation level while selection, text, and keyboard
actions remain usable. The contrast matrix measures the beds each semantic role
actually paints. The software fallback keeps the pipeline disengaged while
content, controls, and protected wells remain intact. A 2,000-entry listing
must keep both views virtualized and presentation structure viewport-bounded.

Each measured axis was checked against an isolated fault. Disabling compact
mode failed the narrow-layout test; freezing the measured width failed the
scale test; removing click focus and trapping the tab chain failed the pointer
and keyboard focus tests independently; bypassing the reduced-motion effective
value failed the motion test; leaving one Off-profile effect nonzero failed the
effects-off test; removing the high-contrast danger variant failed the measured
contrast matrix; allowing the pipeline on the software backend failed the
fallback test; and expanding the list cache to the whole fixture failed the
large-directory virtualization test. Each fault was removed before the clean
run. The full suites invoked the guarded compositor launchers; both declined at
the declaration boundary before Qt created an application. No compositor was
started and no interactive session was contacted.

The warning-clean release suite passed all 72 registered entries at CTest
level, including static analysis. The ASan/UBSan suite passed all 71 enabled
entries with no sanitizer diagnostics; static analysis is disabled in that
preset. Both suites recorded four skips: the two compositor policy declines
above and two offscreen-RHI capability skips because no display server was
advertised. The coverage reconciler accepted the policy declines and rejected
both batteries because those two capability skips lack a declaration. That
separate gate-registration gap remains visible rather than being converted to
green validation evidence.

---

## 2026-08-27 -- Which battery entries may skip is declared, not discovered

The coverage reconciler judged every skip after it happened, which left the
mechanism itself unwatched. An entry could be given `SKIP_RETURN_CODE` in the
build configuration and the reconciler would meet that fact for the first time
on the run where the entry dropped out -- and if that skip printed the refusal
line, it was tolerated from then on without anyone having decided it should be.
An honest skip and a completed check read identically in a summary, so the
defect was never the skip mechanism; it was a skip the reconciler did not know
existed.

The roster is now taken from `ctest --show-only=json-v1`, which reports
per-entry properties, so each line carries its skip return code beside its name.
`tools/skip_declarations.txt` records every entry allowed to have one, and the
two are reconciled before a single result is read. An entry that can skip
without a declaration fails. A declaration naming no registered entry fails. A
declaration naming a registered entry that carries no skip return code fails, so
a declaration cannot outlive its mechanism. A skip return code other than the
project's own fails, so no second convention can appear beside the declared one.

A declaration records one of two tolerances. `refusal` is the shortfall the
reconciler already tolerated: a policy this project enforces, printed as
`<gate-name>: DECL -- declined: <reason>`, which the two compositor entries use
to decline a session they were not given to own. `capability` records a
precondition the entry cannot satisfy itself -- an offscreen OpenGL context, a
JSON-capable interpreter -- and a skip under it FAILS, with the recorded
precondition printed beside the failure so the reader is told what to provide.
The skip return code still earns its place there: the entry reports as not-run
rather than as a broken test, and the reconciler is what turns the totals red,
which is where every other coverage hole in this battery is already decided.

A fifth class comes free. An entry that did not run and declares no skip
capability at all now fails by that name, which covers the skips CTest itself
invents -- a missing executable, an unsatisfied dependency -- and which no
output-shaped rule could see.

The practical effect on the two offscreen GPU entries is stated rather than
softened. Without a display server they are a declared capability failure naming
the context they need, instead of an unexplained shortfall that a reader had to
be told to expect. The answer remains a virtual display, not a wider tolerance.

Verified by 26 self-test scenarios, up from 13, with the suite failing by name
when it reports fewer results than it contains. The load-bearing scenario is the
one where every result is clean -- nothing skipped, nothing missing, nothing
unexpected -- and the reconciliation fails anyway because an entry was given the
ability to skip and nobody wrote it down. Eleven mutations were planted against
the reconciler, each diffed to confirm the edit landed before it ran, and each
was caught by its own scenarios: the four static checks removed one at a time,
an undeclared entry treated as a declared refusal, an unmet precondition
tolerated, a missing declarations file read as nothing declared, a malformed
line skipped over, an unknown tolerance accepted, a duplicate declaration
accepted, and the roster's skip-code column ignored. A twelfth mutation dropping
a self-test scenario is caught by the reported-count floor.

What this still cannot do is judge whether a recorded precondition is the true
one. The registry's text is printed with a failure, not tested against it.

---

## 2026-08-27 -- The advisory static-analysis set names only what can be acted on

The recorded advisory set had grown to 297 diagnostics across 103 file-and-check
entries. The ratchet still reported drift correctly, but a reader meeting three
hundred permanent lines learns to scroll past the recorded set, and that is the
same motion that scrolls past a new one. Three categories accounted for 183 of
the 297, and each is answered differently.

`modernize-use-scoped-lock` is now fixed rather than recorded. All 55
occurrences were single-mutex `std::lock_guard` declarations across eight files
in `core/`, `app/`, and both test suites; `std::scoped_lock` is a strict
superset, so no call site had a reason to keep the older spelling. The check is
promoted to `WarningsAsErrors`, which is what stops the tree drifting back one
advisory line at a time.

`cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` is disabled with
its reasoning recorded in `.clang-tidy`. It requires a checked accessor for
every subscript; this project subscripts after the index has been established by
the code that produced it, and in paths where a second bounds check per element
is real cost. The defect class it describes is covered by controls that do not
depend on a style rule: the whole bugprone family and clang's own analyzer are
errors here rather than advisories, and the entire suite runs under
AddressSanitizer, which reports an out-of-bounds access where it happens.

`readability-function-cognitive-complexity` is disabled for test translation
units alone, through a directory-scoped configuration beside the existing one in
`app/tests/`. The metric measures branching and nesting; a test function is
meant to be a long straight line of arrange, act, and assert. Sixty-seven of its
seventy-four occurrences were test functions, and they were why the seven naming
real project code were hard to see. It stays enabled for `app/` and `core/`.

The recorded set is now 114 diagnostics across 74 entries, and every one of them
sits in code where the advice can be taken. Verified in both directions: the
full release build is warning-clean after the lock refactor, the suite is green,
and a planted `std::lock_guard` in `core/src/thumbnail_service.cpp` makes
`static_analysis` fail as an error naming the file and the check, where before
the promotion it would have been absorbed into an advisory count.

Known gap: the ratchet's identity is still the count per file and check, so a
diagnostic removed and another of the same check added in the same file leaves
the count unchanged and is not reported. Reducing the set shrinks that window
but does not close it.

---

## 2026-08-27 -- Conflict markers honour Git's configured size

Git's default conflict marker is seven characters, but its marker size is
configurable. The earlier record's claim that Git emits exactly seven
characters was wrong. Both record-protection guards now recognize every marker
run of seven or more characters: the corpus guard refuses it before it can be
published, and the archive guard removes it from a previously published body
before comparing that body against history. The two rules move together so a
non-default marker cannot enter the record and then make its own repair look
like a rewrite.

The archive parser accepts a space or tab before a marker label, or the end of
the line, and its whitespace set is a strict subset of the corpus guard's. It
drops an entire marker line, including a label, so that relation is
load-bearing: the broader corpus ban is what prevents label text from becoming
invisible to the history comparison. The corpus check retains its line-start
anchor and no file-level exclusions for Git-text files. As with any text scan,
Git-binary files and a first-line UTF-8 BOM sit outside that anchor's reach;
neither form is in the tracked corpus.

Verification adds a complete nine-character conflict region to the corpus
guard self-test and a nine-character published-record repair to the archive
guard self-test. Mutating either guard back to an exact seven-character run
makes its respective new case fail while the other guard remains unchanged.

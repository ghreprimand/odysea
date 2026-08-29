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

- [2026-08 part 4](docs/devlog/2026-08-part4.md)
- [2026-08 part 3](docs/devlog/2026-08-part3.md)
- [2026-08 part 2](docs/devlog/2026-08-part2.md)
- [2026-08 part 1](docs/devlog/2026-08-part1.md)
- [2026-07](docs/devlog/2026-07.md)

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

---

## 2026-08-26 -- Ambient display variables cannot select a compositor gate

The real-compositor launcher now has a named, non-rendering contract around
the platform boundary its session interlock enforces. `DISPLAY`,
`WAYLAND_DISPLAY`, and `QT_QPA_PLATFORM` describe ambient sessions; none of
them authorises a platform choice. Only a proved isolated-compositor
declaration may reach the OpenGL probe or the presentation suite.

The contract launches the real binary against three deliberately unreachable
environments: X11 alone, Wayland alone, and both together. Each run must exit
with the declaration-refusal status and message before Qt creates an
application. A capability skip, failure, or run message is rejected even when
its process status is also 77, so the old environment-selected path cannot be
mistaken for the required refusal. The endpoints are synthetic and absent;
the check cannot contact an interactive session.

The durable platform matrix now describes the shipped boundary rather than
the launcher's first form: isolated Wayland only, no ambient X11 fallback, and
`ODYSEA_REQUIRE_COMPOSITOR` able to turn a capability failure red without
overriding a policy refusal.

Verified in both directions. The contract passes all three named cases with
the interlock intact. A planted platform-selection branch before the
declaration check makes `ambient_x11`, `ambient_wayland`, and `ambient_mixed`
all fail by name. The columns ownership gate was also rechecked on the current
tree: removing `QQmlEngine::setObjectOwnership` makes
`shell_load_diagnostic` fail at
`anInvokableColumnListingRemainsCppOwned`, observing JavaScript ownership
where C++ ownership is required; restoring it returns the gate to green. No
real-compositor entry was invoked.

---

## 2026-08-26 -- The record's history bound may not shrink or be skipped

Two ways the archive gate's history comparison stopped bounding the record, one
by getting smaller and one by not running at all. Both were reproduced on the
unmodified tree before anything was written.

The comparison demands every entry the walk found, so the size of the walk is
the whole strength of it, and nothing measured that size. Pointing a clone's
`origin/main` at a commit five entries back — the state a fetch that never ran
leaves behind — dropped the bound from 112 published entries to 107 at exit
zero, and an entry published in that gap could then be deleted from both the
record and the manifest without complaint. The only trace was a number on the
success line that nothing compared against anything. The bound now carries a
floor: a reading below a count this record has already published is refused,
because published entries are never removed and that number can only grow. The
floor is conditioned on the oldest published entry so that it is a claim about
this record and not about every record the script is pointed at, and the anchor
cannot go quietly missing — it sits in an archive file, so a walk that stopped
finding it leaves it archived and never published, which is already refused by
name.

Naming a baseline was also optional. The candidate refs cover every ordinary
clone, but they are names, and history does not depend on names: renaming the
branch and the remote and then checking out an unborn branch leaves every
commit reachable under other refs while none of the candidates resolves. In
that state a full clone carrying 168 commits reported the bound unchecked on
standard output, exited zero, and accepted a record with a published entry
deleted from it. A repository that holds commits and cannot name a baseline now
fails. Reporting the bound unchecked remains available only where there is
genuinely nothing to read — a source tree with no repository, or a repository
with no commit yet — because an honest skip and a completed check otherwise
read identically in a summary.

Verification: the self-test grew from 44 scenarios to 49 and gained a floor on
its own reported count, so a suite that stops early fails instead of passing
short. The new scenarios put a record on each side of the count floor, hold the
guard to saying which side it found, pin the keeper that stops the floor going
inert, and drive a repository into the unnameable-baseline state with an intact
record so that only the refusal is under test. Eleven planted mutations were
each asserted to have changed the file before being run; all eleven were caught,
none survived, and none went unmeasured. Both reproductions above now fail by
name, and the shipped record stays green at 112 of 112 compared.

---

## 2026-08-22 -- Repair a record published with its conflict unresolved

The record went out with nine unresolved merge-conflict markers in it, left by
a three-way integration of three entries that each added text at the top of the
file. No entry was lost and no entry's prose was touched -- the markers fell
between entries -- but the top of the published record read as a conflicted
file.

Every gate that exists to protect the record passed it. The archive gate
compares each entry's heading and body against the form the published branch
last carried, and the markers changed neither, so it agreed. Worse, once the
damage was published it became the form every later comparison was made
against: the gate had adopted the break as its baseline and would have refused
the repair. The corpus guard scanned the same file for secrets, private paths
and address syntax, and had no pattern for a marker at all.

Two changes, in opposite directions. The corpus guard now refuses any tracked
line that begins with a conflict marker, so a commit carrying one is rejected
before it can be made rather than discovered after it is published. The pattern
covers the base marker the diff3 and zdiff3 styles add as well as the two sides
every style writes, requires exactly the seven characters Git emits followed by
a space or the end of the line, and is anchored at the start of a line. The
anchor is what lets the check carry no exclusion list: a marker composed inside
a `printf` argument does not begin its line, so the guard, its own self-test,
and any fixture that has to build one are all still scannable by it. A rule of
dashes or equals signs in prose is unaffected.

The archive gate now drops marker lines from an entry's body wherever they
appear, before comparing. This is narrow on purpose and is not the same
exemption as the one already made for trailing separators: a marker is not text
an entry can legitimately be published with, so it must not become part of the
form the entry is judged against. Only lines that are entirely a marker are
removed, so the surrounding prose is still compared byte for byte and a genuine
rewrite is still refused. The pair means a marker cannot enter the record and
removing one is always permitted.

Repairing the record itself removed the nine marker lines and restored the
blank line each had displaced. All thirty entries were checked against the form
the branch published, and against the individual commits the three conflicting
entries came from, before the change was made: every one is byte-identical.
Three further headings elsewhere in the record are not preceded by a blank
line. They predate this and were left alone, because widening a repair to
tidy published text is how published text stops being reliable.

Verified: the corpus guard refuses the exact nine markers the record carried
and accepts the repaired file; twelve new self-test scenarios cover all four
marker spellings, a full conflicted region, a marker in a source file, and five
near-miss forms that must still be accepted; five mutations of the pattern were
planted and all five were caught, each by its own named case. The archive gate
accepts the marker removal and still refuses a repair that also rewords an
entry under an unchanged heading, and removing either the marker rule or one
spelling from it turns the accepting case red.

## 2026-08-22 -- A hard link defeated the socket interlock

The interlock that keeps compositor-dependent gates off a session in use
refused a symbolic link to the login session's socket and accepted a hard link
to it.

`lstat` was doing the work of ruling out a link, and it rules out only the
symbolic kind. A hard link is the socket itself under a second name, so `lstat`
reports a socket, not a link. The run directory defaults to the same filesystem
the session's socket lives on, which is what makes the link constructible: the
session socket could be placed inside a directory that genuinely was the run
directory, beside a token that genuinely matched and a lock a live process
genuinely held. Every condition passed on its own terms, and the accepted
socket was, by inode, the compositor on screen. The one property the interlock
claims to have -- that no declaration resolves onto the login session -- was
false.

A socket created by `bind` has exactly one link, so requiring a link count of
one separates the two, and the field is already present in the `lstat` the
check performs. It is preferred over comparing the declared socket's inode
against the session socket's, which would answer only for the one socket it
names while this holds for any socket reached by a second name. The check errs
toward refusal: linking a genuine harness socket would also raise its count and
refuse the run, which costs a gate that declines rather than a window on
somebody's desktop.

Verified: measured in both directions against the real session socket. A hard
link of it, in a directory shaped exactly as a run directory with a matching
token and a held lock, is refused; a freshly bound socket in that same
directory is accepted. A test case builds the same hard link from a socket
bound elsewhere and reports rather than passes where the two temporary
directories do not share a filesystem, and deleting the link-count check turns
that case red and no other. The header now records the link count among what is
proved, and says plainly that the claim it restores has been falsified once.

## 2026-08-22 -- A build directory now has to be the one its preset describes

Two ways of configuring this project look interchangeable and are not.
`cmake --preset release` applies the pins the preset carries; `cmake -S . -B
build/release` applies whatever the machine defaults to. With a cache already
present the difference is invisible, because CMake reuses what the cache holds.
Once the directory has been wiped there is nothing to reuse, and the bare form
silently takes the system compiler with every option left at its default. The
standing practice of wiping both build directories whenever new entries
register is exactly what makes this reachable, and it is reachable on the runs
whose numbers get quoted.

The loud direction is not the problem. The release tree pins Clang, and built
with the system default it fails outright on a warning nobody can misread. The
quiet direction is: the sanitizer preset pins a different compiler and turns
the sanitizer on, so a wiped `build/asan` configured bare has the sanitizer
off. That tree builds, tests, and reports green. Nothing in the run is false
except what the run is called.

A gate now holds each build directory to the configuration its preset
describes. The directory is matched to a preset by the path that preset names,
and every cache variable that preset resolves to must be present in the cache
with that value -- the compiler, the build type, the prefix path, and the
sanitizer switch alike. Inheritance is resolved rather than read at one level,
because every pin here lives on a hidden base preset and a single-level read
would find nothing pinned anywhere and pass everything. The compiler is
compared as a located, link-resolved program rather than as text, since a
preset names a program and a cache records where it was found.

That comparison is only worth as much as the thing it compares against, so the
preset file is held to a floor from every build directory: each shipped preset
must pin a compiler and a build type, and at least one must pin the sanitizer
on. Deleting a pin fails by name instead of quietly making the check vacuous.

A build directory no preset names fails rather than skipping. This project's
gates do not declare skip return codes -- each one resolves what it checks and
runs, or states a precondition and fails -- and the precondition here is a
record of what the directory was meant to be. Configuring elsewhere stays
possible; quoting a verification result from a directory with no such record
does not.

The failing direction was reproduced before the gate existed: a wiped
`build/release` configured bare recorded `/usr/bin/c++` against a pinned
`clang++` and dropped the prefix path, and the same treatment of `build/asan`
recorded the sanitizer off, the build type wrong, and the compiler wrong. Both
are now refused by name, and both correctly configured directories pass.

Verification is by consequence against fixture trees, so nothing is configured
or built to measure it: the gate reads a preset file and a cache, and fixtures
present configurations a real tree would take minutes to produce. Twelve
scenarios cover the two bare cases, a directory differing in exactly one
pinned value, a pin reached only through inheritance, a compiler spelled as a
link to the pinned program, both floor removals, an unplaceable directory, an
unconfigured directory, and an unreadable preset file. Ten mutations of the
gate were planted and all ten caught, each by its own scenarios; an eleventh
removed a scenario from the runner list and is caught by a floor on the number
of results reported.

One defect was introduced and caught during the work rather than shipped. The
binary directory a preset uses is inherited here, and the template contains the
preset's own name, so expanding it while walking up the inheritance chain
substituted the parent's name and resolved `release` to a directory belonging
to no preset. Every real build directory then read as unplaceable, and the gate
answered about none of them. It is now expanded once, against the preset being
resolved, and a mutation restoring the earlier behaviour is among the ten.

Known limits, stated. The gate compares what a cache records, so it cannot see
a compiler that was replaced on disk after configuration, and it compares
locations rather than versions: two compilers of different versions at the same
path are the same program to it. Where a system default is a link to the pinned
compiler the comparison accepts it, which is the honest answer, since the build
really was produced by the pinned program; on the machine this was written
against the default is a separate file and is refused. Registered gates run
only when the battery is run, so a directory that never runs the battery is
unaffected by any of it.

---

## 2026-08-21 -- Compare paths by identity, and prove a harness is alive

The interlock published yesterday was measured again and let three things
through, all of them onto the session it exists to protect.

Paths were compared as text. The socket at `/run/user/N/wayland-1` was refused,
and the same socket spelled `/run/user/N//wayland-1` was accepted — one extra
character, a spelling a shell or a configuration file produces by accident. So
were the `/.` and `/../N/` forms. The kernel resolves every one of them to the
login compositor, and the refusal that was supposed to be the interlock's
strongest property did not hold for any spelling but the literal one. The
socket was also inspected with a call that follows symbolic links, so a link in
any writable directory pointing at the live socket was accepted outright: the
directory examined was the link's own, which is not the session's, and the
socket check looked through the link and saw a socket. No path trick was needed
for that one at all.

Both are now decided by identity rather than spelling. A directory is resolved
to its device and inode, which every spelling of it shares and no spelling of a
different directory does, and the socket is inspected without following links.
Identity is what the kernel agrees with; a string is what a caller typed.

The third was a design error rather than an oversight. The token was described
as proof because it is unpredictable, but nobody has to guess it: the check
compared a file the declarer writes against a value the declarer exports, and
any pair of equal strings satisfies that. Unpredictability buys nothing when
the verifier accepts any value that matches itself. What cannot be fabricated
by writing files is a lock the kernel releases when its holder dies, and the
harness already holds one on its run directory for the whole run. A gate now
requires that lock to be held, tested with a non-blocking lock attempt — never
`fcntl(F_GETLK)`, which on Linux does not observe these locks at all and would
have reported every lock free, turning the check into one that always refuses.
The socket's directory must also match the run directory the harness exports,
so "not the session's" is no longer the only bar a directory has to clear.

The headless proof had the same shape of hole one level down: it checked that
the compositor reads the selector and never looked at what the selector was set
to. Asking for the drm backend reached "compositor ready" and exited zero. On a
real wlroots compositor that is precisely the backend that takes the seat, the
virtual terminal and DRM master — the outcome the check exists to prevent,
reachable through the documented knobs rather than by sabotage. Values are now
allow-listed per selector, and a selector the harness does not recognise is
refused rather than trusted. Two smaller corrections follow it: the library
sweep reads ELF headers instead of executing the dynamic loader against the
binary, and the bypass used by the self-test is honoured only alongside a
non-production state directory, so one exported variable can no longer disable
the control that keeps this harness off the seat.

Two claims are corrected rather than softened. The comment describing the
selector search as "the observable trace of code that reads it" was wrong, and
the proof of it was sitting in the same commit: the positive control is a shell
script whose only occurrence of the name is inside an unexecuted here-document.
It does not read the variable in any sense and it passes. What the search
measures is the presence of a string. The direction that carries weight is the
failing one, and it is exact — a program that never mentions the name cannot be
reading it — which is why the value is now checked separately. And the harness
no longer claims to start nothing before its first refusal; it inspects files
and forks tools, and what it does not do is start a compositor.

The checks are now gated by `app_isolated_compositor_declaration`, which builds
its own run directories, sockets and locks and asks the shared check what it
makes of them. Each way through is an executable case, including the accepting
one: a check that refused everything would satisfy every refusal test and be
indistinguishable from a working one. Five mutations were planted and all five
caught, each by its own named case. One of those runs found a defect in the
test rather than the code — the login-session cases were being refused because
no token was present, so they proved nothing about the directory rule, and
reverting that rule to a string comparison left the suite green. The cases now
satisfy every other condition deliberately, so only the rule under test can
refuse. The files they place outside a temporary directory are uniquely named,
are not names any compositor uses, and are removed when the case ends.

State the limit plainly, since overstating it is what carried the previous two
versions past review. A local user who runs their own socket and holds their
own lock can still present something shaped like a harness. Unforgeable proof
is not available to a check running as the same user as the thing it checks.
What is claimed is that no declaration can resolve onto the login session's
compositor and no accidental or hand-typed declaration succeeds.

Verified: the declaration gate passes seventeen cases and each repair has a
failing witness; the harness self-test passes eleven scenarios including both
directions of the value allow-list; a genuine harness run is still accepted end
to end, so the refusal discriminates rather than closing the path. The
real-compositor path remains unmeasured on this machine and the contributor
guide continues to say so.

## 2026-08-20 -- Starting a compositor is an obligation to end it

The harness that runs compositor-dependent gates started a compositor and a
gate, each in its own session, and recorded both. What it did on the way out
was weaker than what it had taken on. Teardown signalled the gate's process
group once and moved on without waiting for it or escalating, then stopped the
compositor and deleted the runtime directory. A gate that did not stop for
SIGTERM therefore kept running against a display being torn down, inside a
directory being removed, with nothing said about it. A gate that ignores that
signal demonstrates it exactly.

The sweep that disposes of runs abandoned by an untrappable kill had the same
shape and a worse consequence. It sent SIGTERM to each recorded process group,
deleted the directory whatever happened next, and reported a reap. A compositor
that ignores SIGTERM survived it, and deleting the record removed the only
thing that named the survivor, so nothing could find it again -- while the log
said it had been disposed of. In production that survivor holds the GPU, a
seat, and a runtime directory.

Underneath both sat an identity problem. A pid is not an identity: numbers are
reused, and every signal here goes to a whole process group, so a stale record
would name whichever group now holds that number. Each pid is therefore
recorded with its start time and both must match before anything is signalled.
The start time was read in a way that returns empty on any failure to read it,
and an empty recorded value can never match, so one transient failure disabled
teardown, the escalation, and every later sweep at once -- silently, and with
the record deleted anyway. It is now a hard failure at the moment of recording:
a run that could not be torn down afterwards does not start.

What replaces all of it is one rule applied in both places. The gate is stopped
first, because it is the process using the compositor and reading the directory
that the following steps take away, and teardown waits for it. Each group is
escalated from SIGTERM to SIGKILL. A record is deleted only once every process
in it is confirmed gone; anything that cannot be confirmed is kept and named,
because the record is what keeps a survivor reachable. A run that finds an
earlier one it cannot dispose of does not start beside it. And a gate that
passed while its own teardown failed is reported as a harness failure rather
than as a pass, since something it started is still alive on the machine.

One defect was introduced and caught here rather than shipped. A process that
is exiting normally passes through a window where the kernel still answers
`kill -0` for it while its `/proc` entry has already stopped being readable.
Treating the first such reading as "cannot be accounted for" failed every
healthy teardown in the suite. What separates that window from a record that
has genuinely stopped being checkable is only that the second is still the
answer when the wait runs out, so the judgement is made there and not on first
sight.

Verification is by consequence, with stubs standing in for a compositor: a real
one is neither started nor needed for any of it. The self-test grew from nine
scenarios to eighteen, and the four defects were reproduced against the
unmodified harness first -- the ordering probe reported a refused connection,
the SIGTERM-ignoring gate outlived the harness, the SIGTERM-ignoring orphan
survived the sweep, the sweep claimed a reap it had not performed and deleted
the record, and an unreadable start time ran a gate that could not have been
torn down. Eleven mutations were then planted in the fixed harness and all
eleven were caught, each by the scenarios written for it and by no others; the
mutation runner compares each mutated file against the original, so a mutation
that failed to apply is reported as unmeasured rather than as a survivor.

Known gaps, stated rather than implied. The real-compositor path stays
unmeasured on this class of machine: the headless proof refuses a compositor
whose backend selector it cannot show to apply, and everything above is proven
with stubs. A process that genuinely survives SIGKILL cannot be fabricated, so
that branch is reported but not witnessed; the witness for the
cannot-be-accounted-for path instead makes the process records stop being
readable while the processes are still running. Liveness is asked of the kernel
rather than of the record, which has one known limit: a pid this user may not
signal reads as gone, a case that does not arise for a harness's own children.
And the early refusal for a record carrying no start time at all is a clearer
message on a path the general one already covers, so removing it changes no
result.

---

## 2026-08-20 -- Make workspace location follow the focused Miller column

Miller navigation now advances the pane's workspace adapter whenever keyboard
or pointer input focuses a different live column. The adapter remains the sole
owner of tabs and history, so the window title, path navigator, breadcrumbs,
tab label, navigation actions, and recent-destination state all observe the
same location as the column that holds focus.

The synchronization is directional rather than a binding loop. A
column-originated change updates workspace location without rebuilding the
chain it came from. Workspace navigation to a path already represented in the
chain focuses that column, while a path outside the chain replaces it with a
new root. Entry and transfer actions remain resolved through the focused
column listing throughout; updating workspace navigation state does not make
the invisible list/grid listing an action target.

The model test records the exact focused-path transition sequence across
right, left, right, and collapse. Rendered-shell coverage drives the same
transitions through keyboard and pointer paths, proves the live chain survives
its own location updates, proves an external destination resets the root, and
keeps the active entry model distinct from the workspace adapter. Release and
ASan/UBSan builds are warning-clean. Release passed 65 non-compositor entries
plus static analysis; ASan/UBSan passed the same 65 with static analysis
disabled. The two offscreen RHI probes skipped in both presets, and the two
real-compositor entries remained excluded under the session-safety policy.
Repository, formatting, QML, file-length, attribution, and application-smoke
gates pass.

## 2026-08-20 -- The filtered RHI gate carries every device-scaling check

The offscreen OpenGL validation entry deliberately filters the device-scaling
TestCase instead of running unrelated visual cases. Its per-axis-rounding
companion was added to that TestCase but omitted from the filter, leaving the
entry that supplies its GPU surface unable to run it. The registered function
list now includes the companion. `rhi_function_filter` rejects either
direction of that drift, so a future TestCase addition cannot silently become
offscreen-GPU coverage in name only.

## 2026-08-20 -- Protected-well mask edges retain their coverage

The presentation pipeline samples the protected-well mask independently from
the content frame. The shaders classify coverage at one half so an edge with
partial coverage stays protected without expanding into the one-device-pixel
ring outside a well. The source texture, however, inherited nearest filtering.
At a source/destination texel phase mismatch, a nearest lookup can select the
transparent texel beside a covered edge, exposing that pixel to bloom,
scanlines, and vignette even though the well's logical rectangle includes it.

The mask source now requests linear filtering explicitly. Its existing
half-coverage classification turns the filtered edge into the intended binary
decision: a covered border remains byte-true, while the outside ring remains
processed. No well geometry is inflated; a scale-independent expansion cannot
satisfy both boundaries at every device ratio. The device-pixel suite makes
the filtering choice observable, so changing the source back to nearest fails
before a frame comparison can quietly stop exercising the condition.

The mutation is measured on the offscreen OpenGL RHI surface: setting the
source to nearest fails `DevicePixelScaling::initTestCase`, and restoring
linear filtering passes the same entry. The real-compositor surface remains
unmeasured: the isolated-compositor harness currently refuses its production
backend selector before starting a compositor, and no interactive-session
render is an acceptable substitute.

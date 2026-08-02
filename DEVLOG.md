# OdySea — Devlog

Public running record of OdySea development, in reverse-chronological order.
Each entry records what landed, how it was verified, and any known gaps. See
`docs/ROADMAP.md` for milestone status and `docs/DESIGN.md` for durable product
and architecture decisions.

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

Verification: release 29/29 including the new gate, ASan/UBSan 28/28 enabled,
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

## 2026-07-31 -- Storage Tube presentation pipeline

The screen-effect levels now render. The shell frame draws crisp, a
thresholded chroma-preserving bright pass extracts what exceeds the emission
threshold outside protected wells, separable Gaussian chains blur it at a
tight core radius and a wide halo radius, and one composite adds the emission
over the content and then rides scanlines, vignette, and an 8x8 ordered
dither on the added light with a soft-knee floor. The wide chain stores
values through a x4 linear headroom encode so its sub-quantum Gaussian tail
survives 8-bit intermediate textures on stacks without float render targets;
the composite decodes it, leaving the added energy unchanged. Shaders compile
to qsb at build time and ship in the module's resource tree.

Every pipeline parameter binds the theme's `effective*` levels — never the
stored preferences the controls display. A dedicated presentation suite
drives the divergence (strong stored levels under a pinning accessibility
override) and fails any stage that reports a stored value; rebinding a
pipeline parameter to the stored property was verified to fail that test.
The suite also drives the panel's sliders by pointer and by keyboard and
asserts the composite's uniforms move, renders Off, Minimal, Balanced,
Strong, and a Custom table to frames and asserts each pair differs, and
probes pixels inside a registered thumbnail well to confirm they are
byte-identical between Off and Strong while the frame around them changes.

Material depth landed with the pipeline. The window and pane grounds are a
still deep-field material scaled by the ground-depth level; the glass amount
fades only the pane ground and the surface amount only the chrome strips, so
translucency reads as depth while text and occluders stay opaque. Text lift
is applied palette-side: chromatic inks multiply toward white, which also
puts them over the bloom threshold. Persistence became a shared motion token
consumed by the list and grid current-item rings as a decay trail; reduced
motion zeroes it through the effective level. Loaded thumbnails register
themselves as protected wells and follow scrolling through explicit viewport
notifications.

Fallback is silent and tested: on the software scene graph — and on any
backend where a shader stage fails to build, via a latched status check —
the pipeline never engages, content renders on the plain path, and the
controls keep writing stored preferences. A forced-software suite asserts
disengagement, a visible plain-path frame, live controls, and the surviving
material and motion behavior; the full-shell input-parity suite (also on the
software backend) confirms the layer stands down inside the real window.
Popups — menus, dialogs, the appearance panel — render in the window overlay
above the pipeline by design, so modal surfaces stay solid and color-true.

Verified with the full gate set: formatting, scoped static analysis,
QML lint/format/module guards, warning-clean release build and tests,
ASan/UBSan build and tests, file-length and public-repository guards, and
silent headless smoke launches on both the software and OpenGL RHI backends.
The GPU-path frame comparisons additionally ran green under both release and
sanitizer builds with the RHI scene graph forced. Known gaps: the offscreen
test environment defaults to the software scene graph, so the frame
comparisons skip unless the environment provides a working GL context (the
suite documents the variables); Victor Mono is resolved, not yet bundled;
window-manager compositor transparency remains out of scope — material
opacity is internal by design.

---

## 2026-07-30 -- Appearance-state hardening: input safety, override semantics

Hardening pass over the new appearance system, closing correctness gaps in the
behavior it promises.

Non-finite input can no longer corrupt the state or the layout. The persisted
format's number parser now rejects `nan` and `inf` spellings — a NaN defeats
every clamp comparison, so such values count as malformed and keep their
defaults — and the clamp helpers pin any non-finite in-memory value to the low
bound of its range. Previously a `scale=nan` line survived parsing, round-
tripped through saves, and rendered the shell at one-pixel rows and type until
a reset. Serialization clamps first, so a written file never carries a
non-finite number. Stored strings are sanitized of control characters: the
format is line-oriented with no escaping, so a font-family value containing a
newline could previously rewrite any key serialized after it.

Effect levels now exist in two views with distinct consumers. The plain
properties are the stored preference the controls display and edit; the new
`effective*` properties are what the rendering pipeline consumes — the stored
levels after the reduced-motion and high-contrast overrides. Previously one
property served both roles, so with an override active an enabled slider read
back zero, snapped back on drag, and silently switched the profile to
`Custom` while appearing to do nothing. Now the slider keeps showing and
accepting the user's value while the override pins only what renders, and
lifting the override renders the adjustments made under it.

Reset now rewrites the settings file unconditionally: a damaged file parses to
the defaults, so the live state could already equal them while the file on
disk stayed wrong and the visible reset action did nothing. Startup is covered
by a test that reproduces the application's exact load sequence against a
pre-existing settings file and asserts the scene's bound surfaces — window
ground, row metrics — render the stored state consistently; removing the
load-time change notification fails it. Combo boxes' follow-direction bindings
(profile reporting `Custom` after a slider write, palette following state
changes) are now tested, appearance controls carry accessible names for
assistive technology, and the leak-checker suppression for fontconfig's
one-time configuration parse now travels with the tests themselves, so direct
`ctest --test-dir` invocations behave the same as preset runs while a
deliberate project leak on the same code path stays fatal.

Documented limits were narrowed to what the code does: unknown keys in a
newer build's file load but are not preserved by the next write, and the
atomic-rename save guarantees readers a complete file rather than durability
across power loss or coordination between concurrent writers.

Verification: 25 release and 24 sanitizer tests pass, including new core
coverage for non-finite and injection inputs and new controller, scene-load,
and control tests; formatting, QML, privacy, and file-length gates pass; a
headless smoke launch stays healthy.

---

## 2026-07-30 -- Live appearance state, palettes, profiles, and settings

The shell now has a real appearance system: one live state object that every
styled surface binds to and every appearance control writes through. The state
model — profiles, effect levels, clamping, and the persisted form — is a
Qt-free core component with headless tests; the `ShellTheme` type in the shell
module binds it to the scene, resolves the active color family into rendering
roles, resolves fonts against the platform's installed families, and derives
row, cell, and type metrics from density and scale. `docs/DESIGN.md` records
the durable decisions.

Six curated color families ship, expressed directly in file-manager roles
(ground, panels, wells, hairlines, entry and metadata text, directory and
symlink inks, selection bed and ink, focus, status inks) rather than terminal
color conventions; `odyssey-default` is the shipped default and an unknown
identifier resolves to it. Directory entries now render in the directory ink
role, entry metadata in the metadata ink role, and the previously hard-coded
hover, pressed, danger, and rubber-band values in derived or palette roles.

Screen-effect state ships ahead of the GPU pipeline that will consume it:
`Off`, `Minimal`, `Balanced` (default), and `Strong` presets over bloom,
scanlines, vignette, persistence, ground depth, and text lift, plus a `Custom`
profile that remembers the user's own levels across preset round-trips.
Reduced motion zeroes persistence; high contrast zeroes the legibility-hostile
gains and promotes the muted and hairline roles. Typography offers the bundled
default face (Victor Mono, through a stable fixed-width fallback chain), the
system fixed-width face, or a directly named family with fallback. Preferences
persist as a small versioned key=value file written atomically; parsing
tolerates unknown keys, malformed values, and files from newer builds.

The appearance panel opens from a toolbar button and from `Ctrl+,`, and every
control applies immediately — a test drives each control with pointer or key
input and observes the shared state change in the same event, so a control
that renders without being wired fails. The sanitizer preset gained a
LeakSanitizer suppression scoped to fontconfig's one-time configuration
allocations, which any process that queries the font database now trips.

Verified with the formatting, static-analysis, QML, file-length, and public-
repository gates, warning-clean release and ASan/UBSan builds, both full test
suites (25 release, 24 sanitizer), and a headless smoke launch against a
sample tree. Known gaps: the effect levels are stored and live but nothing
renders them yet — the GPU presentation pass consumes them next; the Victor
Mono files are not yet bundled, so the default face falls back to an installed
fixed-width family until packaging adds them; window and surface opacity are
persisted state without a compositor path.

## 2026-07-30 -- Report why the shell scene failed to load

Startup now explains a shell that cannot be loaded. The scene is reachable only
through the `OdySea` QML module, so a packaging, install, or resource fault that
drops it leaves the application unable to start; previously that produced a
process which exited non-zero having written nothing a caller could capture.
Measured with the scene omitted from the module's file list, a captured launch
recorded zero bytes on both output streams. The same measurement now records a
single line naming the module, the type, the scene URL when the engine supplies
one, and the engine's own explanation when it has one, followed by the same
non-zero exit.

The report is written directly to the standard error stream rather than through
a logging category. Qt's default handler may route categorized output to the
platform's system log when the stream is not a terminal, which is precisely the
case for a packaging script, a service unit, or any captured launch — the
situations where a startup failure most needs an explanation. Routing the report
through a logging category was measured first and left the captured launch
silent, so the stream is addressed directly. Engine errors are composed from the
individual error records rather than from the aggregate string, which prefixes a
placeholder location for an error that has none, and are joined onto one line so
the report stays greppable.

The load path, its module and type names, and the composed diagnostic are shared
between the application and the tests, so a test cannot pass against a module or
scene name the application does not use. Coverage requires the real scene to
load from the module without a report, an absent type and an absent module each
to be reported, a scene that cannot compile to be reported with the file and
line the engine located, the scene URL to appear only when supplied, engine
errors to be folded onto one line, and the message to explain itself when the
engine offers nothing. The uncompilable-scene fixture is generated at run time
rather than tracked, so a deliberately invalid scene cannot reach the QML
formatting and lint gates. The stream test redirects the process's error
descriptor and reads back what arrived, so it measures delivery to the stream a
caller captures rather than delivery to a message handler; the descriptor and
the write target are both owned by scope-bound types, because a descriptor
leaked out of a failing check would silence every later check.

The entry point is covered as a process, not only as a function. A second
executable is built from the application's own entry point against a scene name
the module does not contain, and the test runs it, requiring a non-empty standard
error stream carrying the diagnostic, an empty standard output stream, and exit
status 1. The shipped binary is launched in the same test with its scene intact
and must start without producing the diagnostic, so a passing failure check
cannot come from a report written unconditionally. The scene name is a
compile-time choice with no runtime override, so the shipped binary's behavior is
unchanged. Removing the write fails both the stream check and the process check,
and returns the captured launch to zero bytes; restoring the aggregate error
string in place of the composed one fails the located-error check.

Two gate clarifications ride along. `qml_lint_guard` needs the shell module
built before it can lint a scene that imports it; the failure previously
surfaced as a missing type-description path with nothing said about ordering, so
the gate now checks each import root's manifest for its declared type
descriptions and states the requirement. The manifest scan reads a final line
that carries no trailing newline, which a generated or hand-written manifest may;
without that, a manifest ending mid-line hid the very declaration the check
exists to read. A manifest that declares no type descriptions at all, such as a
plugin-only module, passes the ordering check and is left to `qmllint`.
Contributor documentation records that ordering, records that scenes live in
`app/qml` because the module gate derives its tracked side from that directory
and rejects a scene placed elsewhere even when the module file list names it
correctly, and records that the corpus-derived gates read tracked files, so a
new file must be staged before those gates can see it.

## 2026-07-29 -- Harden shell navigation and type-ahead coverage

Rendered-shell tests now require handled list and grid navigation keys to clear
an active type-ahead buffer, require unbound modified keys to leave type-ahead
and the cursor untouched, and verify that numbered tab shortcuts beyond the
open tab count stay disabled and inactive.

The shell-model stub now returns fixed scripted prefix-search rows while
recording the requested prefix and cycling mode. This keeps the rendered suite
focused on the shell contract instead of duplicating the adapter's prefix-match
algorithm.

Verified with all twenty-two release tests and all twenty-one enabled
ASan/UBSan tests, including warning-clean builds, static analysis, C++ and QML
formatting, zero-warning QML linting, public-repository and file-length guards,
the QML module guards, and eight-second offscreen release and sanitizer smoke
launches. Replaying each covered mutation makes the rendered-shell suite fail.

## 2026-07-28 -- Complete entry interaction and transfer parity

List and grid entries now share activation and context-menu behavior. Return
and double-click both use the model activation path: directories navigate in
the current tab, while files use an injectable application-layer desktop
launcher. Launcher tests record requests and failures without starting external
programs. A symbolic link keeps its symlink identity while a link whose target
is a directory navigates and accepts drops like a directory.

Right-click, Menu, and Shift+F10 expose the same Open, Copy, Move, Rename, and
Move to Trash actions with selection- and operation-aware enablement. Context
invocation preserves an existing multi-selection, selects an unselected target,
and returns focus to the originating list or grid after closing.

Selected entries drag as standards-compliant, fully encoded local file URLs.
Control requests copy and the default requests move. Directory cells and every
breadcrumb accept internal transfers through the existing asynchronous
operation adapter with fail-on-collision behavior. Validation rejects missing
or busy selections, non-directory destinations, transfers to the current
parent, self-targets, and directory descendants before work begins. Operation
requests capture paths synchronously so model-row reuse cannot retarget them.
Vertical flicks and rubber-band gestures remain separate from the horizontal
drag threshold. The exported MIME payload is a notifying adapter property, so
already-realized list and grid delegates update after selection changes,
in-application renames, and watcher-observed external renames. Drop validation
compares unresolved source identities before canonical containment checks,
preventing a symlink from bypassing the same-parent guard when its target lives
elsewhere.

The breadcrumb strip includes the filesystem root and every absolute path
segment. It scrolls horizontally, provides accessible location names, supports
pointer activation and Left/Right plus Return keyboard traversal, accepts
internal drops, and returns focus to the active directory view after
navigation.

The shell was split into dedicated breadcrumb, list-view, grid-view, and shared
context-menu components before adding these interactions. Adapter tests cover
launcher success and failure, encoded URLs, breadcrumb segment construction,
copy scheduling, and invalid or recursive targets. Rendered-shell tests cover
keyboard and pointer activation, both context-menu keyboard paths, focus and
selection rules, copy/move negotiation, target mapping, rejection, and scroll
and band arbitration. A separate rendered test uses the real adapter and both
production views to require `Drag.mimeData` to follow selection changes and
both rename paths, and requires directory-symlink drop targets to remain
enabled. Core and adapter coverage distinguishes directory, file, and broken
symlinks and requires target-directory changes to refresh the directory role.

Verified with all twenty-two release tests and all twenty-one enabled ASan/UBSan
tests, including warning-clean builds, scoped static analysis, C++ and QML
formatting, zero-warning QML linting, public-repository and file-length guards,
the QML module guards, and eight-second offscreen release and sanitizer smoke
launches.

## 2026-07-28 -- Add spatial navigation and bounded type-ahead

List navigation now moves by rows, while grid navigation respects the rendered
column geometry and does not wrap Left or Right across cell boundaries.
Home/End and viewport-sized PageUp/PageDown movement work in both views.
Shift extends from the shared stable selection anchor, Control moves focus
without replacing selection, Return activates the current entry, and every
movement reveals its result. Switching views keeps current entry, selection,
anchor, and independent scroll positions coherent.

Printable input in either directory view starts a case-insensitive prefix
search. The search wraps, repeated single characters cycle among matches,
Backspace edits the buffer, Escape clears it, and inactivity expires it after a
bounded interval. A match becomes the current single selection and scrolls into
view. Text fields and modal dialogs keep their printable input instead of
leaking it into directory search.

`Ctrl+1` through `Ctrl+9` now activate numbered tabs. View switching moved to
`Ctrl+Shift+1` and `Ctrl+Shift+2`, matching always-enabled, checkable List and
Grid buttons. Pointer switching returns focus to the active view so arrow keys
work immediately instead of leaving focus on a disabled toolbar control.

Adapter tests cover case-insensitive search, wraparound, cycling, failure
stability, focus-only movement, range anchors, and exact selection state.
Rendered-shell tests cover list and grid geometry, PageUp/PageDown,
Home/End, activation, automatic reveal, cross-view anchors, shortcut
separation, button focus, timeout and buffer editing, and text-control and
dialog input ownership.

Verified with all eighteen release tests and all seventeen enabled ASan/UBSan
tests, including warning-clean builds, static analysis, C++ and QML formatting,
zero-warning QML linting, public-repository and file-length guards, and
offscreen release and sanitizer smoke launches. The adapter and rendered-shell
tests also passed fifteen consecutive release runs and five consecutive
sanitizer runs.

---

## 2026-07-28 -- Cover the rendered thumbnail path end to end

Thumbnail source URLs address the image provider by name, and the engine
resolves them by the name the provider was registered under. If those two ever
disagree, every thumbnail silently resolves to nothing: the grid shows
placeholders, no diagnostic appears, and the existing tests stay green, because
they either stop at the model role or start from an image already inside the
provider.

The name and the URL it produces now come from the provider itself, and one
function creates the provider and registers it, so the application and the
tests cannot disagree about it. A new test loads the real scene from the shell
module, decodes a real image file through the real decoder with the disk cache
replaced by an in-memory one, and requires the grid delegate's image to reach
`Image.Ready` with a non-zero painted size. It also requires the same
identifier to fail when addressed to a different provider name, so reaching
`Image.Ready` cannot be an accident.

Registering the provider under a name one letter different from the one the
model builds fails two checks in this test and leaves every other suite
passing, which is the gap it was written to close.

Grid cells expose their image by object name for this purpose. Realized
delegates have a visual parent but no object parent, so the test walks the
visual tree rather than the object hierarchy.

Verified with release and sanitizer builds, the full test suites, the QML and
module gates, and an offscreen application launch.

---

## 2026-07-28 -- Hold the QML module to the tracked scene corpus

A scene can be well formed, formatted, and lint-clean while remaining
unreachable, because reachability depends on the module's file list rather than
on the file itself. Nothing in the compiler or the QML tooling notices that.

The module manifest gate closes that. It derives one side of the comparison
from the tracked scenes under `app/qml` and the other from the manifest the
build produced, then requires the two to agree in both directions. A scene left
out of the module fails the gate, and so does a manifest entry that no longer
names a tracked scene, which is what a rename leaves behind. Scene file names
must begin with a capital letter, since the file stem is the type name the
application instantiates.

The gate's own self-test builds throwaway repositories and manifests covering
an omitted scene, a renamed entry, a renamed file, a stale entry, an absent
manifest, an empty corpus, and a scene that cannot become a type. Seven of the
eight scenarios require rejection and one requires acceptance, so neither a
gate that always passes nor one that always fails can satisfy it.

Verified with release and sanitizer builds, the full test suites, the QML
gates, and an offscreen application launch.

---

## 2026-07-28 -- Load the shell through a linkable QML module

The declarative shell is now a linkable static QML module rather than a module
attached to the application executable. The application and the rendered-shell
tests both link it, so `import OdySea` resolves through the generated `qmldir`
in either case.

This closes a real gap between the two. The rendered-shell tests previously
imported `../qml` as a directory, which reads source files straight from the
working tree and never consults the module. A scene omitted from the module's
file list therefore kept its tests green while the application could not start.
With the module linked, omitting `Main.qml` from the module fails the shell
tests with `Main is not a type`, which is the same failure the application
reports.

The tests run from a dedicated executable that links the module, because the
generic QML test runner only ever sees a source directory. The scenes, the
fixtures, and the assertions are unchanged.

Linting a scene that imports the module requires the built module directory on
the import path, so the QML lint gate now accepts import roots and CMake passes
the build tree's application directory. Import roots containing whitespace are
refused rather than silently word-split.

Verified with release and sanitizer builds, the full test suites, the QML
formatting and lint gates, and an offscreen application launch.

---

## 2026-07-28 -- Add a virtualized thumbnail grid

The shell now offers list and grid presentations over the same incremental
model and selection state. The toolbar switches views with the pointer, while
standard `Ctrl+1` and `Ctrl+2` shortcuts provide the matching keyboard path.
Each view retains its own scroll position when hidden.

The grid realizes only its viewport and a bounded cache buffer. Visible
delegates request thumbnails lazily, release both unfinished work and completed
provider images when hidden or destroyed, and load opaque provider URLs
asynchronously. The provider has its own 64 MiB byte budget and evicts decoded
images in least-recently-used order independently of the core cache. Directory
and unsupported-file placeholders remain usable while decoding is absent or
refused.

The application builds every thumbnail key through the core cache policy.
Symbolic links therefore retain their own cache URI while using their resolved
target's size and modification time for invalidation. A changed visible source
is requested again immediately after its old provider state is removed.
Non-regular sources are rejected before a decoder opens them, and the image
reader's allocation limit reinforces the existing dimension and decoded-byte
bounds.

Grid cells preserve click, control-click, shift-click, double-click, context
menu, wheel, and flick behavior. Arrow and Home/End navigation, activation,
selection toggling, and clearing remain available from the keyboard.
Rubber-band selection computes explicit two-dimensional cell intersections and
passes stable model rows to the shared selection layer.

Rendered-shell tests require a non-empty model before exercising the view. They
count realized grid cells to distinguish virtualization from eager delegate
creation, and cover keyboard and pointer switching, selection and scroll
preservation, thumbnail requests, cell activation and context selection,
two-dimensional band geometry, and flick-versus-selection arbitration.

Focused adapter regressions cover completed-image release, provider byte-budget
eviction order, automatic re-request after an in-place change, symbolic-link
target invalidation, and early refusal of directories and named pipes.

Verified with warning-clean release and ASan/UBSan builds and suites, scoped
static analysis, C++ and QML formatting, zero-warning QML linting, the
public-repository and tracked-file length guards, and offscreen release and
sanitizer smoke launches. The thumbnail-model and rendered-shell tests also
passed fifteen consecutive release runs alongside the thumbnail backend; the
thumbnail model and backend each passed five consecutive sanitizer runs.

The M2 grid and asynchronous cached-thumbnail roadmap item is complete.
Integrated verification passed all eighteen release tests and all seventeen
enabled ASan/UBSan tests, including the repository, formatting, QML, static
analysis, and file-length gates. Both offscreen application launches remained
healthy for the smoke window. Type-ahead, drag-and-drop, breadcrumbs, and the
command palette remain open.

## 2026-07-28 -- Decode and deliver thumbnails through the application adapter

The application layer now supplies the codec and persistent-store interfaces
behind the core thumbnail service. Image headers are inspected before decoding;
only PNG, JPEG, and WebP inputs are accepted, and both dimensions and decoded
byte cost are capped. Output is scaled to the requested standard edge.

Persistent thumbnails use the shared freedesktop cache layout and carry the
source URI, modification time, and byte length in PNG metadata. Cache reads
recover that metadata from the decoded image and leave validity decisions to
the Qt-free core policy.

Worker results cross to the GUI thread through queued, context-bound delivery.
Every result is checked against the active generation, stable entry path, and
full thumbnail key before it can update the model. Rows are never retained as
asynchronous identity. The model publishes loading and opaque image-provider
roles, keeps hard links distinct, withdraws work when a delegate leaves, and
removes provider images when an entry disappears or changes.

Adapter tests cover the decode allowlist and pre-decode bounds, PNG metadata
round trips and stale records, hard-link identity, role removal, cancellation,
navigation that reuses row zero while an older decode is running, and queued
delivery across model destruction.

Known gap: the list shell does not request these roles yet. The virtualized grid
and its view switching remain open.

## 2026-07-28 -- Formatting covers every C and C++ extension

The formatting gate checked only `.cpp` and `.hpp`, so a tracked `.c`, `.cc`,
`.cxx`, `.c++`, `.h`, `.hh`, `.hxx`, `.h++`, `.inl`, `.ipp`, or `.tpp` file
passed without being looked at. Naming a file differently is not a decision
anyone makes in order to skip formatting, so the gate now covers all of them.

A gate that silently checks nothing is indistinguishable from a gate that
passes, which is the failure this change is really about. A self-test now
builds a throwaway repository for each required extension, puts one
deliberately unformatted file in it, and requires the gate to reject it, then
formats that same file and requires the gate to accept it. The second half
matters as much as the first: without it, a gate that always failed would look
correct. The required extensions are stated in the self-test rather than read
out of the gate, so a gate that quietly stopped covering one is caught instead
of agreed with.

No tracked file needed reformatting, because the repository currently uses only
the two extensions the gate already covered. The fixtures are temporary and are
removed afterwards; no unformatted file is tracked, which would otherwise leave
the gate failing against its own test data.

Verified with the formatting gate and its self-test, warning-clean release and
ASan/UBSan builds and suites, the public-repository and file-length guards, and
offscreen release and sanitizer smoke launches. Reverting the change fails the
self-test: narrowing the covered extensions fails exactly the narrowed ones,
restoring the previous two-extension gate fails eleven, a gate that inspects
nothing fails all thirteen on rejection, and a gate that always fails fails all
thirteen on acceptance.

## 2026-07-28 -- Bounded, cancellable thumbnail scheduling

Thumbnail work now runs on a small pool of core workers behind two interfaces
the caller supplies: one that turns a file into pixels and one that reads and
writes the persistent cache. Both are free of any toolkit type, so scheduling is
verified headless with fakes, without a codec, an image file, or a display
server. Results are delivered on a worker thread and never while a lock is held,
matching the directory scanner so a consumer marshals both the same way.

Four properties decide whether a grid of thumbnails stays usable, and each is
enforced rather than assumed. Asking repeatedly for one source decodes it once
while still answering every request separately. Entries the user can see are
decoded before entries they cannot. Work belonging to a location the view has
left is dropped: refused at the door, purged from the queue, skipped at the
moment it would be claimed, and suppressed at delivery, so a decode already
running still populates the cache but answers nobody. Decoded pixels are
retained under a byte budget, evicting least-recently-used entries, and results
are shared so an image already handed out survives its own eviction.

Two smaller bounds keep pathological directories cheap. A source that cannot be
decoded is remembered, so a directory full of files no codec understands is not
attempted again on every pass, and forgetting that record makes the source
eligible again. The queue itself is bounded, dropping the oldest background
request rather than growing without limit.

A stored thumbnail is used only after the recorded description is checked
against the source, so an out-of-date file on disk is replaced instead of shown.
Shutdown stops accepting work, suppresses delivery, and joins every worker
before returning, so no result arrives after the service is gone.

Verified with a headless scheduling suite built entirely on latches and
counters, with no sleeps, repeated twenty-five times without variation.
Reverting a guard fails it: removing deduplication, eviction, refusal memory,
priority ordering, the queue bound, the stored-description check, or request
withdrawal each fails checks, and replacing the joining shutdown with a
detaching one crashes outright. Cancellation is deliberately defended at four
points, so removing any one of them alone is caught by the others; removing the
whole layer fails seven checks.

Known gap: no view consumes this yet, and nothing here decodes an image, so the
roadmap entry for thumbnails remains unchecked.

## 2026-07-28 -- Interoperable thumbnail cache policy

The core now decides which thumbnail is wanted, where it belongs, and whether a
stored one still describes its source. Decoding is deliberately absent: a codec
belongs with the presentation layer that already links one, while everything
that has to be verified without a display server stays here.

Cache layout follows the freedesktop.org Thumbnail Managing Standard. Source
paths become `file://` URIs, cache files are named after the digest of those
URIs, sizes map to the standard directories, and refused sources are recorded
under a directory namespaced by application and version so a later version
retries what an earlier one declined.

URI escaping matters more than it appears. The trash specification and the
thumbnail cache want different byte sets, and reusing the trash escaping would
have escaped characters other desktops leave literal. Because the cache file
name is the digest of those exact bytes, the result would have been a private
cache invisible to every other application and blind to theirs, with no
symptom. The escaping is therefore pinned by expectations taken from an
established implementation rather than from this one.

Validity is checked on inspection, not assumed from the file name. A digest
carries no collision guarantee, so a stored thumbnail is accepted only when the
source URI recorded inside it matches, the recorded modification time matches,
and the recorded length matches whenever the writer recorded one. Stored
thumbnails are themselves never thumbnailed.

Describing a source resolves symbolic links for content metadata while still
addressing the path as the caller named it, so a link appears under its own
name yet its thumbnail goes stale when the contents it points at change. Listing
metadata describes the link and cannot be reused for this, which the headers of
both components now state.

Verified with a headless policy suite covering the published digest vectors and
every padding boundary, the escaping expectations, cache layout and fallbacks,
key derivation across links and refused sources, and the validity rules.
Reverting any single guard fails that suite: the trash escaping fails two URI
expectations, a corrupted round constant fails eighteen checks, dropping the
recorded-URI comparison fails the collision case, describing the link instead of
its target fails four checks, and removing the cache exclusion fails two.

Known gap: nothing consumes this yet. Scheduling, caching, and the decoding
layer are still open, so the roadmap entry for thumbnails remains unchecked.

## 2026-07-28 -- Directory entries carry a modification timestamp

Listings now expose the last content-modification time of every entry in whole
Unix seconds. The value comes from the metadata lookup the listing already
performs, so no additional system call is made per entry.

The timestamp describes the listed entry itself and never the target of a
symbolic link, matching the size and identity fields beside it. Selection
identity depends on telling a link apart from what it points at, so a cache
keyed on file contents resolves target metadata separately rather than
reinterpreting this field. The header states that boundary.

Verified with the headless listing suite, which pins a symbolic link and its
target to deliberately different times and requires each entry to report its
own; resolving the link instead fails that check.

## 2026-07-28 -- Stabilize incremental presentation and geometric selection

Scanner batches, completed scans, watcher changes, sorting, filtering, and
hidden-file changes now reconcile the application model without resetting it.
Refresh batches update entries already observed and append newly observed
entries while retaining the previous listing until the completed scan can
identify removals. Presentation reconciliation groups row insertions and
removals, emits role-specific data changes, and uses layout signals with
persistent-index remapping when ordering changes.

Directory-entry paths provide distinct presentation keys, including for hard
links. Explicit rename pairs preserve the same presented item across a path
change, and device and inode identity provides a fallback only when unique on
both sides. Selection, the current entry, range anchors, navigation history,
sort and filter settings, hidden-entry visibility, and stale-scan rejection
remain stable across incremental updates.

Rubber-band selection now accepts an explicit set of intersected model rows plus
the row nearest the pointer. The list view derives that set from the selection
rectangle in content coordinates. The selection model stores additive bases and
range anchors by stable entry key, leaving it independent of list geometry and
ready for a grid to supply two-dimensional intersections. Row clicks,
double-clicks, context menus, scrolling, keyboard selection, and pointer-drag
arbitration retain their existing behavior.

Adapter tests distinguish insert, remove, data, and layout signals, reject model
resets, preserve a persistent index across rename, cover partial scans,
navigation history, hidden entries, stable range anchors, and explicit
non-contiguous selection sets. Rendered-shell tests cover downward and upward
geometric bands, content movement during a drag, empty-space bands, and
independence from row clicks and scrolling.

Verified with warning-clean release and ASan/UBSan builds and test suites,
formatting and static analysis, QML formatting and zero-warning linting, the
public-repository and tracked-file length guards, and offscreen release and
sanitizer smoke launches.

## 2026-07-28 -- Establish deterministic QML quality gates

The declarative shell now has a repository-owned `qmlformat` baseline using Qt
6.10 tooling, four-space indentation, Unix newlines, explicit semicolons, and
stable source ordering. Every existing QML source matches that baseline.

CTest now exposes separate formatting and linting gates for all tracked QML.
Formatting is checked by comparing source files with canonical formatter output
without modifying the working tree. Linting runs without machine-local settings
and treats every warning as a failure. The checks apply equally to application
components and rendered-shell tests and run in release and sanitizer suites.

Verified with the focused QML gates, warning-clean release and ASan/UBSan
builds and test suites, the public-repository and tracked-file length guards,
and offscreen release and sanitizer smoke launches.

## 2026-07-28 -- Live core integration and recoverable operations

The Qt adapter now owns the cancellable core scanner and consumes incremental
batches through queued UI-thread delivery. Rapid navigation cancels superseded
work. A stoppable worker owns the inotify watcher, coalesces each event burst,
resolves changed metadata off the GUI thread, incrementally updates affected
entries, replaces watches after navigation, and requests a full scan after an
overflow or removed watch.

Directory entries carry device and inode identity from the Qt-free core. The
adapter keys selection by directory-entry path so distinct hard links never
collapse, remaps paired rename events by their inotify cookie, and uses inode
identity only as a fallback when it is unique in both event sets. Selection and
the current row therefore survive sorting, filtering, scanning, watch
refreshes, and external renames without relying on presentation indices.
Hidden operation-recovery entries remain absent from the default listing but
become visible and clearly labeled when hidden files are enabled. They are
never suppressed after discovery, including while an unrelated operation is
active; watcher refreshes are deferred until that operation settles.

Copy, move, rename, and move-to-trash now run off the GUI thread through the
core transactional APIs. Destination and rename dialogs expose conflict
handling, trash requires confirmation, failures open an error dialog and remain
visible in the status area, and every operation is reachable through standard
keyboard shortcuts, toolbar buttons, and entry context menus.

Adapter tests cover incremental batches, rapid-navigation cancellation, watcher
bursts and overflow recovery, hard-link-safe rename remapping, queued-callback
destruction, mutation success and failure, stable selection, and forced
retention of sole-copy recovery data. Rendered-shell tests cover keyboard,
toolbar, context-menu, dialog-confirmation, and error-feedback paths. Verified
with QML linting, formatting, static analysis, the tracked-file length and
public-repository guards, warning-clean release and sanitizer builds, both
complete CTest suites, repeated stress runs, and headless release and sanitizer
smoke launches.

## 2026-07-28 -- Number collisions within the name limit, and correct the working-entry contract

Two corrections to the public contract of the mutation primitives.

Resolving a collision by numbering — `report (2).txt` — lengthened the name, so
it could not resolve a collision on a name already as long as the filesystem
allows for a single entry. The operation refused with the filesystem's length
error. Refusing loses nothing, but a policy whose whole purpose is to find a
free name should find one, and the caller had no way to satisfy it.

A numbered name is now built to fit. The limit is read from the filesystem
holding the destination rather than assumed, and where the number would not
otherwise fit the name is shortened to make room. The extension is kept whole
whenever the numbered name can hold it together with at least one byte of the
stem, since the extension is what identifies the file to both the user and the
system; only where the extension alone would fill the name is it shortened too.
A name that is valid UTF-8 is shortened between characters, never through one,
so what remains still displays. A name that is not valid UTF-8 is cut on a byte
boundary: it did not display as text to begin with, and a different rule would
only make the result harder to relate to the original. The number widens as it
counts past nine, and the shortening absorbs that rather than letting the name
grow. The resolved name is reported in the outcome, and previewing a resolution
agrees with performing it.

The second correction concerns the entries these operations create for their
own use. The contract described a replacement being assembled as data that also
exists elsewhere and is therefore safe to remove. That was wrong. A move
relocates the source under that same role, so when a failure cannot be undone
the entry left behind can be the only remaining copy of what the caller asked
to move. The role is now documented as what it is: while an operation runs the
entry is its own to manage, but one found afterwards was left by a failure or an
interruption, and its name no longer says what it holds. The same is true of a
moved-aside destination. Neither role licenses deletion, and a listing that
omits these entries should offer a way to see them. A test asserts the case
directly: after a move whose install and recovery both fail, the source no
longer exists under its own name, and the entry holding it classifies under the
role that had been described as disposable.

Coverage for the numbering: a maximal name with an ordinary extension; a
maximal name that is almost entirely extension; twelve repeated collisions on
the same maximal name, which must all fit, all be created, and all differ; a
maximal name built from multi-byte characters, where the result must remain
valid UTF-8; and agreement between the previewed and the performed name.
Restoring the previous lengthening behavior fails ten of these checks and then
aborts.

Verified with clean release and ASan/UBSan builds, warning-free under
`-Werror`, the full release suite including formatting, static analysis, and
the public-repository guard, the full sanitizer suite, the mutation tests
repeated ten times under release and five under sanitizers, and a headless
application smoke launch.

## 2026-07-28 -- Bound the working names used to replace an entry

The temporary entries a replacement creates were named after the entry being
replaced. A file name may be as long as the filesystem allows for a single
component, so adding a prefix to it produced a name too long to create, and the
operation failed at the reservation step. The failure fell on exactly the
entries that are most awkward to recover by hand, and it was not a limit the
caller could have anticipated: a name the filesystem accepts should be an
operable name.

Working names now have a fixed shape and a bounded length, independent of what
is being operated on: a marker identifying the operation that owns them, a
role, a per-process tag, and a serial number. A static assertion pins the upper
bound below the single-component limit of the common Linux filesystems. The
serial advances globally rather than per operation, so a name already taken by
another thread, another process, or an entry abandoned by an interrupted run is
skipped rather than retried with the same spelling. The per-process tag keeps
two processes working in one directory out of each other's name space.

Recognizing these entries is now part of the public interface rather than
something a caller reproduces by matching spellings. `classify_working_entry`
reports whether a directory entry belongs to one of these operations and, if it
does, whether it holds a replacement being assembled or a destination that was
moved aside and could not be put back. A presentation layer can tell both apart
from entries the user made instead of showing an unexplained name. The naming
scheme itself stays private, so it can change without affecting callers.

Coverage exercises names at the exact limit the filesystem reports, rather than
an assumed one: copying, moving, and renaming into a free destination; copying
and moving over an existing one; a directory rename over an existing directory;
and a failed install at the limit, which leaves the source, the destination,
and its contents intact with nothing left behind. A further case observes a
working name the code produced, occupies the names that would be reserved next,
and confirms the operation still completes while leaving the entries that were
in the way untouched. Restoring the previous naming scheme fails fifteen of
these checks.

Known gap: resolving a collision by numbering, as `report (2).txt`, still
lengthens the name, so that policy cannot resolve a collision on a name already
at the limit and reports the filesystem's length error. No data is at risk;
the operation refuses rather than acting.

Verified with clean release and ASan/UBSan builds, warning-free under
`-Werror`, the full release suite including formatting, static analysis, and
the public-repository guard, the full sanitizer suite, the mutation tests
repeated ten times under release and five under sanitizers, and a headless
application smoke launch.

## 2026-07-28 -- Replace destinations transactionally

Staging a replacement beside its destination removed one data-loss window but
left another. Copy, move, and rename all still removed an existing directory
destination immediately before installing the prepared entry, so a failure of
that final rename destroyed the destination and installed nothing. The window
was narrow, but the loss was total and silent: the operation reported an error
while the entry the caller had asked to replace no longer existed.

Replacement now runs as one transaction shared by all three operations. The
prepared entry is completed first. An occupied destination is then moved aside
to a sibling under a reserved name rather than removed. The prepared entry is
installed. Only once the install has succeeded is the moved-aside entry
discarded. A failed install puts the previous destination straight back, so
the caller ends up exactly where it started. Nothing is ever removed before its
replacement is in place.

Recovery can fail too, and when it does the rule is to keep data rather than
tidy up. A destination that cannot be put back stays under its reserved name;
a source that cannot be returned to its original name stays under the staging
name it was parked at. Both are recoverable by hand. Removing either would not
be. The reserved names are documented in the public header so callers know what
the temporary entries in a destination directory mean.

Copying now stages unconditionally rather than only when the destination is
occupied, which also removes the partial entry a failed copy used to leave at a
free destination. The same-filesystem atomic paths are unchanged: moving into a
free name, and replacing one non-directory with another, are still a single
rename, which is a stronger guarantee than any sequence of steps can offer.
Replacing an entry with itself remains a no-op that reports success.

None of these failures can be provoked from a test without privileged control
of the mount, so the internal rename seam now distinguishes the steps of a
replacement — relocating the source, moving the destination aside, installing,
restoring, and unwinding — and each can be failed independently. That seam
stays in an internal header that is not installed; the public API is unchanged.
Coverage drives a failed install for a copied-over directory, a copied-over
file, a moved-over directory, and a renamed-over directory, and asserts in each
case that the source, the destination, and the destination's contents survive
and that no reserved entry is left behind. Further cases cover a destination
that cannot be moved aside, a destination that cannot be restored, and a source
that cannot be put back, and assert that the data is retained under its
temporary name. Restoring the previous implementation fails nine of these
checks.

Verified with clean release and ASan/UBSan builds, warning-free under
`-Werror`, the full release suite including formatting, static analysis, and
the public-repository guard, the full sanitizer suite, the mutation tests
repeated ten times under release and five under sanitizers, and a headless
application smoke launch.

## 2026-07-28 -- Stage cross-filesystem moves before replacing anything

Replacing a destination by move removed it before the rename was attempted.
Within one filesystem the following rename then completed the move, but across
a filesystem boundary the rename failed and the fallback copy ran with the
destination already gone: if that copy then failed, the destination had been
destroyed for nothing. The recursive copy in the fallback also wrote directly
over the destination path, so a copy interrupted part-way left a half-written
result in place of what used to be there.

A move now removes nothing until its replacement exists. A rename is still
attempted first and still completes the common cases in one atomic step: moving
into a free name, and replacing one non-directory with another. Only when the
rename cannot do the job — because a directory is being replaced, or because
the two paths are on different filesystems — is the moved entry assembled under
a staging name beside the destination and then swapped into place. Within one
filesystem that staging step is a rename, so the source itself is preserved and
can be put back under its original name if a later step fails. Across
filesystems it is a copy, and the source stays where it is until the copy has
been installed. A failure at any point discards the staging entry or restores
the source, leaving both the source and the destination as they were.

The route a move takes depends on whether the kernel reports a filesystem
boundary, which a test cannot arrange on a single mount point. The rename step
is therefore injectable through an internal header that is not part of the
public API, so the fallback is driven deterministically wherever the tests run.
Coverage confirms that a boundary-crossing move replaces both a file and a
directory, removes the source only after the replacement is installed, and
leaves no staging entry behind; and that a boundary-crossing move whose copy
cannot complete fails with the source, the destination, and the destination's
contents all intact. The same behavior is also exercised over a real filesystem
boundary when the machine offers a second writable filesystem, and reported as
skipped when it does not.

The coverage discriminates: with the previous implementation restored, three
checks fail, including the one that crosses a real boundary.

Verified with the release and sanitizer presets: warning-clean builds, the
eight-test release suite including the formatting, static-analysis, and
publishing guards, the seven-test sanitizer suite under ASan and UBSan, the
mutation suite repeated ten times in release and four times under sanitizers,
and a headless application smoke launch.

---

## 2026-07-28 -- Enforced file-length ceiling

Tracked source and text files now have a hard ceiling of 2,000 physical lines.
The `file_length_guard` checks both indexed content and the tracked working tree
so staged changes and later local edits cannot bypass the limit. Files
approaching the ceiling must be split along clear responsibility boundaries
without compressing readable code or documentation.

Verified against the complete tracked corpus and through the release and
ASan/UBSan CTest suites.

## 2026-07-27 -- Report listing failures instead of throwing

Reading a directory advanced its iterator with a range-for, which uses the
throwing increment. A failure part-way through an iteration therefore escaped
as an exception from a function documented to report errors through an
`std::error_code` out-parameter, bypassing every caller's error handling.
Iteration now advances explicitly through the error-reporting increment: the
first failure is recorded in the out-parameter and ends the walk, and the
entries already read are kept and returned, so a caller can present a partial
listing alongside the error rather than losing both.

The documented contract now states plainly that the function never throws, that
a directory it cannot open yields no entries, and that a failure part-way
through yields the entries gathered before it.

Coverage now exercises every listing failure that can be provoked without
privileged filesystem control: a missing directory, a path naming a plain file,
a symlink loop, and an empty path each report through the out-parameter with no
exception escaping. A successful listing is confirmed to clear a stale error
left by an earlier call, and a directory losing entries while it is being read
is confirmed to degrade to a partial, correctly ordered result.

The mid-iteration failure branch itself has no automated coverage, and the
source says so at the branch. Linux keeps a directory handle usable after the
directory is removed or its permissions change, so the underlying read cannot be
made to fail from a test without privileged control of a filesystem.

Verified with the release and sanitizer presets: warning-clean builds, the
eight-test release suite including the formatting, static-analysis, and
publishing guards, the seven-test sanitizer suite under ASan and UBSan, the
listing suite repeated in both configurations, and a headless application smoke
launch.
## 2026-07-28 -- Stable band gestures and complete parity controls

Rubber-band selection now retains its pointer grab over scrollable lists and
stores its press anchor in content coordinates. Filled-viewport drags therefore
keep their full range in both directions from any scroll position, while future
content movement during selection cannot silently move the anchor.

Tabs can be switched with Control+Tab and Control+Shift+Tab as well as pointer
activation. Select All now has a direct pointer control alongside Control+A.
Rendered-shell tests distinguish both input paths and cover band gestures at a
nonzero scroll position, a content-moving anchor, and unchanged row click,
double-click, right-click, and wheel behavior.

Verified with diagnostic-free QML linting, the Qt Quick interaction suite,
formatting and static-analysis gates, warning-clean release and sanitizer
builds, both CTest suites, the public-repository guard, and headless release and
sanitizer smoke launches.

## 2026-07-27 -- Complete list selection regression coverage

Rubber-band selection now remains available when rows fill the viewport through
a narrow background gutter beside the row delegates. The gutter and the empty
space below the last row share the same selection behavior, while row click,
modified-click, double-click, right-click, and future row-drag handling remain
independent.

The rendered-shell regression suite now exercises plain, Control-modified,
Shift-modified, double, and right pointer actions; keyboard cursor, toggle, and
clear actions; and rubber-band drags in both partially filled and filled
viewports. Qt QuickTest is an explicit configure-time dependency. Tab buttons
use stable implicit sizing so the tab bar no longer forms a width binding loop.

Verified with diagnostic-free QML linting, the Qt Quick interaction suite,
formatting and static-analysis gates, warning-clean release and sanitizer
builds, both CTest suites, the public-repository guard, and headless release and
sanitizer smoke launches.

## 2026-07-27 -- Pointer hit testing and scan watcher lifetime

The list rubber-band surface now occupies only unused space below the final
visible row, so row clicks, modified clicks, and double-clicks reach their
delegates while blank-area drags still create range selections. A Qt Quick test
sends a left click through the rendered shell and verifies that the selection
model receives the row.

Asynchronous scans now use a single RAII-owned future watcher. The watcher
observes the newest scan without heap allocation or deferred manual deletion,
while generation checks continue to reject stale results. Selection input uses
the keyboard-modifier type directly across the QML boundary.

Verified with diagnostic-free QML linting, the Qt Quick pointer regression,
formatting and static-analysis gates, warning-clean release and sanitizer
builds, both CTest suites, the public-repository guard, and headless release and
sanitizer smoke launches.

## 2026-07-27 -- Input-parity navigation shell

The graphical shell now pairs pointer controls with keyboard shortcuts for
back, forward, up, refresh, location entry, filtering, hidden-file visibility,
sorting, tab creation and closure, pane activation, selection, entry
activation, and filesystem-operation requests. Selection supports single,
toggle, range, select-all, cursor-only movement, and rubber-band paths. The
pane workspace preserves independent tab and navigation state while the
transfer-oriented dual-pane layout remains a later milestone.

Verified with diagnostic-free QML linting, warning-clean release and sanitizer
builds, both CTest suites, and headless release and sanitizer smoke launches.

## 2026-07-27 -- Asynchronous shell model and navigation state

The Qt adapter now schedules directory reads away from the GUI thread and
discards stale scan results after newer navigation requests. It exposes loading
and error state, per-tab navigation history, two-pane workspace state,
presentation sorting and filtering, hidden-file control, and a multi-selection
model to the Qt Quick shell. Copy, move, rename, trash, and file-open requests
cross explicit adapter seams while their core and platform implementations
remain pending.

The adapter keeps filesystem behavior in the toolkit-agnostic core. Qt owns
only scheduling and presentation state.

Verified with warning-clean Clang and GCC sanitizer builds.

## 2026-07-27 -- Checkout-independent header analysis

Static analysis now anchors its header filter to the detected source root
instead of assuming the checkout directory is named `odysea`. App, core, and
test headers therefore receive identical analysis in renamed clones, CI
workspaces, and ordinary contributor checkouts.

Verified with `clang-tidy` 22 in both the primary checkout and a temporary clone
whose directory name does not contain the project name, plus the release and
ASan/UBSan CTest suites.
## 2026-07-27 -- Correct destructive overwrite in copy, move, and rename

An overwrite could destroy the entry it was asked to preserve. Copying or
moving an entry into the directory it already occupies resolved the destination
back to the source, and the destination was cleared before the transfer ran, so
the operation deleted the file or the whole directory tree and then reported
that nothing was there. The mutation APIs now compare source and destination by
identity before anything is removed, using an equivalence test that sees through
hard links, symlinks, and alternative spellings. An entry copied, moved, or
renamed onto itself is a no-op that reports success.

Replacement no longer clears the way ahead of time when it does not have to.
Swapping one non-directory for another is left to the rename, which replaces
atomically and leaves no window where neither copy exists; only combinations a
rename cannot perform remove the destination first. Copies that replace an
existing entry are assembled beside it under a staging name and moved into place
once complete, so a copy that fails part-way leaves the existing destination
untouched instead of deleting it first and failing afterwards. Staging entries
are always cleaned up, including when a failed copy reproduced a directory that
cannot be entered.

Remaining no-throw gaps are closed: the path-classification and absolute-path
calls inside the operation and trash APIs now use their error-reporting
overloads, so a filesystem failure is reported rather than thrown out of an
interface documented never to throw. A containment check that cannot reach a
conclusion now refuses the transfer, because rejecting a legitimate operation is
recoverable and copying a directory into itself is not.

Static analysis is clean. An inotify buffer size is computed in the width it is
stored in, two helper signatures no longer take transposable adjacent
parameters, and a test helper no longer reaches into an optional it has not
checked.

New regression tests cover file and directory self-copy, self-move, rename to
the current name, an entry reached through a hard link, replacement by atomic
rename verified through the destination identity, and failed overwrites leaving
both source and destination intact. The permission-dependent cases report a skip
when run by a superuser, whose access ignores the bits they rely on.

Verified with the release and sanitizer presets: warning-clean builds, the
complete release suite of eight tests including the formatting, static-analysis,
and publishing guards, the seven-test sanitizer suite under ASan and UBSan, the
filesystem suites repeated to shake out timing and ordering flakiness, and a
headless application smoke launch.

## 2026-07-27 -- Executable formatting, analysis, and publishing gates

Formatting and static analysis are now executable CTest gates rather than
documentation-only requirements. The formatting check pins `clang-format` 22
and scans every tracked C++ source and header. The analysis check pins
`clang-tidy` 22, uses the active compilation database, and applies the
repository's warning-as-error policy to each tracked translation unit. It runs
with the Clang release database; the GCC sanitizer preset retains its separate
ASan/UBSan runtime gate.

The publishing guard now verifies public no-reply commit attribution, rejects
`Co-Authored-By` trailers and high-signal internal workflow narration, scans for
private-network references, and checks its own content for private-key and token
signatures. All three repository-dependent gates report a CTest skip when a
source archive has no Git metadata.

Verified with the public repository, formatting, and static-analysis scripts,
plus release and ASan/UBSan builds and their complete CTest suites.

## 2026-07-27 -- Deterministic C++ formatting baseline

The complete tracked C++ corpus now matches the repository's `clang-format`
policy. This removes inherited formatting drift from the initial scaffold so
future formatting checks report only newly introduced changes.

Verified with `clang-format` 22 in dry-run error mode, the public repository
guard, and both release and ASan/UBSan test presets.
## 2026-07-27 -- Cancellable off-thread directory scanning

Navigation no longer has to wait for a directory to finish reading.
`DirectoryScanner` runs listings on a worker thread and delivers entries as
incremental batches, so a view can show its first screenful while the rest is
still arriving. Every request returns a token immediately, and the newest
request wins: starting a scan cancels the one in flight and supersedes anything
queued behind it, which is what keeps rapid navigation cheap. Cancellation is
checked per entry, so an abandoned directory stops costing work almost at once.

Every request receives exactly one completion summary reporting the token, the
number of entries delivered, any error, and whether it was cancelled, including
requests replaced before they ever started. Callbacks are invoked on the worker
thread and never while a lock is held; consumers with thread affinity marshal
batches themselves. Destroying a scanner cancels the scan in flight, joins the
worker, and suppresses further callbacks.

Batches arrive in filesystem discovery order, so the listing comparator is now
public alongside `sort_entries` and `make_entry`. A consumer merging batches
orders them exactly the way a complete listing is ordered, and the synchronous
`read_directory` path reuses the same helpers.

Verified with the release and sanitizer presets: warning-clean builds, six CTest
suites passing in both configurations, the scanning and watching suites repeated
to shake out timing flakiness, a headless application smoke launch, and
formatting checked on the changed sources. Known gap: no shell view consumes the
scanner yet, so the roadmap entry for off-thread scanning stays open.

## 2026-07-27 -- Incremental directory watching

`DirectoryWatcher` wraps Linux inotify so a view refreshes the entries that
actually changed instead of rescanning on a timer. Each `wait` drains the whole
kernel queue, so a burst of filesystem activity becomes one batch and one
refresh. Changes report their kind, the watched directory, the entry name, and
whether the entry is itself a directory.

Renames carry the kernel cookie, so the departing and arriving halves of a move
can be matched into a single rename rather than a delete followed by a create.
A queue overflow is surfaced explicitly as a change that instructs the caller to
rescan, because silently dropping events would leave a stale view. Deleting or
moving a watched directory reports the watch as removed and the watcher forgets
it.

The watcher is move-only and single-owner, with one deliberate exception:
`interrupt` may be called from any thread to release a blocked wait, which is
how a watching worker shuts down promptly. The raw descriptor is exposed for
callers that prefer to drive their own event loop.

Verified with the release and sanitizer presets: warning-clean builds, five
CTest suites passing in both configurations, and formatting checked on the
changed sources. Known gaps: watches are not recursive, and a directory that
becomes watchable later is not retried.

## 2026-07-27 -- Delete to the freedesktop.org trash

Deleting from OdySea moves an entry into the desktop trash rather than
destroying it. `move_to_trash` writes a `.trashinfo` record holding the
percent-encoded original path and a local deletion timestamp, then renames the
entry into the trash `files` directory. The record is created exclusively
before the entry moves, so two concurrent deletions of the same file name
cannot claim the same slot; a taken name becomes `report_1.txt`. A failed move
removes the record it claimed.

Trash location follows the specification. Entries on the same filesystem as the
home trash go to `XDG_DATA_HOME/Trash`, defaulting to `HOME/.local/share/Trash`.
Entries elsewhere go to a top-level trash on their own filesystem: a sticky
`.Trash/<uid>` when the administrator provided one, otherwise `.Trash-<uid>`.
Keeping the trash on the same filesystem keeps the delete a rename instead of a
silent copy of a large tree.

Supporting pieces: a move-only RAII descriptor type so POSIX handles close
exactly once, and a percent-encoder covering the specification's unreserved set.
Tests redirect the data-home variable into a temporary tree, so they never touch
a real desktop trash.

Verified with the release and sanitizer presets: warning-clean builds, all four
CTest suites passing in both configurations, and formatting checked on the
changed sources. Known gaps: restore-from-trash, trash emptying, and the
optional directory-size cache are not implemented, and no shell action is wired
to deletion yet, so the roadmap entry for filesystem operations stays open.

## 2026-07-27 -- Core copy, move, and rename primitives

The core gained toolkit-agnostic filesystem mutations: `copy_into`,
`move_into`, `rename_entry`, and the `resolve_destination` helper that previews
a final name before anything changes on disk. Nothing throws; every failure
returns a `std::error_code` so the presentation layer decides how to surface it.

Collisions are governed by an explicit policy: fail, overwrite, or auto-rename
to the next free `name (2)` variant. Overwrite replaces the destination rather
than merging into it, so a copied directory never inherits stale children.
Directory copies recurse and preserve symlinks as symlinks. Copying or moving a
directory into itself or into one of its own descendants is rejected, and moves
fall back to copy-then-remove when source and destination sit on different
filesystems.

A shared headless test harness now provides assertion helpers and a
self-cleaning temporary tree built entirely from synthetic paths. Each core test
file builds as its own CTest executable.

Verified with the release and sanitizer presets: warning-clean builds under
`-Werror`, all three CTest suites passing in both configurations, and formatting
checked on the changed sources. Known gap: delete-to-trash, directory watching,
and off-thread scanning are still open, so the roadmap entry for filesystem
operations remains unchecked.

## 2026-07-27 -- Public development record and repository safeguards

The repository now carries a public development log and explicit publishing
boundaries. Contributor guidance requires staged-change inspection, synthetic
fixtures, impersonal engineering prose, and exclusion of secrets, personal
data, private infrastructure, machine-local configuration, diagnostic captures,
internal workflow artifacts, and personal commit metadata.

Ignore rules cover common environment files, credentials, private keys, local
logs, crash captures, editor state, build output, reports, and local contributor
instructions. These patterns are a defensive backstop; tracked and staged
content still requires inspection before every commit.

The `public_repository_guard` test checks the tracked index for sensitive file
names, private-key and common token signatures, personal home paths, and
at-signs in text. The public clone example now uses HTTPS and the README links
this record.

Verified with the standalone repository guard, release and ASan/UBSan builds,
both two-test CTest suites, and a headless application smoke launch.

## 2026-07-27 -- Initial core and Qt Quick foundation

The first working slice separates a toolkit-agnostic C++20 filesystem core from
the Qt Quick application shell. The core reads directory entries, classifies
their kinds, records file sizes, filters hidden files, and sorts directories
before files with case-insensitive name ordering. A headless test executable
checks the listing behavior without Qt.

The shell exposes the core listing through a `QAbstractListModel` adapter and
renders it with a virtualized Qt Quick `ListView`. Release builds use Clang;
the sanitizer preset uses GCC with AddressSanitizer and UndefinedBehaviorSanitizer.

Verified with the release and sanitizer CMake presets: both builds completed
without warnings, both test runs passed, and the application remained healthy
during a headless smoke launch.

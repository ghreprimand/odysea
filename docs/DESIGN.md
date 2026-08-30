# Design Overview

OdySea is a local-first, GPU-accelerated file manager. This document records the
product intent and the high-level architecture the implementation targets.

## What OdySea is

A fast, local browser for the filesystem on this machine, driven equally well by
keyboard or by mouse.

- **Local and immediate.** It browses what is mounted here. Directory listings,
  metadata, and thumbnails are produced by this application, and a large
  directory stays responsive while it loads.
- **Graphical and GPU-rendered.** Icons, thumbnails, selection models, and
  drag-and-drop are first-class, and the shell is drawn through a hardware scene
  graph rather than a character grid.
- **Keyboard-complete.** Every action has a keyboard route, and the keyboard
  route is designed first rather than retrofitted.
- **Desktop-integrated but not desktop-bound.** It follows the freedesktop
  specifications for trash, thumbnails, MIME handling, and desktop entries, so
  it interoperates with the surrounding session without adopting the conventions
  of any single desktop environment.

## Scope boundaries

- **Not a text-only interface.** The pointer experience is a first-class target,
  equal to the keyboard — full graphical rendering, icons, thumbnails,
  drag-and-drop, and context menus.
- **Not modal by default.** Modal keybindings are opt-in, never forced.
- **Not a cross-device virtual filesystem or cloud sync platform.** Content
  indexing across multiple devices and remote backends is out of scope.

## Interaction model

**Principle: full input parity.** OdySea is a graphical desktop application. The
keyboard and the pointer are two complete, first-class input paths, and neither
is allowed to compromise the other. A mouse-driven user should reach every
action through direct manipulation and menus; a keyboard-driven user should
reach every action without touching the pointer, at typing speed. Achieving both
at once is a deliberate design target.

- **Keyboard.** Every action is reachable without the mouse. A command palette
  provides discoverable, searchable commands; type-ahead jumps to entries; arrow
  and Home/End keys navigate; standard shortcuts cover rename, trash, copy,
  move, and selection. An optional modal keybinding mode is available but is
  **off by default**.
- **Navigation geometry.** List arrows move by one row. Grid arrows preserve
  row and column boundaries rather than wrapping across cell edges, while
  PageUp/PageDown move by the visible rows of the active view. Shift extends
  from the stable selection anchor; Control moves focus without replacing the
  selection. Switching views preserves that model state and reveals the
  current entry.
- **Type-ahead.** Printable input in a directory view performs a
  case-insensitive prefix search with wraparound and repeated-character
  cycling. The buffer supports Backspace and Escape and expires after a short
  inactivity interval. Text fields and dialogs retain ownership of their input.
  Numbered tab shortcuts use `Ctrl+1` through `Ctrl+9`; list and grid switching
  use `Ctrl+Shift+1` and `Ctrl+Shift+2`.
- **Mouse.** Fully first-class and complete, matching modern desktop
  expectations: single-click select, double-click open, ctrl/shift and
  rubber-band (drag-rectangle) multi-select, internal and drag-out
  drag-and-drop, right-click context menus, hover states, back/forward buttons,
  scroll, resizable columns/panes, and clickable breadcrumbs.
- **Entry activation and menus.** Return and double-click share the same model
  activation path. Directories navigate within the current tab; regular files
  pass through an injectable desktop-launcher boundary. Right-click, Menu, and
  Shift+F10 open the same shared context actions. A secondary-button press
  opens at its pointer position; a keyboard request anchors to the current
  delegate instead of consulting pointer state. Opening a menu on an existing
  selection preserves that selection and restores focus to its directory view
  when the menu closes. Directory symlinks retain their symlink metadata but
  navigate and accept drops as directories when their targets resolve.
- **Transfers and breadcrumbs.** Drags carry fully encoded local file URLs,
  refresh those URLs when selected paths are renamed, and negotiate copy or
  move explicitly. Internal directory and breadcrumb targets reuse the
  asynchronous filesystem-operation path with fail-on-collision behavior,
  reject no-op and recursive targets before scheduling, and capture stable
  paths rather than model rows. Same-parent checks use the unresolved source
  identity so symlink targets elsewhere cannot bypass them; canonical paths are
  reserved for containment checks. A same-parent move remains a no-op and is
  rejected; a same-parent copy uses automatic collision-safe renaming so it
  creates a sibling duplicate without replacing the source. Breadcrumbs expose
  every absolute path segment, including the filesystem root, through pointer
  and keyboard paths in a horizontally scrollable strip.
- **Calm path orientation.** Normal browsing keeps the path surface to
  breadcrumbs and quiet Location and Places affordances. `Ctrl+L` or the
  Location button summons a full editor. Hiding a changed editor retains its
  draft and marks that retained state; only a successful navigation replaces
  it. Direct entry accepts absolute paths and `~/` expansion, completes the
  next directory segment inline, and reports invalid, missing, non-directory,
  or unreadable destinations through the shared status surface without
  changing location.
- **Places, recents, and tree jumps.** Places are user-orderable shortcuts that
  can be added and removed with pointer controls or keyboard actions. Recent
  destinations are newest-first, de-duplicated, bounded to twelve entries, and
  explicitly clearable. Breadcrumb ancestors, Places, and recents all jump
  directly to their immutable path instead of walking intermediate folders.
  Breadcrumb and Place context requests expose stable path snapshots and
  visual anchors to the shell-wide action registry; navigation chrome does not
  declare private context menus.
- **Parity rule for contributors.** No primary action may be keyboard-only or
  mouse-only. When a feature adds an interaction, it is wired for both paths.
- **Views.** The single-pane list/grid expands into two simultaneous directory
  adapters for transfers. Each pane retains its own location, selection,
  sorting, filtering, tabs, and history; one active pane owns all shared
  actions at a time. `F6` or a pointer press changes the active pane, the
  bounded divider supports pointer dragging and declared keyboard resizing,
  and copy or move transfers the active selection directly to the opposite
  pane through the shared action registry. Single/dual state and divider ratio
  use the existing versioned settings store. Miller/columns is the third
  exclusive view mode for deep trees: up/down moves within a level, left/right
  moves across levels, Return enters a child or opens a file, and Backspace or
  the matching pointer control collapses the rightmost level. A pointer press
  on a row selects it and reveals a directory to the right; double-click uses
  the same activation path as Return. `Ctrl+Shift+3` and the toolbar choose the
  view alongside the list and grid modes. Moving focus across the live chain
  drives that pane's workspace adapter, so its current tab, history, path
  navigator, breadcrumbs, and pane header name the active column. Workspace
  navigation back to a path already in the chain focuses that live column;
  navigation elsewhere starts a new chain at the requested location. Entry
  operations follow the focused view: copy, move, rename, trash, selection
  status, dialogs, and cross-pane transfers resolve against the active column
  listing. A transfer destination follows the opposite pane's visible listing
  by the same rule. The active column path is shown in the pane header.
  Column rows publish the same URI drag payload and accept the same directory
  drops as list and grid rows. This split preserves an exploratory path chain
  without allowing an operation to act on an invisible source or destination.
- **Geometric selection.** Each view computes the explicit set of model rows
  intersected by its rubber-band rectangle. The shared selection model applies
  that set by stable entry key, so list and grid geometry do not leak into
  selection state.
- **Rubber-band reachability.** A drag-rectangle selection must be startable at
  every window size, including the one that matters most: a directory whose
  entries fill the viewport completely, leaving no blank space below the last
  row. Each directory view therefore reserves a permanent interactive gutter
  along its trailing edge and narrows its delegates by exactly that width. The
  delegate and gutter ranges are adjacent rather than overlapping, so the
  gutter — which sits above the delegates in stacking order, because it must
  stay reachable while rows are present — never covers part of a row and never
  intercepts a click meant for an entry. The gutter is a layout requirement,
  not decoration; a view laid out without it has no rubber-band at all once its
  content fills the viewport, and the loss is invisible in a short directory.
- **Rubber-band drag ownership.** A rubber-band press claims the pointer grab
  for the whole drag, and anchors its origin in the view's content coordinates
  rather than in view coordinates. Both are required for a correct band. A
  surrounding flickable otherwise takes the grab over mid-drag once the pointer
  passes its movement threshold, which discards the motion samples after that
  point and leaves the band short of where the pointer actually went; an anchor
  in view coordinates additionally slides whenever the content scrolls during
  the drag. These constraints hold for any view that offers a rubber-band,
  including reusable components factored out of an existing view.
- **Terminal bridge.** Opening a terminal at the current directory, and running a
  command against the current selection, are core interactions.

## Architecture

The codebase separates a toolkit-agnostic core from the presentation layer:

- **`core/` (pure C++20, no Qt).** Directory reading, stable entry identity,
  cancellable scanning, incremental directory watching, transactional copy,
  move, and rename, freedesktop trash support, a bounded journal that reverses
  completed operations, and thumbnail cache policy and scheduling. It has no
  GUI dependency and is unit-tested headless. This is
  where the performance-critical work lives, deliberately free of framework
  overhead.
- **Entry kind is unresolved; navigability is resolved.** An entry's kind
  describes the directory entry itself, so a symlink is classified as a symlink
  whatever it points at. Symlinks are tested before directories for exactly
  that reason, and the ordering is a requirement rather than an accident: a
  symlink to a directory that reported itself as a directory would lose the one
  fact that distinguishes it, and the shell could no longer mark it, refuse to
  recurse into it, or resolve it separately. Whether an entry behaves as a
  directory is a second, separate question answered through the link target.
  Call sites that dispatch on navigability — opening, dropping onto, recursing
  into, or ordering an entry — must ask the resolved question and must not read
  the kind as a proxy for it. A broken symlink keeps its symlink kind and is
  not navigable.
- **Entry identity is a single value, and a device and inode pair is not
  enough.** Selection, focus, and the selection anchor follow entries by
  identity rather than by name, path, or row, so that renaming, sorting,
  filtering, or refreshing a directory does not lose track of what the user
  picked. Identity therefore carries two obligations at once: it must be
  distinct, so following an entry can never land on a different one, and it
  must be stable, so an entry that merely moved or changed still matches
  itself. The device and inode numbers satisfy neither on their own. On Btrfs
  every subvolume root carries inode number 256, and the device half is an
  anonymous number the kernel allocates at runtime, releases when the subvolume
  goes away, and reissues to the next subvolume created — so a removed
  subvolume and an unrelated new one can present exactly the same pair. The
  creation time closes that gap: it is the one timestamp neither a rename nor a
  content write disturbs, so it separates a recycled identifier from the entry
  that held it before without disturbing an entry that only moved. Identity is
  a single comparable value rather than loose fields, because a caller that
  compares only part of it reintroduces the collision. Filesystems that record
  no creation time degrade to the pair, which is no worse than before.
- **Identity is session-scoped and must never be persisted.** Because the
  device half is an anonymous number on some filesystems, an identity is
  meaningful only within one run against one live mount. It is never written to
  disk, embedded in a cache key that outlives the process, or compared across a
  remount. A consumer that needs a durable handle uses the path.
- **An unknown identity matches nothing.** Metadata lookup can fail, and the
  resulting identity holds zeroed fields. Comparing those by equality would
  report every entry whose lookup failed as the same entry, so the comparison
  used for following an entry rejects unknown identities outright: a failed
  lookup means "cannot say", not "matches everything that also failed".
  Following an entry additionally requires the match to be unique on both
  sides, so an ambiguous identity drops selection rather than moving it.
- **`app/` (Qt Quick).** The GPU-rendered shell: the QML scene, input handling,
  theming, and thumbnail decoding and presentation. A thin adapter
  (`DirectoryListModel`, a `QAbstractListModel`) is the single boundary that
  exposes the core to QML. It marshals scanner and watcher callbacks onto the UI
  thread, schedules mutations away from it, and reconciles presentation changes
  through incremental row, data, and layout signals keyed by directory-entry
  identity. Default-application launching is an injectable application-layer
  service, so tests record launch requests without invoking desktop programs;
  the core itself never includes a Qt header.
- **Columns retain only the live path.** The Miller controller composes the
  same `DirectoryListModel` adapter used by the primary views, one instance per
  live path level, so every column inherits cancellable off-thread scanning and
  incremental updates. Selecting a different branch or collapsing a level
  destroys the displaced descendant models immediately, which cancels their
  scans and releases their listings; previously visited branches are not kept
  as a hidden cache. The horizontal column strip and every vertical entry list
  are virtualized independently with no delegate cache buffer, so a deep path
  multiplies listing state only by the levels it still represents and a large
  directory does not multiply rendered rows by its entry count. The controller
  is the sole C++ owner of these QML-visible listing objects; they are marked
  with C++ ownership before crossing the QML boundary so the engine never
  becomes a second owner. Settings that describe every column, such as hidden
  visibility and sorting, necessarily pay their linear presentation cost once
  per live level. Text filtering instead applies only to the active listing
  and releases its descendants first, preventing both per-keystroke fan-out
  and a chain whose ancestor anchor has been filtered away.
- **Tree search walks once and ranks many times.** Opening fuzzy find starts one
  cancellable, off-thread walk rooted at the focused listing's directory. The
  completed corpus retains one folded name and root-relative path per entry;
  every query ranks that immutable corpus on a separate newest-request-wins
  core thread, so a keystroke performs no filesystem I/O and cannot restart
  the recursive walk.
  Directory links appear as results but are never followed, hidden subtrees
  follow the active view's hidden-file setting, and the walk stays on the
  root's filesystem by default. Matching prefers exact, prefix, and contiguous
  name matches before non-contiguous name matches and relative-path matches,
  with stable path ordering for ties. Both the walk and ranking poll
  cancellation within their per-entry loops. Activating a directory enters it;
  activating a file enters its parent, clears a local listing filter that would
  hide it, and restores selection and current position to that exact entry when
  the asynchronous listing settles.
- **Reusable shell components.** The chrome is composed from module
  components — the shared button and text field, the chrome strip material,
  the navigation toolbar, tab strip, action row, status strip, the
  inactive-pane placeholder, and the directory pane — each driven by the
  semantic theme roles through a single bound theme object rather than
  per-instance color plumbing. The directory pane is the one site that maps
  theme roles onto the directory views' granular color and font properties,
  and it deliberately adds no clip and no transform between the views and the
  presentation layer: thumbnail well registration names the grid as its only
  clipping viewport, and the mask layer maps well rectangles assuming that
  single clip level and identity transforms. New surfaces build on these
  components instead of restyling primitives inline, so palette, density,
  scale, and contrast changes restyle every surface from one definition.
- **Shared action system.** Every user-facing action — over an entry, a
  selection, blank canvas, a path segment, a Places shortcut, a device, a tab,
  or a pane — is declared exactly once, carrying its label, icon role,
  enablement, key sequences, and handler together. Menus, toolbar buttons, the
  tab strip, pane surfaces, the instantiated shortcut table, and the command
  palette all render from these declarations through one registry; no surface
  restates enablement or wording, because duplicated enablement logic has
  already been observed to drift between a menu and a toolbar until a gate
  caught it. Enablement is a function of the action and a context snapshot
  only — never of which surface is asking — and is revalidated when an action
  triggers, so a stale popup or a key press on a contextually dead action is a
  safe no-op rather than an unauthorized operation. Context snapshots are
  immutable frozen objects keyed by kind and target identity (path or index);
  capabilities are read live at evaluation and never snapshotted. Each hosting
  surface owns one shared menu instance parameterized per invocation rather
  than a popup per realized delegate; keyboard-invoked menus anchor to the
  focused item, never to a pointer position; disabled actions render visibly
  disabled and are skipped by menu key navigation rather than hidden; and
  destructive actions state their exact target count and render last, after a
  separator. Key sequences are declared with their actions, and a sequence
  declared by two actions is a test failure, not a runtime shadowing
  surprise; conflict detection compares sequences as Qt parses them rather
  than as raw strings, so two alias spellings of one physical key —
  `Delete` and `Del`, `Escape` and `Esc` — collide in the gate exactly as
  they collide at runtime.
- **Command palette.** The palette is a registry surface, not a second command
  list: it enumerates the labeled declarations at open and on each filter
  change, so an action added to the declarations is reachable in the palette
  with no palette-side change. The enumeration is honest about reach: a
  declaration whose enablement is a per-target predicate can never be
  satisfied by a context that does not carry its target, so the palette's
  global context omits target-scoped rows instead of listing them
  permanently dead. The palette opens that global context over the current
  location and a tab ordinal, so commands whose predicate reads those fields
  — adding the current location to Places, switching to a numbered tab — are
  reachable and invocable there rather than absent; only rows whose target
  the global context cannot carry, such as a specific entry, Place, or pane,
  are omitted. Every row it does list is either enabled or states the reason
  its declaration provides — an invariant the tests pin as such rather than
  as row counts. Rows show the declared key sequence read from
  the declaration; disabled rows stay listed with their stated reason, are
  skipped by keyboard navigation, and are disabled at the item level so
  assistive technology reads them as unavailable rather than actionable;
  activation routes through the registry's trigger-time revalidation. The
  filter field
  owns focus while open, and focus restoration on dismissal is the modal
  focus popup's platform behavior, pinned by the full-shell input-parity
  tests rather than duplicated in palette code. `Ctrl+Shift+P` opens the
  palette; the dual-pane toggle lives on `F3`.
- **One cancellation contract, not one per walk.** Directory listing and
  storage-usage accounting are both long walks a user can abandon mid-flight,
  and both need the same guarantees: a private worker thread, monotonic
  request tokens where starting a request cancels everything issued before it,
  exactly one completion callback per request including one replaced before it
  ever started, no callback delivered once teardown begins, and a cancellation
  flag the running walk polls. Those guarantees are implemented once and
  shared. A second implementation would be a second set of edge cases, and the
  edges — a superseded request that never ran, a teardown mid-walk — are
  exactly where two implementations would quietly disagree.
- **Cancellation is polled per entry, not per directory.** A walk that only
  checked between directories would still finish the directory it was in,
  which on a large one is an unbounded wait after the user has already moved
  on. The check is cheap enough to make per entry at any depth, and it is also
  made between directories so that a queue of unreadable ones is abandoned
  rather than drained.
- **Storage usage states what it counts.** A usage figure that does not say
  what it measures is not a measurement, so the accounting is fixed and
  visible rather than implied. Apparent and allocated sizes are reported side
  by side and never blended: a sparse file claims far more than it occupies, a
  compressed one less, and a small file usually occupies a block more than it
  claims. Every entry counts, hidden ones included, because a dotfile occupies
  real space and presentation filtering must not silently change a
  measurement. Directories count their own metadata as well as their contents.
  A symbolic link counts at its own size and is not followed unless the caller
  asks, because following one makes the figure depend on where the link
  happens to point. The same inode reached twice — through hard links, a
  bind mount, or a link the caller asked to follow — is counted once, with the
  first reach owning the bytes and later reaches reported as deduplicated; a
  child's total is therefore "what removing this child would free" only when
  nothing outside the subtree also links to its files. The guarantee does not
  depend on an entry's link count, because a bind mount presents one inode at
  a second path with a single link at both; holding it costs one identity per
  counted entry, which is the deliberate price of a figure that cannot inflate
  without saying so. Space the walk could not read
  is reported as a partial result, never dropped as if the subtree were empty.
- **Storage usage has two equal views over one incremental model.** The
  proportional map and the visible accessible list share selection, current
  position, subtree navigation, apparent and allocated totals, and scan state;
  neither is reconstructed from the other. Pointer and keyboard activation
  enter the same subtree, and closing the workspace cancels its outstanding
  walk. The app adapter retains only the newest pending worker snapshot,
  updates at most the one subtree that is actively changing per delivery, and
  performs its only whole-result reorder after successful completion. A fast
  scan therefore cannot queue an unbounded series of full snapshots on the GUI
  thread or turn incremental progress into repeated whole-list reconciliation.
- **Crossing a filesystem boundary is a caller's decision.** A walk stays on
  the filesystem it started on unless told otherwise, because a walk from a
  system root would otherwise wander into pseudo-filesystems, removable media,
  and network mounts and answer a question nobody asked. Two consequences
  follow from the boundary being a device number. A Btrfs subvolume carries
  its own anonymous device number, so a subvolume nested in the scanned tree
  reads as another filesystem and is reported as a skipped boundary rather
  than measured. A bind mount of the same filesystem shares its device number,
  so it is not a boundary at all; what stops it from being counted twice is
  inode deduplication, not the boundary setting.
- **A walk over a cycle terminates by construction.** Following directory
  links is optional, and when it is on, a directory already entered in this
  walk is never entered again. Termination therefore does not depend on a
  depth limit or a timeout. Directory identities are tracked unconditionally
  rather than only when links are followed, because a bind mount of part of
  the same filesystem re-presents directories the walk has already entered
  without crossing any boundary and without any link involved. The cost is
  bounded by the number of directories, far below the number of files.
- **A completed operation is recorded with the terms of its own reversal.**
  Copy, move, rename, and delete-to-trash are recorded in a bounded journal
  that performs them rather than being told about them afterwards, which is
  what lets it see a destination before an operation runs and the result
  immediately after. That difference is the whole safety argument for reversing
  a copy: reversing one removes the entry the copy created and never an entry
  that was already there. The newest record is the only one a reversal acts on,
  because reversing an older operation while a newer one still stands would
  produce a state the filesystem never held.
- **Whether an operation can be reversed is settled when it is recorded.** A
  record that can never be reversed carries the reason as part of itself, so it
  is never offered as reversible and refused afterwards. Four situations settle
  that as the operation completes. An operation that replaced an existing entry
  discarded what it replaced, so reversing it would restore half of a state
  that never existed and the whole reversal is refused. An operation that
  resolved to the entry it was given changed nothing, and treating its result
  as something the operation created would delete the only copy. A copied tree
  larger than the journal will remember cannot be confirmed unchanged later. A
  move between filesystems copies the data and removes the original, so a file
  that had more than one name arrives as a new one, and nothing rejoins it to
  the names that kept the old data. That last barrier is about files and only
  files: a directory cannot have a second name, because the kernel refuses a
  user hard link to one, and the count that would suggest otherwise reports how
  many subdirectories a directory holds rather than how many names it has.
  Filesystems do not agree even on that, so a condition read from it barred
  every crossing directory move on one filesystem and none on another.
- **A reversal that cannot be certain does nothing.** Returning an entry
  requires it to still be the entry that moved, compared by identity rather
  than by path, because a path can be reoccupied by something that looks
  exactly like what left it. Removing what a copy created is held to a stricter
  test because it destroys: every entry the copy made must still carry the
  identity, the modification time, and the size it had when the copy finished,
  which detects an entry added, removed, or written since. That test is applied
  to the root of a result and to every entry beneath it alike. A field recorded
  for the root and omitted from the entries inside a tree protects the tree
  more weakly than the root, and since a reversal removes the whole tree, what
  protects an entry cannot depend on how deep in a copy it sits. Every step of
  a reversal refuses a name that is already taken rather than displacing what
  holds it, so a reversal can fail but cannot overwrite. What that leaves
  uncovered is stated rather than implied: a rewrite that changes neither the
  modification time nor the size is invisible, times are compared in whole
  seconds, an identity is only as distinct as the filesystem makes it, and the
  journal is a convenience over a filesystem other programs share rather than a
  transaction.
- **A guard that cannot be shown to matter is not a guard.** The journal's
  tests show that a reversal refuses; they cannot show which check produced the
  refusal, and a check that stopped being read would leave them all passing.
  Each one is therefore deleted in turn by an automated gate that requires the
  suite to fail, and the two checks with no reachable failing state are
  declared to that gate and required to survive, so a declaration cannot
  quietly become a free pass as the code around it changes.
- **`tests/` (pure C++).** Headless verification of the core, runnable under
  AddressSanitizer.

Keeping the core free of Qt types means it stays fast, portable, and testable,
and that the presentation layer can evolve independently.

## Appearance

The shell's look is one shared, live state object rather than scattered style
constants. What each value *means* — which profiles exist, what their effect
levels are, how preferences persist and clamp — lives in the Qt-free core as a
versioned appearance model with headless tests. The presentation layer binds
that model to the scene as the `ShellTheme` type: it resolves the active color
family into rendering roles, resolves fonts against what the platform actually
has, and derives metrics from density and scale. Every appearance control
writes this state directly and every bound surface restyles in the same event;
there is no apply step.

- **Semantic roles, not terminal conventions.** Colors are named for what a
  file manager renders: window ground, panels, wells, hairlines, entry text,
  metadata columns, directory and symlink inks, selection bed and ink, focus,
  match, and status inks. Fourteen curated families ship,
  `odyssey-default` first. The set spans midnight blue, amber, teal, violet,
  green, red, and magenta grounds alongside the existing neutral and light
  choices. Each family's structural ground, frame, match, focus, warning, and
  success roles retain the named family's canonical values. Application-only
  wells, panels, reading inks, file-type inks, and selection beds are tuned on
  their real render sites; a selection bed never inherits a terminal selection
  role. Every ink clears its role-specific contrast floor on the surfaces it
  renders over, directory ink stays measurably chromatic, and the selection bed
  remains distinct from the ground. An unknown family identifier resolves to
  the default rather than rendering nothing.
- **Accent presets preserve meaning.** Tideglass, Beacon, Ember, Orchid, and
  Verdant select one stable accent identifier whose display name is separate
  from persisted state. The accent drives active chrome, focus, rubber-band,
  and bright-pass emission live while profile and accessibility overrides
  retain their established authority. Directory, file, symlink, error,
  warning, and success inks stay in the semantic palette matrix. The raw
  preset color is an authored hue input, not a displayed-token contract:
  `ShellTheme.accent` is resolved against the active family's render sites
  after profile and accessibility lift, and previews or future swatches bind
  that resolved token. A dynamic invariant keeps every current preset from
  changing file-type and status roles; a separately exact roster test makes a
  shipped-preset change explicit. Every shipped preset clears the window,
  selected-entry, hover, pressed, and panel sites for every family, profile,
  and high-contrast state. Those shared samples drive resolution and
  acceptance, so the displayed token cannot drift into a second contrast
  rule. There is no arbitrary custom-accent input: every selectable choice is
  resolved at this boundary before it reaches a shell surface, rather than
  presenting an unreachable selection warning. The preset control is keyboard
  and pointer reachable, previews immediately, and reset restores Tideglass.
- **Measured contrast floors.** The contrast claims are asserted, not
  eyeballed: a test measures every foreground role against the beds tracked
  surfaces actually paint it on, in every shipped family, in both override
  states. The pair list is derived from render sites, not from the role
  vocabulary — a floor asserted on a bed nothing paints can neither fail
  nor protect anything, so roles without a render site carry no pair until
  a surface paints them. Beds resolve to every variant the compositor can
  present: the pane ground is measured flat and at each deep-field gradient
  extreme, the panel both opaque and as a chrome strip over the window
  ground, and the selection, hover, and pressed interaction beds are
  measured under every ink that renders on them — entry text and icons keep
  their floors while selected, hovered, and pressed, not only at rest.
  Under high contrast, text-sized roles must clear the 4.5:1 ratio and
  non-text indicators 3.0:1 — that override is the accessibility path, and
  each family carries a curated high-contrast danger variant because the
  base danger ink cannot reach text strength on the selection and hover
  beds. The default state holds the same floors on the reading and
  indicator pairs, with two accepted exceptions: icon ink is deliberately
  subdued below the Strong bright-pass threshold and carries an
  anti-regression bound instead, and the status inks hold the non-text
  floor while high contrast raises them to text strength. Disabled chrome
  ink keeps an anti-regression bound as well; inactive controls are exempt
  from WCAG minima, so that floor is a perceivability guarantee, not a
  conformance claim. Text lift is exempt on light families: their inks are
  dark marks on bright grounds, so multiplying toward white would lower
  contrast instead of raising emphasis.
- **Adaptive chrome density.** Below their measured width bounds the toolbar's
  workspace toggles, the path controls, and the action row's operation buttons
  drop their labels and render icons only, and the filter field narrows, so
  every control stays visible, reachable, and unclipped down to the window's
  minimum size (720×480 logical). Each bound comes from an independent,
  always-labeled measurement row using the same controls, margins, spacing,
  and stretch reserve as its rendered row, so compact children cannot change
  the condition that selects them. Every chrome strip reserves an interior
  perimeter above and below its controls, preventing a row from crossing its
  own surface or crowding the next band. The labels remain available through
  accessible names and tooltips, and the bound scales with the interface.
- **Screen-effect profiles.** `Off`, `Minimal`, `Balanced` (shipped default),
  and `Strong` are fixed presets over the effect levels: core and wide bloom,
  scanlines, vignette, persistence, ground depth, and text lift. `Custom`
  renders the user's own stored levels; moving any effect control switches to
  it and the adjustments survive preset round-trips. The levels exist in two
  views with distinct consumers: the controls display and edit the *stored*
  preference, while the rendering pipeline consumes the *effective* levels —
  the stored ones after the accessibility overrides. Reduced motion and high
  contrast zero the glow, bloom, scanline, vignette, and persistence gains;
  high contrast also pins effective text lift and promotes the muted and
  hairline roles to stronger inks. Because the overrides act on the effective
  view only, an active override never makes an enabled control discard or
  misreport a write, and lifting the override renders the adjustments made
  while it was on.
- **Typography roles.** The static shell module carries Victor Mono 1.561 in
  Regular, Italic, Bold, and Bold Italic faces under the SIL Open Font License.
  The application registers those resources before constructing the shell, so
  the bundled source does not depend on a system installation; registration
  failure resolves through a fixed-width fallback chain. The system's
  fixed-width face and a directly named installed family remain selectable,
  and a missing named family falls back rather than failing. Semantic content,
  chrome, path, caption, and long-form roles derive their metrics from density
  and UI scale. Entry row and grid-cell geometry stays invariant when a font
  source changes, with tests bounding the resulting glyph-metric delta.
  Long-form copy uses the system's proportional general face, 1.45 leading, a
  density-scaled maximum measure, wrapping, and the primary neutral text ink
  to keep dialogs and explanations readable.
- **Vector iconography.** Directory, file, symlink, navigation, view, tab,
  selection, transfer, rename, delete, and open symbols share code-native
  vector paths in a 24-unit coordinate space. They recolor through semantic
  theme ink, retain the same geometry at 1x and 2x scale, and gain stroke
  weight under high contrast. Entry icons use a dedicated thin-stroke
  component that owns the mapping from directory, file, and symbolic-link
  metadata to both outline geometry and the existing file-type ink roles.
  List and grid consumers therefore cannot drift into separate type-to-color
  tables, and changing the live accent leaves entry recognition unchanged.
  Effects-off and software rendering keep the same vector geometry and role
  ink; high contrast strengthens the outline without replacing its semantic
  role. Normal dark-palette icon ink remains below the
  Strong bright-pass threshold so orientation chrome does not become an
  unintended emitter; high contrast deliberately promotes it to primary text.
  The ink under that cap is a curated per-family hue rather than a derivation
  from the faint text ink: the cap limits the brightest channel, so a family
  whose faint ink peaks in a low-luminance channel would land its capped
  symbols below the measured pressed-bed floor, and the neutral families
  choose a hue whose capped form stays measurable on every control bed.
- **Application identity mark.** A circular O carries one horizon wave. The
  toolbar renders that geometry through `VectorIcon`, using the semantic icon
  ink and the established high-contrast stroke lift; it never follows the
  selectable accent. The desktop asset uses the default family's window-ground
  and primary-text roles, while its symbolic companion is the same geometry in
  one neutral stroke for monochrome and high-contrast icon themes. Both assets
  stay scalable SVGs, so there is no raster fallback or parallel in-application
  drawing path. The installed icon theme remains authoritative when it provides
  an `odysea` icon, and the bundled desktop asset is the window-icon fallback.
  Tests rasterize both assets at 16, 20, 24, 32, and 48 logical pixels at 1x
  and 2x device sizes, assert the symbolic rendering is one neutral ink, and
  exercise the live mark across every effect profile, accent preset, and
  high-contrast state.
- **Versioned persistence.** Appearance, accessibility, Places, and recent
  navigation preferences serialize to one small key=value file, so independent
  surfaces cannot overwrite each other through competing stores. Repeated
  navigation values use percent encoding and explicit counts, preserving UTF-8
  labels and paths while making malformed or incomplete lists fall back safely.
  The write goes through a sibling temporary file and a rename, so a reader
  never observes a partially written file; durability across power loss and
  coordination between concurrent writers are out of scope for preference
  state. Parsing is tolerant: unknown keys are ignored on load — and are not
  preserved by the next write — malformed and non-finite values keep their
  defaults, out-of-range values clamp, control characters never enter a
  stored string, and a missing file is the first-run default state, so a
  newer build's file still loads on an older one. Resetting to defaults
  rewrites the file unconditionally, which also repairs a damaged one.

## Presentation pipeline

The screen-effect levels render through one pipeline over the shell frame:
the content draws crisp, a thresholded chroma-preserving bright pass extracts
what exceeds the emission threshold, two separable Gaussian chains blur it at
a tight core radius and a wide halo radius, and a single composite adds that
emission over the content and then rides scanlines, vignette, and an ordered
dither on the added light, with a soft-knee floor so lit pixels never reach
zero. The wide chain stores values through a linear headroom encode so its
sub-quantum Gaussian tail survives 8-bit intermediate textures on stacks
without float render targets; the composite decodes it, so the added energy
is unchanged.

- **Effective values only.** Every pipeline parameter derives from the
  theme's effective effect levels — never the stored preferences the
  controls display. The presentation tests set the two views apart with an
  accessibility override and fail any stage that reports the stored value.
- **Protected wells.** Thumbnails and previews are color-true content, not
  emitters. Views register each loaded thumbnail with a mask layer the
  pipeline samples; masked pixels are excluded from the bright pass and pass
  through the composite untouched, byte-identical to the plain path, while
  the chrome around them keeps its depth. Protection is binarized at the
  sampling site: any pixel at least half covered by the mask is protected
  outright, because linear sampling of the mask edge lands on a half-texel
  phase at fractional device scales and a proportional mix would leak a rim
  of added light onto the outermost protected row. The protection boundary
  therefore snaps to the nearest device texel, and the well border is
  verified byte-true against the plain path along its entire perimeter on
  the GPU path — including on a real 2x surface, where the rim leak was
  observed and fixed. Protection is bounded from the outside as well: the
  ring one device pixel beyond a well sits on receiving material in the
  gate and must stay processed, so a mask oversized in any direction fails
  loudly instead of silently exempting surrounding chrome from the
  pipeline.
- **Plain-path identity.** When every stage is at identity — the `Off`
  profile, or every gain zero — the pipeline disengages and the content
  renders on the plain path, byte-identical to having no pipeline at all.
- **Material depth and window alpha.** The ground the content sits on is a
  still deep-field material: gradients toward the deep tone from the edges,
  scaled by the ground-depth level. The application requests destination alpha
  before it creates its first window, then enables a translucent ground only
  after its requested alpha setting and scene-graph renderer allow it. The
  renderer mapping treats unknown, software, and null rendering as incapable;
  the requested setting is recorded separately rather than treated as a
  negotiated surface result. The `Off` profile,
  and high contrast select the opaque fallback. Reduced motion leaves this
  still material available. Pane grounds, text, and occluders remain opaque;
  the surface control is an opaque panel-color blend with a 0.45 floor, so
  every text contrast measurement keeps its known bed. Text lift is
  palette-side: chromatic inks multiply toward white, which both brightens
  them and puts them over the bloom threshold.
- **Motion as one token.** Persistence drives a single motion duration the
  views consume for the current-item ring's decay trail. Reduced motion and
  high contrast zero the effective persistence, so every consumer becomes
  instant through the same path the renderer already trusts.
- **Bounded context phosphor.** The active tab, the focused directory pane,
  and the current selected entry add one transparent accent emitter to the
  existing bright pass; the two existing blur chains supply any visible
  phosphor halo. The ordinary outline remains in its owning surface, so
  keyboard and pointer affordances stay crisp when the emitter is absent. A
  context frame collapses active-tab, focus, and selection requests to one
  fixed source level rather than adding their gains. Selection supplies that
  source only at the current selected entry, never once per selected entry,
  so a bulk selection cannot grow either the glow energy or presentation
  cost. The emitter uses the resolved accent token, whose shared render-site
  samples already cover the window, panel, hovered, pressed, and selected
  surfaces; no separate color or contrast calculation exists. Profiles with
  no bloom, high contrast, shader failure, and software fallback omit the
  emitter altogether. It has no animation of its own, while the established
  current-item ring continues to take its duration from the reduced-motion
  token.
- **Silent capability fallback.** On the software scene graph, and on any
  backend where a shader stage fails to build, the pipeline never engages:
  content renders on the plain path with tokens, typography, material
  grounds, and text lift intact, and the controls keep writing stored
  preferences for when a capable backend returns. No warning flood, no
  blank frame, no disabled UI.
- **Popups stay solid.** Menus, dialogs, and the appearance panel render in
  the window overlay above the pipeline by design: modal surfaces are
  occluders and remain solid and color-true.

## Visual validation

The visual foundation is held to an automated acceptance matrix over the real
shell scene, so its claims regress loudly instead of visually:

- **Layout integrity.** At the narrowest supported window (720×480 logical)
  and at a wide layout, every chrome control stays visible, non-degenerate,
  and entirely inside the window and its owning strip; the chrome strips stack
  without overlap and retain their interior band spacing; and no visible label
  overflows its bounds without eliding. Component coverage also exercises each
  independent label threshold at its exact pixel and one pixel below it.
- **Focus visibility.** With every effect off, buttons and fields show the
  accent focus ring against the resting hairline after pointer focus, a
  pointer-focused directory view turns its pane frame stroke to the accent,
  and keyboard tab traversal cycles through the chrome without ever dropping
  focus.
- **Reduced motion and high contrast.** Reduced motion removes persistence and
  transition duration only, keeping static material, bloom, and the context
  marker available for a user who requested less movement rather than less
  luminance. High contrast removes every composed glow, bloom, scanline,
  vignette, and persistence source while retaining palette-side material and
  crisp semantic outlines. The hardware-scene entry begins with visible
  Strong-profile sources, inventories the emitting QML stages, distinguishes
  the two overrides, and requires the inventory to be empty for high contrast
  and `Off`; it requires OpenGL RHI so a software fallback cannot stand in for
  that distinction. Chrome geometry stays byte-stable, and the stored controls
  remain unchanged so leaving an override restores the chosen profile rather
  than a rewritten preference.
- **Effects off.** Selection and text legibility are palette properties
  measured without the pipeline, and the keyboard surfaces stay reachable —
  nothing depends on the effect layer for affordance.
- **Virtualization and flat effect cost.** The deterministic 2,000-entry
  fixture is at least ten times each view's geometry-derived work bound. Both
  views realize a viewport of delegates, not the directory; the effect layer's
  structure and protected-well registry scale with that viewport bound, never
  with entry count or elapsed time.
- **Device pixels.** Well-mask geometry is logical-coordinate math at every
  scale and is exercised by the software validation entries at their declared
  1x and 2x logical scales. The doubled entry asserts that Qt reports the
  declared scale before crediting any layout result; its offscreen frame still
  rasterizes at logical resolution, so that assertion is not device-resolution
  evidence. The same logical-geometry function remains in the RHI entry to keep
  its TestCase filter complete, but it performs no frame grab and contributes
  no GPU evidence. On the GPU path a protected well's entire border is compared
  byte for byte against the plain path, armed by an emitter ring so a mask
  misaligned in any direction fails. A second well, separated from its
  emitter frame by a dark gutter, arms the opposite direction: the ring one
  device pixel outside it must differ from the plain path, so an oversized
  mask — which every inner assertion tolerates — fails there. Every suite
  that compares frame pixels carries a vacuity sentinel: an absolute-value
  assertion on a coordinate the harness fixes, which rejects any
  environment whose grabbed frame does not carry the rendered scene. The
  offscreen platform never allocates a genuine high-density framebuffer —
  under a forced scale factor it reports 2x while rasterizing at 1x — so
  the automated pixel gate runs at 1x and the software scaled-layout entry
  checks logical geometry under a doubled scale factor. Neither is genuine
  2x rendering. The same suite would exercise every assertion at full density
  on a declared, isolated compositor that allocates a real 2x surface; without
  that surface, device-resolution 2x remains explicitly unmeasured.

The high-contrast contrast matrix in the appearance tests and the software
scene-graph fallback suite complete the matrix; the fallback keeps content,
controls, and protected regions intact with the pipeline disengaged. The
fallback entry selects `QT_QUICK_BACKEND=software` and observes the initialized
renderer interface before it accepts the opaque window path, so an ineffective
renderer selector cannot be credited as coverage.
These are independently measured axes: high contrast is not currently crossed
with the layout-breakpoint, focus-traversal, effects-off, or large-directory
virtualization cases. The four device-pixel frame cases skip by design inside
both passing software validation entries and are reported as unexercised there,
rather than being credited through the entry-level pass.

### GPU-path gates and the platform matrix

The frame comparisons run under GPU-path gates that cover distinct platform
axes, each of which either exercises the comparisons or reports a visible skip
— never a silent pass over work it did not perform:

- **Offscreen OpenGL RHI** (`shell_presentation_rhi`, `shell_visual_validation_rhi`,
  `shell_visual_accessibility_rhi`).
  A launcher probes for a usable OpenGL context and, when one exists, runs the
  suite on a real OpenGL scene graph under the offscreen QPA platform. This
  exercises the shaders and readbacks on real hardware, but the offscreen
  platform has no compositor: it never drives first-frame exposure, surface
  configure/commit, or frame-callback pacing, and it cannot allocate a genuine
  high-density framebuffer — under a forced scale factor it reports a doubled
  ratio while rasterizing at 1x — so its device-resolution assertions run at
  1x. When no context is usable the launcher skips (77) with a printed cause
  that distinguishes "no display server reachable" — the offscreen platform
  draws its context from a display server, and none is advertised — from "a
  display is reachable but the OpenGL context is unusable", a driver problem on
  a machine that ought to have had one. `ODYSEA_REQUIRE_OFFSCREEN_GL` turns that
  skip into a failure where an offscreen context is expected; it is deliberately
  distinct from `ODYSEA_REQUIRE_COMPOSITOR`, so a pure-Wayland verifier with no
  X display can require the compositor gate while honestly skipping this one.
  `shell_visual_validation_rhi` and `shell_visual_accessibility_rhi` each pass
  a named `TestCase::function` through to the suite, because
  the validation scope also holds the broader visual cases, which do not belong
  on the GPU path. A filter is a second thing to keep in step, and both ways it
  can drift are silent — an added function left off the list runs nowhere here,
  and a listed function that no longer exists matches nothing, since Qt Quick
  Test accepts a filter matching none. `rhi_function_filter` resolves the entry
  through its launcher's `ODYSEA_PRESENTATION_BINARY` and the suite's
  `QUICK_TEST_SOURCE_DIR` to the QML it actually runs and requires the two to
  agree exactly, per TestCase the filter names.
- **Real compositor** (`shell_presentation_compositor`). A launcher runs the
  full presentation suite only on the isolated Wayland compositor its harness
  declared and proved it owns, with OpenGL RHI forced. Ambient `DISPLAY`,
  `WAYLAND_DISPLAY`, and `QT_QPA_PLATFORM` values never select a platform; the
  launcher declines before Qt creates an application unless the declaration
  names the harness socket. A non-rendering contract exercises unreachable
  ambient X11, Wayland, and mixed endpoints and requires the policy refusal, so
  restoring environment-based selection fails by name without contacting a
  session. This is the only gate that exercises the frame comparisons through
  a compositor's frame lifecycle, which is a distinct path: frame-grabbing
  tests that pass offscreen can fail there. It runs the whole suite rather than
  a named function list, so a newly added presentation test is covered on the
  compositor path without a registration edit.
- **Genuine-2x device resolution** (`shell_visual_validation_compositor_2x`).
  The same real-compositor launcher runs the validation suite with OpenGL RHI
  and `QT_SCALE_FACTOR=2`, so a windowing system that can allocate a 2x surface
  grabs frames at genuine device resolution and the mask-border, ring, and
  interior sweeps run at full density rather than skipping as they do on the
  software scaled-layout pass. The gate declares its scale through
  `ODYSEA_EXPECTED_FRAME_SCALE=2`; the device-resolution test then asserts the
  grabbed frame carries at least twice the logical size, so a run that fell back
  to 1x, or a pipeline that reported a high ratio while rasterizing low, fails
  instead of passing as if it had rendered at full density. The bound is "at
  least" because `QT_SCALE_FACTOR` composes with the screen's own scale, so on a
  fractional output the effective ratio exceeds the forced factor. Where no
  compositor
  can allocate a 2x surface the gate skips (77) exactly like the other
  compositor gate, so the 2x device-resolution claim rests on a run that
  actually reached that surface rather than on an offscreen approximation.
  Until that entry reaches an owned compositor, its strict software-fallback
  failure branch and the presentation suite's real-compositor frame sentinel
  are also unexercised. They remain separate declared evidence gaps: a missing
  framebuffer does not prove that the enforcement which rejects a wrong
  backend has executed.

Which combinations are required versus best-effort:

- **Required where available: an isolated Wayland compositor plus OpenGL RHI.**
  A missing or unproved harness declaration is a policy refusal with exit code
  77, not permission to fall back to an ambient display. Once a proved
  compositor is selected, an unusable OpenGL context is a capability skip with
  a printed reason. Setting `ODYSEA_REQUIRE_COMPOSITOR` turns that inability
  into a failure where the capability is expected; it deliberately does not
  override a declaration refusal.
- **A compositor that actually presents the window.** Whether real frames
  materialized is decided by the suite's vacuity sentinels, not by the
  launcher: a standalone window grab succeeds even where a compositor withholds
  frame callbacks from a background surface, so no launcher-side probe can
  predict the suite's throttled grabs. A harness-owned presenting output runs
  the comparisons; a background window under a compositor that has gone quiet
  trips the sentinels and fails loudly rather than passing over empty frames.
- **Best-effort for paced timing and fractional scaling: a virtual or headless
  compositor.** A virtual output composites each grab completely, so it runs
  the frame comparisons, but it renders at a 1:1 buffer ratio and does not
  reproduce either the display-paced first-frame timing or the fractional
  buffer scale a real presenting display drives. It confirms the comparisons
  execute; it does not stand in for a real display when a bug depends on frame
  pacing or on the grabbed-frame ratio diverging from the reported screen
  ratio.

The coordinate-basis defect the compositor gate was built to catch — a probe
placed with `Screen.devicePixelRatio` instead of the grabbed frame's own ratio,
which misses under Wayland fractional scaling where the two disagree — only
appears on a surface whose frame ratio actually diverges from the screen ratio.
A run at frame ratio equal to the screen ratio (offscreen, X11, a virtual or
integer-scaled compositor) cannot exercise it, so the presentation suite records
the ratio it ran at and, when the two agree, skips the divergence check with the
fractional path named unexercised — a visibly weaker run in the totals rather
than a green pass equal to one that met the divergence. On a divergent surface
it asserts that the frame-derived conversion stays in bounds where the
screen-ratio conversion would have missed.

Under the compositor gates the suite also refuses to skip its GPU assertions:
the launcher exports `ODYSEA_REQUIRE_GPU_FRAMES`, and the suite turns its own
"software scene graph" skips into hard failures when it is set, so a run that
reached the suite on a software fallback cannot report success while every GPU
assertion was skipped.

### Battery coverage and the smoke criterion

A gate that is honest about skipping is only half the guarantee. The other half
is that a skip cannot pass unnoticed: `ctest` reports "100% tests passed"
whether it ran every entry or a third of them, naming skips in a trailing block
below the headline, so the executed count can fall without changing any number a
check watches. A git-less tree skips the corpus guards; a display-less tree
skips the GPU gates; both still read as a clean pass.

The coverage reconciler closes that. `tools/run_verification_battery.sh`
captures the registered roster live from `ctest --show-only=json-v1`, runs the
battery capturing per-entry results as JUnit, and accounts for every registered
entry: an entry that ran, an entry that declared a refusal, an entry that did
not meet a declared precondition, an entry that did not run and declares no
skip capability, a skip that declared no refusal, a silent skip, a registered
entry missing from the results, and a result absent from the roster.

Exactly one shortfall is tolerated, and the distinction it rests on is the one
the compositor gates already draw. A gate that *cannot* run is missing a
capability, and that is a hole in the battery's coverage. A gate that *will
not* run is enforcing a policy — the compositor gates refuse to render an
activating window into a session they were not given to own — and turning that
red would pressure the next reader into deleting the refusal rather than
respecting it. So the reconciler accepts a skip only when the entry printed a
declared refusal, a line of the exact form
`<gate-name>: DECL -- declined: <reason>`, and fails on every other skip,
including one that explained itself. Accepting any skip that printed something
would let a single `echo` reopen the failure class the reconciler exists to
close. Declared refusals are listed with their reasons on every run, and a run
where nothing executed fails regardless, so a declaration covers one entry and
never a battery.

That standard has a price, and it is paid deliberately: a machine where the
offscreen GPU launchers cannot obtain a context does not produce a green
battery. The answer there is a virtual display, not a wider tolerance.

Every judgement so far is made after a skip has happened, and that left the
mechanism itself unwatched. An entry could be given `SKIP_RETURN_CODE` in the
build configuration and the reconciler would meet the fact on the first run
where it dropped out — and if that skip printed the refusal line, it was
tolerated from then on without anyone having decided it should be. An honest
skip and a completed check read identically in a summary, so the defect was
never the skip mechanism; it was a skip the reconciler did not know existed.

The roster therefore carries each entry's skip return code, taken from the same
listing that names it, and `tools/skip_declarations.txt` records every entry
allowed to have one. The two are reconciled before a result is read: an entry
that can skip without a declaration fails, a declaration naming no registered
entry fails, a declaration naming an entry that carries no skip return code
fails so it cannot outlive its mechanism, and a skip return code other than the
project's own fails so no second convention can appear beside the declared one.
A declaration records one of two tolerances. `refusal` is the shortfall
described above. `capability` records a precondition the entry cannot satisfy
itself, and a skip under it fails with that precondition printed: the skip
return code still earns its place, because the entry then reports as not-run
rather than as a broken test and the reconciler is what turns the totals red,
which is where every other coverage hole in this battery is decided.

The bound is two-sided — an empty roster, an empty results file, and results
that merely do not exceed the roster all fail — so a stopped counter cannot
satisfy it. The reconciliation runs after `ctest` returns rather than as one
more entry inside the battery, because an in-battery reconciler would run
mid-run under `-j` and could not see the entries scheduled after itself. What
it cannot do is judge whether a declared refusal was warranted; that stays with
the gate, where the refusal and the inability are separate exit paths with
different text, and only the inability is turned red by
`ODYSEA_REQUIRE_COMPOSITOR`.

The application smoke is subject to the same standard of evidence. "Zero bytes
on both streams" was accepted as a clean launch for the life of the project,
but under redirection Qt routes its diagnostics to the journal, so a healthy
launch, a silently non-functional one, and a core dump are byte-identical. The
`application_smoke` gate forces `QT_FORCE_STDERR_LOGGING=1` so a fault names
itself and records the watchdog's harness-owned termination of a process that
survived the observation window. An early exit — including status 124 — or a
signal death fails by name, as does a live process reporting a platform-plugin,
RHI, scene-graph, or sanitizer fault. It removes ambient display variables,
redirects XDG storage and the opened directory into a temporary workspace, and
runs on the offscreen software scene graph, selected with
`QT_QUICK_BACKEND=software` — the real software key, where
`QSG_RHI_BACKEND=software` is not a valid key and silently falls back to the
default — which renders at device pixel ratio 1 and runs with or without a
display. A launch on a real GPU path is the compositor gate's stronger and
separate job, not a second offscreen smoke run twice.

## Rendering

Qt Quick renders through a hardware-accelerated scene graph. On Linux it drives
that via Qt's Rendering Hardware Interface (RHI), which can target OpenGL
(the default and most broadly available), Vulkan, or a software fallback, and
can be overridden with an environment variable. This makes the app robust across
diverse driver stacks while still allowing custom shader effects (QML shader
effects, or a custom RHI/Vulkan pass) for the visual identity.

## Performance principles

- Directory I/O and metadata resolution run off the UI thread; the interface
  renders whatever is available and fills in asynchronously.
- Per-entry metadata stays at one syscall. Entry identity needs a creation
  time, which `lstat` cannot report, so listing uses `statx` instead — the same
  single call per entry, requesting the creation time alongside the fields
  `lstat` already supplied. Strengthening identity therefore costs no extra
  syscalls, no allocation, and no second pass over the directory; comparing two
  identities remains a comparison of integers. A kernel that does not provide
  `statx` falls back to `lstat` and leaves the creation time unknown.
- Refresh batches retain previously published entries until the completed scan
  identifies removals. Inserts, removals, metadata changes, and sorting publish
  granular model signals instead of invalidating the entire view.
- Large directories use a virtualized view (`ListView`/`GridView` only realize
  visible delegates), so entry count does not gate frame time.
- Thumbnails are generated lazily, cached, and cancellable when navigation moves
  on. Deciding what to thumbnail, in what order, and when a cached one is stale
  is core work; turning a file into pixels is not, because that needs an image
  codec the presentation layer already links. The two meet at a pair of
  interfaces free of toolkit types, which is what keeps the scheduling, the
  memory bound, and the cancellation behaviour verifiable without a display
  server.
- The application decoder accepts a conservative set of web image formats and
  rejects oversized dimensions and decoded byte costs before allocating pixel
  buffers. Worker results return through a thread-safe image provider only
  after the current navigation generation, stable entry path, and full cache
  key all match. Model row numbers are never asynchronous identity.

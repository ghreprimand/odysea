# Design Overview

OdySea is a local-first, GPU-accelerated file manager. This document records the
product intent and the high-level architecture the implementation targets.

## Positioning

The Linux file-manager landscape spans a wide range:

- **Environment file managers** — Dolphin (KDE), Nautilus (GNOME), Thunar
  (Xfce). Feature-complete and well integrated with their desktop environments,
  but tied to those environments' conventions.
- **Terminal file managers** — Yazi, nnn, ranger, Midnight Commander. Extremely
  fast and keyboard-driven, but constrained by the terminal grid and text-only
  rendering.
- **Data-platform explorers** — e.g. content-indexed virtual filesystems that
  span multiple devices and cloud backends.

OdySea occupies the gap between the first two categories: the ergonomics and
speed of a terminal file manager, delivered through a graphical, GPU-rendered
surface with proper thumbnails, selection models, and drag-and-drop. It does not
pursue the data-platform direction; its scope is a fast, local browser that is
equally comfortable driven by keyboard or mouse.

## Explicitly not

- **Not a TUI or a keyboard-only tool.** The pointer experience is a first-class
  target, equal to the keyboard — full graphical rendering, icons, thumbnails,
  drag-and-drop, and context menus.
- **Not modal-by-default.** Vim-style modal input is opt-in, never forced.
- **Not a cross-device virtual filesystem or cloud sync platform.**

## Interaction model

**Principle: full input parity.** OdySea is a graphical desktop application, not a
terminal UI with a mouse bolted on. The keyboard and the pointer are two
complete, first-class input paths, and neither is allowed to compromise the
other. A mouse-driven user should find OdySea as natural as any mainstream
desktop file manager; a keyboard-driven user should find it as fast as a
terminal file manager. Achieving both at once is a deliberate design target.

- **Keyboard.** Every action is reachable without the mouse. A command palette
  provides discoverable, searchable commands; type-ahead jumps to entries; arrow
  and Home/End keys navigate; standard shortcuts cover rename, trash, copy,
  move, and selection. An optional modal (vim-style) mode is available but is
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
  view alongside the list and grid modes. Workspace navigation and tab history
  remain rooted in the directory from which the columns chain opened, while
  entry operations follow the focused view: copy, move, rename, trash,
  selection status, dialogs, and cross-pane transfers resolve against the
  active column listing. The active column path is shown in the pane header.
  This split preserves an exploratory path chain without allowing a shortcut
  to act on an invisible selection retained by another view.
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
  move, and rename, freedesktop trash support, and thumbnail cache policy and
  scheduling. It has no GUI dependency and is unit-tested headless. This is
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
  match, and status inks. Six curated families ship, `odyssey-default` first;
  each family's values are derived so every ink clears its contrast floor on
  the surfaces it renders over, directory ink stays measurably chromatic, and
  the selection bed remains distinct from the ground. An unknown family
  identifier resolves to the default rather than rendering nothing.
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
- **Adaptive chrome density.** Below a measured width bound the toolbar's
  workspace toggles and the action row's operation buttons drop their labels
  and render icons only, and the filter field narrows, so every control
  stays visible, reachable, and unclipped down to the window's minimum size
  (720×480 logical). The labels remain available through accessible names
  and tooltips, and the bound scales with the interface.
- **Screen-effect profiles.** `Off`, `Minimal`, `Balanced` (shipped default),
  and `Strong` are fixed presets over the effect levels: core and wide bloom,
  scanlines, vignette, persistence, ground depth, and text lift. `Custom`
  renders the user's own stored levels; moving any effect control switches to
  it and the adjustments survive preset round-trips. The levels exist in two
  views with distinct consumers: the controls display and edit the *stored*
  preference, while the rendering pipeline consumes the *effective* levels —
  the stored ones after the accessibility overrides. Reduced motion zeroes
  effective persistence; high contrast zeroes the effective gains that
  modulate legibility, pins effective text lift, and promotes the muted and
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
  weight under high contrast. Normal dark-palette icon ink remains below the
  Strong bright-pass threshold so orientation chrome does not become an
  unintended emitter; high contrast deliberately promotes it to primary text.
  The ink under that cap is a curated per-family hue rather than a derivation
  from the faint text ink: the cap limits the brightest channel, so a family
  whose faint ink peaks in a low-luminance channel would land its capped
  symbols below the measured pressed-bed floor, and the neutral families
  choose a hue whose capped form stays measurable on every control bed.
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
- **Material depth without a compositor.** The ground the content sits on is
  a still deep-field material: gradients toward the deep tone from the
  edges, scaled by the ground-depth level. The glass amount fades only that
  ground; the surface amount fades only chrome strips. Text and occluders
  stay opaque, so translucency reads as depth and never thins legibility.
  Text lift is palette-side: chromatic inks multiply toward white, which
  both brightens them and puts them over the bloom threshold.
- **Motion as one token.** Persistence drives a single motion duration the
  views consume for the current-item ring's decay trail. Reduced motion
  zeroes the effective persistence, so every consumer becomes instant
  through the same path the renderer already trusts.
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
  and entirely inside the window; the chrome strips stack without overlap;
  and no visible label overflows its bounds without eliding.
- **Focus visibility.** With every effect off, buttons and fields show the
  accent focus ring against the resting hairline, a focused directory view
  turns its pane frame stroke to the accent, and tab traversal cycles
  through the chrome without ever dropping focus.
- **Reduced motion.** The override zeroes effective persistence and the
  shared motion token while chrome geometry stays byte-stable, and the
  stored preference the controls display is untouched.
- **Effects off.** Selection and text legibility are palette properties
  measured without the pipeline, and the keyboard surfaces stay reachable —
  nothing depends on the effect layer for affordance.
- **Virtualization and flat effect cost.** A directory large enough to
  exercise virtualization realizes a viewport of delegates, not the
  directory, in both views; the effect layer's structure and the
  protected-well registry scale with the viewport at most, never with the
  entry count.
- **Device pixels.** Well-mask geometry is logical-coordinate math at every
  scale, and on the GPU path a protected well's entire border is compared
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
  the automated pixel gate runs at 1x, the layout and geometry assertions
  run at both scales, and the same suite executed on a windowing system
  with a real 2x surface exercises every assertion at full density.

The high-contrast contrast matrix in the appearance tests and the software
scene-graph fallback suite complete the matrix; the fallback keeps content,
controls, and protected regions intact with the pipeline disengaged.

### GPU-path gates and the platform matrix

The frame comparisons run under GPU-path gates that cover distinct platform
axes, each of which either exercises the comparisons or reports a visible skip
— never a silent pass over work it did not perform:

- **Offscreen OpenGL RHI** (`shell_presentation_rhi`, `shell_visual_validation_rhi`).
  A launcher probes for a usable OpenGL context and, when one exists, runs the
  suite on a real OpenGL scene graph under the offscreen QPA platform. This
  exercises the shaders and readbacks on real hardware, but the offscreen
  platform has no compositor: it never drives first-frame exposure, surface
  configure/commit, or frame-callback pacing, and it cannot allocate a genuine
  high-density framebuffer — under a forced scale factor it reports a doubled
  ratio while rasterizing at 1x — so its device-resolution assertions run at
  1x.
- **Real compositor** (`shell_presentation_compositor`). A launcher runs the
  full presentation suite on the ambient real compositor — Wayland preferred,
  X11 as the fallback display server — with OpenGL RHI forced. This is the only
  gate that exercises the frame comparisons through a compositor's frame
  lifecycle, which is a distinct path: frame-grabbing tests that pass offscreen
  can fail there. It runs the whole suite rather than a named function list, so
  a newly added presentation test is covered on the compositor path without a
  registration edit.
- **Genuine-2x device resolution** (`shell_visual_validation_compositor_2x`).
  The same real-compositor launcher runs the validation suite with OpenGL RHI
  and `QT_SCALE_FACTOR=2`, so a windowing system that can allocate a 2x surface
  grabs frames at genuine device resolution and the mask-border, ring, and
  interior sweeps run at full density rather than skipping as they do on the
  software 2x pass. The gate declares its scale through
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

Which combinations are required versus best-effort:

- **Required where available: a real compositor plus OpenGL RHI.** The
  compositor gate skips with exit code 77 and a printed reason when no
  compositor is advertised or no OpenGL context is usable, so a machine that
  cannot render the gate is visibly skipped, distinguishable at a glance from a
  pass. Setting `ODYSEA_REQUIRE_COMPOSITOR` turns that skip into a failure,
  which is how an environment that is expected to have a compositor refuses to
  let an unrun gate read as green.
- **A compositor that actually presents the window.** Whether real frames
  materialized is decided by the suite's vacuity sentinels, not by the
  launcher: a standalone window grab succeeds even where a compositor withholds
  frame callbacks from a background surface, so no launcher-side probe can
  predict the suite's throttled grabs. An interactive session or a
  nested/virtual output presents frames and the comparisons run; a background
  window under a compositor that has gone quiet (a locked screen, an inactive
  output) trips the sentinels and fails loudly rather than passing over empty
  frames.
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

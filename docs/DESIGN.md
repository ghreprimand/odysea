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
- **Parity rule for contributors.** No primary action may be keyboard-only or
  mouse-only. When a feature adds an interaction, it is wired for both paths.
- **Views.** Planned view models include a single-pane list/grid, a dual-pane
  layout for transfers, and miller/columns navigation for deep trees.
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
- **`app/` (Qt Quick).** The GPU-rendered shell: the QML scene, input handling,
  theming, and thumbnail decoding and presentation. A thin adapter
  (`DirectoryListModel`, a `QAbstractListModel`) is the single boundary that
  exposes the core to QML. It marshals scanner and watcher callbacks onto the UI
  thread, schedules mutations away from it, and reconciles presentation changes
  through incremental row, data, and layout signals keyed by directory-entry
  identity. Default-application launching is an injectable application-layer
  service, so tests record launch requests without invoking desktop programs;
  the core itself never includes a Qt header.
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
- **Versioned persistence.** Preferences serialize to a small key=value file.
  The write goes through a sibling temporary file and a rename, so a reader
  never observes a partially written file; durability across power loss and
  coordination between concurrent writers are out of scope for appearance
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
  the chrome around them keeps its depth.
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

# Roadmap

Milestones are ordered but not dated. Each builds on a testable core before the
graphical shell grows around it.

## M0 — Core foundation (in progress)

- [x] Toolkit-agnostic filesystem model (`core/`): entries, metadata, sorting.
- [x] Headless test suite, runnable under AddressSanitizer.
- [x] Minimal GPU-rendered listing view (Qt Quick `ListView`).
- [x] Filesystem operations: copy, move, rename, delete (to trash).
- [x] Directory watch and incremental refresh.

## M1 — Navigation state

- [x] Navigation history (back/forward/up) and current-location model.
- [x] Selection model (single, range, toggle) — driven by both keyboard
      (space/shift/ctrl+arrows) and mouse (click, ctrl/shift-click, rubber-band).
- [x] Sort and filter settings; hidden-file toggle in the UI.
- [x] Tab and pane model.

## M2 — Shell views

- [x] Incremental adapter updates and geometry-driven selection foundation.
- [x] Grid view with asynchronous, cached thumbnails.
- [x] Off-thread directory scanning with main-thread reconciliation that stays
      proportional to the listing, so navigating and filtering a large
      directory does not stall the interface.
- [x] Keyboard navigation and type-ahead.
- [x] Mouse interaction parity: double-click open, context menus,
      drag-and-drop (internal + drag-out), clickable breadcrumbs.
- [x] Calm path orientation, direct entry and completion, configurable Places,
      bounded recent destinations, and direct ancestor/shortcut jumps.
- [x] Command palette.
- [x] Visual foundation acceptance: layout integrity across every density's
      measured compact breakpoint, focus visibility, reduced motion, contrast
      measured at the surfaces the views actually paint, effects-off
      usability, software fallback, and large-directory validation. Rendering
      at a doubled device pixel ratio is exercised by a real-compositor gate
      at forced 2x, which grabs frames at genuine device resolution where a
      windowing system can allocate a high-density surface and skips where
      none is available rather than approximating it offscreen; the offscreen
      GPU validation entry runs at a single device pixel per logical pixel and
      the software doubled-scale entry checks logical-coordinate layout only.

## M3 — Power features

- [x] Dual-pane layout for transfers.
- [ ] Miller/columns view. Entry actions now resolve against the focused
      column listing and the pane header names it, so an operation cannot
      reach a selection held invisibly by another view. The shell's location
      still does not follow the column chain: the workspace keeps the tabs,
      history, and location the chain began from.
- [x] Interactive storage-usage maps with cancellable scanning and accessible
      list equivalents.
- [x] Hardened filesystem entry identity for Btrfs subvolume roots.
- [ ] Capability-gated filesystem tools for mounts, quotas, subvolumes, and
      Btrfs snapshot discovery.
- [ ] Built-in fuzzy find across the current tree.
- [ ] Optional modal (vim-style) keybindings.

## M4 — Terminal integration

- [ ] Open a terminal at the current location.
- [ ] Run a command against the current selection.
- [ ] Shared theming with the Odyssey visual identity.

## M5 — Packaging and polish

- [ ] Configuration file and in-app settings.
- [ ] Desktop entry, MIME/default-handler integration.
- [ ] Packaged releases.

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
- [ ] Off-thread directory scanning; main-thread reconciliation remains to be
      hardened for large directories.
- [x] Keyboard navigation and type-ahead.
- [x] Mouse interaction parity: double-click open, context menus,
      drag-and-drop (internal + drag-out), clickable breadcrumbs.
- [x] Calm path orientation, direct entry and completion, configurable Places,
      bounded recent destinations, and direct ancestor/shortcut jumps.
- [x] Command palette.
- [ ] Visual foundation acceptance: scale behavior, focus visibility, reduced
      motion, effects-off usability, software fallback, and large-directory
      validation hold. Two gaps remain: chrome overflows its window in a band
      of widths just above the compact breakpoint, and the contrast floors are
      measured against role pairs that no view paints, leaving four rendered
      pairs below their stated floor.

## M3 — Power features

- [x] Dual-pane layout for transfers.
- [ ] Miller/columns view.
- [ ] Interactive storage-usage maps with cancellable scanning and accessible
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

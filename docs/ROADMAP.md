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
- [x] Application identity mark and scalable desktop icon, with a monochrome
      symbolic form and measured 16–48 pixel rendering at 1x and 2x.
- [ ] Visual foundation acceptance. Automated coverage holds layout integrity
      across every density's measured compact breakpoint, pointer and keyboard
      focus visibility, reduced motion, contrast at the surfaces the views
      actually paint, effects-off usability, window-alpha and software
      fallback, and large-directory behavior. Genuine doubled-device-pixel
      rendering remains unmeasured without a declared, isolated compositor
      that can allocate a 2x surface; the offscreen GPU entry renders at 1x
      and the software scaled-layout entry checks logical coordinates only.

## M3 — Power features

- [x] Dual-pane layout for transfers.
- [x] Miller/columns view. The focused column drives the pane's workspace
      location, tabs, history, path navigator, breadcrumbs, and pane header.
      Entry actions still resolve against the focused column listing, so an
      operation cannot reach a selection held invisibly by another view.
- [x] Interactive storage-usage maps with cancellable scanning and accessible
      list equivalents.
- [x] Hardened filesystem entry identity for Btrfs subvolume roots.
- [x] Undoable operation journal. One shared shell action reaches the active
      listing adapter from `Ctrl+Z`, the toolbar, the canvas context menu, and
      the command palette. Its availability and disabled reason follow the
      adapter's current journal state, and it remains unavailable while a
      filesystem operation is active.
- [ ] Capability-gated filesystem tools for mounts, quotas, subvolumes, and
      Btrfs snapshot discovery.
- [x] Built-in fuzzy find across the current tree. Matching and ranking are
      Qt-free and cancellable; the overlay is reachable by keyboard shortcut
      and through the shared action surfaces, and results open by keyboard or
      by pointer.
- [ ] Optional modal keybindings.

## M4 — Terminal integration

- [ ] Open a terminal at the current location.
- [ ] Run a command against the current selection.
- [ ] Shared theming with the Odyssey visual identity.

## M5 — Packaging and polish

- [ ] Configuration file and in-app settings.
- [ ] Desktop entry, MIME/default-handler integration.
- [ ] Packaged releases.

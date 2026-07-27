# Roadmap

Milestones are ordered but not dated. Each builds on a testable core before the
graphical shell grows around it.

## M0 — Core foundation (in progress)

- [x] Toolkit-agnostic filesystem model (`core/`): entries, metadata, sorting.
- [x] Headless test suite, runnable under AddressSanitizer.
- [x] Minimal GPU-rendered listing view (Qt Quick `ListView`).
- [ ] Filesystem operations: copy, move, rename, delete (to trash).
- [ ] Directory watch and incremental refresh.

## M1 — Navigation state

- [ ] Navigation history (back/forward/up) and current-location model.
- [ ] Selection model (single, range, toggle) — driven by both keyboard
      (space/shift/ctrl+arrows) and mouse (click, ctrl/shift-click, rubber-band).
- [ ] Sort and filter settings; hidden-file toggle in the UI.
- [ ] Tab and pane model.

## M2 — Shell views

- [ ] Grid view with asynchronous, cached thumbnails.
- [ ] Off-thread directory scanning so the UI never stalls.
- [ ] Keyboard navigation and type-ahead.
- [ ] Mouse interaction parity: double-click open, context menus,
      drag-and-drop (internal + drag-out), clickable breadcrumbs.
- [ ] Command palette.

## M3 — Power features

- [ ] Dual-pane layout for transfers.
- [ ] Miller/columns view.
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

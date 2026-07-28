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
- **Mouse.** Fully first-class and complete, matching modern desktop
  expectations: single-click select, double-click open, ctrl/shift and
  rubber-band (drag-rectangle) multi-select, internal and drag-out
  drag-and-drop, right-click context menus, hover states, back/forward buttons,
  scroll, resizable columns/panes, and clickable breadcrumbs.
- **Parity rule for contributors.** No primary action may be keyboard-only or
  mouse-only. When a feature adds an interaction, it is wired for both paths.
- **Views.** Planned view models include a single-pane list/grid, a dual-pane
  layout for transfers, and miller/columns navigation for deep trees.
- **Geometric selection.** Each view computes the explicit set of model rows
  intersected by its rubber-band rectangle. The shared selection model applies
  that set by stable entry key, so list and grid geometry do not leak into
  selection state.
- **Terminal bridge.** Opening a terminal at the current directory, and running a
  command against the current selection, are core interactions.

## Architecture

The codebase separates a toolkit-agnostic core from the presentation layer:

- **`core/` (pure C++20, no Qt).** Directory reading, stable entry identity,
  cancellable scanning, incremental directory watching, transactional copy,
  move, and rename, and freedesktop trash support. It has no GUI dependency and
  is unit-tested headless. This is where the performance-critical work lives,
  deliberately free of framework overhead.
- **`app/` (Qt Quick).** The GPU-rendered shell: the QML scene, input handling,
  theming, and (later) thumbnail presentation. A thin adapter
  (`DirectoryListModel`, a `QAbstractListModel`) is the single boundary that
  exposes the core to QML. It marshals scanner and watcher callbacks onto the UI
  thread, schedules mutations away from it, and reconciles presentation changes
  through incremental row, data, and layout signals keyed by directory-entry
  identity; the core itself never includes a Qt header.
- **`tests/` (pure C++).** Headless verification of the core, runnable under
  AddressSanitizer.

Keeping the core free of Qt types means it stays fast, portable, and testable,
and that the presentation layer can evolve independently.

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
  on.

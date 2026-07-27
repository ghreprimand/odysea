# OdySea

**A fast file manager for modern Linux desktops, with full keyboard and mouse parity.**

OdySea is a GPU-accelerated file manager written in modern C++ with a Qt Quick
interface. It is part of the Odyssey application family (alongside the OdyTTY
terminal) and is built around three priorities: speed on very large directories,
complete keyboard *and* pointer interaction (neither treated as an
afterthought), and tight integration with the terminal.

It is a full graphical desktop application — icons, thumbnails, drag-and-drop,
context menus — not a terminal UI. The "keyboard" emphasis means every action is
*also* reachable from the keyboard, so power users never have to reach for the
mouse; it does not mean the mouse is second-class.

> **Status:** early development. The toolkit-agnostic filesystem core and a
> minimal GPU-rendered listing view are in place; the richer shell is under
> active design. Not yet ready for daily use.

## Why OdySea

Desktop file managers tend to sit at two extremes: feature-dense environment
suites, or minimal terminal tools. OdySea aims for the middle ground — the speed
and keyboard ergonomics of a terminal file manager, presented through a
GPU-rendered graphical surface where mouse and keyboard are equally
first-class — thumbnails, rubber-band selection, drag-and-drop, and context
menus on one hand; a command palette, type-ahead, and a shortcut for every
action on the other. It is local-first and Linux-focused.

## Goals

- **Fast.** Smooth navigation of directories with 100k+ entries, asynchronous
  metadata and thumbnailing, and no UI stalls during I/O.
- **Full input parity.** Every action is reachable from both the keyboard and
  the mouse. Keyboard: a command palette, type-ahead navigation, and optional
  (off-by-default) modal keybindings. Mouse: single/multi-select, rubber-band
  selection, drag-and-drop, context menus, and breadcrumb navigation. Neither
  path is an afterthought.
- **Terminal-native.** Open a shell at the current location and act on selections
  from the keyboard.
- **GPU-rendered.** Built on Qt Quick's hardware-accelerated scene graph, with
  room to drop to custom shaders for the visual identity.
- **Focused.** A local browser that does the common operations extremely well.

## Non-goals

- A cloud or peer-to-peer virtual filesystem, or cross-device sync.
- An embedded web browser or general-purpose plugin platform.
- A drop-in clone of any specific existing file manager.

## Architecture at a glance

The code separates a **toolkit-agnostic C++ core** (`core/`) from the **Qt Quick
presentation layer** (`app/`). The core has no Qt dependency and is unit-tested
headless, keeping the performance-critical filesystem model free of framework
overhead. See [docs/DESIGN.md](docs/DESIGN.md).

## Build

Requires CMake 3.28+, a C++20 compiler, and Qt 6.6+ (Qt Quick).

```sh
git clone https://github.com/ghreprimand/odysea.git
cd odysea
cmake --preset release
cmake --build build/release
ctest --preset release
./build/release/app/odysea ~     # launch on a directory
```

An AddressSanitizer/UBSan build is available for development:

```sh
cmake --preset asan && cmake --build build/asan && ctest --preset asan
```

## Documentation

- [Design overview](docs/DESIGN.md)
- [Technology stack](docs/STACK.md)
- [Roadmap](docs/ROADMAP.md)
- [Development log](DEVLOG.md)
- [Contributing](CONTRIBUTING.md)

## License

Licensed under the GNU General Public License v3.0 only. See [LICENSE](LICENSE).

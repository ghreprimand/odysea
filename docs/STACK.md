# Technology Stack

This document records the technology decisions and the reasoning behind them.

## Language: modern C++ (C++20)

OdySea is written in C++20. C++ is the native language of the desktop
file-manager and GPU-rendering ecosystems (Qt, Vulkan, Skia), gives direct
access to graphics APIs, and compiles to efficient native code with no runtime
garbage collector — predictable latency for a UI that must stay smooth while
scanning large directories.

Memory safety is treated as an explicit engineering discipline rather than an
afterthought; see [Memory safety](#memory-safety) below.

- Standard: C++20
- Toolchain: CMake 3.28+, Ninja, Clang (release) / GCC (sanitizer build)

## GUI: Qt Quick (Qt 6.6+)

The graphical shell is built with Qt Quick:

- **GPU-rendered scene graph.** Qt Quick renders on the GPU by default through
  Qt's Rendering Hardware Interface, which targets OpenGL, Vulkan, or a software
  fallback and degrades gracefully across driver stacks.
- **Cross-distro robustness.** Defaulting to widely available OpenGL (with a
  software fallback and an environment-variable override) avoids the class of
  "no usable GPU backend" failures that a Vulkan-only renderer can hit on
  unusual driver setups.
- **Declarative UI with escape hatches.** QML expresses the interface concisely,
  while shader effects and custom RHI passes remain available for the heavier
  visuals.
- **Native fit.** Qt is the toolkit of the KDE ecosystem, so the app integrates
  naturally on Plasma while remaining independent of any specific environment.
- **Bounded image decoding.** `QImageReader` and `QImageWriter` supply the
  application-layer codecs and freedesktop PNG metadata. Header dimensions and
  decoded byte cost are bounded before decoding, while scheduling and cache
  policy remain in the Qt-free core.

The core (`core/`) has no Qt dependency; Qt is confined to the `app/` layer.

### QML quality gates

The QML surface uses `qmlformat` and `qmllint` 6.10 as verified development
tools. Repository-owned formatter settings define four-space indentation, Unix
newlines, explicit semicolons, and stable source ordering. Verification compares
every tracked QML file with formatter output and rejects all lint warnings.
These checks run through CTest in both release and sanitizer configurations.

"Stable source ordering" is a deliberate setting, not an oversight. The
formatter's property-normalization and import-sorting passes are both turned
off, so declaration and import order is the author's and the gate accepts it as
written. Order in a QML scene carries meaning the formatter cannot see:
identifiers and required properties read first, related bindings stay grouped,
and layered children keep the stacking order their positions depend on.
Reordering is also the formatter's least stable behavior across releases, which
would make a toolchain upgrade rewrite the corpus. Turning either setting on
rewrites every tracked scene at once — a change with no behavioral gain that
would collide with any concurrent work on the same files — so the settings file
is changed only as a deliberate, separately reviewed decision.

The shell itself is a linkable QML module. The application and the
rendered-shell tests both link it and load scenes through the `OdySea` module
rather than by relative source path, so a scene the module does not export
fails the tests instead of only failing at startup. A further gate compares the
tracked scene corpus against the manifest the build produced, in both
directions, so an omission or a leftover entry is caught without running the
application.

## Memory safety

C++ places memory management in the developer's hands. OdySea addresses this with
layered guardrails, wired in from the first commit:

- **Modern C++ discipline.** RAII, standard containers, and smart pointers
  (`std::unique_ptr`/`std::shared_ptr`) instead of raw owning pointers and
  manual `new`/`delete`. Qt's parent-child object ownership handles GUI object
  lifetimes.
- **Warnings as errors.** `-Wall -Wextra -Wpedantic -Werror` plus conversion and
  shadowing warnings — the compiler is the first line of defense.
- **AddressSanitizer + UBSan.** The `asan` CMake preset builds with
  `-fsanitize=address,undefined`; the headless core tests run under it, so
  use-after-free, buffer overflow, and leaks surface with an exact stack trace.
- **Static analysis.** A `.clang-tidy` policy enforces a subset of the C++ Core
  Guidelines (owning-memory and no-malloc checks are errors).

### Toolchain note

On this build environment the release preset uses `clang++`, while the `asan`
preset uses `g++`, because the installed Clang's compiler-rt sanitizer runtime
is not present but GCC's `libasan` is. Both compile the same C++20 code; the
sanitizer build simply selects the compiler whose runtime is available.

## Platform

Linux and Wayland are the primary target, with multi-monitor awareness; X11
works through the toolkit's compatibility layer. Qt is cross-platform, so other
operating systems are not foreclosed, but the deep desktop integration a file
manager needs is inherently platform-specific and is scoped to Linux first.

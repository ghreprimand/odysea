# Contributing

## Prerequisites

- CMake 3.28+ and Ninja
- A C++20 compiler (Clang or GCC)
- Qt 6.6+ with Qt Quick (`Core`, `Gui`, `Qml`, `Quick`)

## Build and test

```sh
cmake --preset release
cmake --build build/release
ctest --preset release
./build/release/app/odysea ~        # run on a directory

# Development build with sanitizers:
cmake --preset asan
cmake --build build/asan
ctest --preset asan                # core tests run under ASan/UBSan
```

## Project layout

- `core/` — toolkit-agnostic C++20 filesystem model. **No Qt or GUI types here.**
  It must remain unit-testable without a display server.
- `app/` — the Qt Quick shell (`main.cpp`, the `DirectoryListModel` adapter, and
  `qml/`). This is the only place Qt is used.
- `tests/` — headless core tests (a dependency-free assertion harness).
- `docs/` — design, stack, and roadmap documentation.

## Memory-safety rules

These are not optional; they are how the project stays safe in C++:

1. **No raw owning pointers.** Use RAII, standard containers, and smart pointers.
   In the Qt layer, use QObject parent-child ownership. Never hand-write `new` /
   `delete` / `malloc` for ownership.
2. **Keep the core Qt-free.** `core/` must not include a Qt header. Cross the
   boundary only through the adapter in `app/`.
3. **Run the sanitizer build** (`ctest --preset asan`) before submitting changes
   that touch the core.
4. **New core behavior comes with a test.**

## Style

- `clang-format` (config in `.clang-format`) and the `.clang-tidy` policy apply.
- Warnings are errors (`-Werror`); keep the build clean.
- Prefer small, reviewable commits with descriptive messages.

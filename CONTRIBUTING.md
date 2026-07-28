# Contributing

## Prerequisites

- CMake 3.28+ and Ninja
- A C++20 compiler (Clang or GCC)
- `clang-format` 22 and `clang-tidy` 22
- Qt 6.6+ with Qt Quick (`Core`, `Gui`, `Qml`, `Quick`)
- `qmlformat` and `qmllint` 6.10 for the QML quality gates

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

# Public tracked/staged-content safety guard:
./tools/check_public_repo.sh

# Formatting and static-analysis gates:
./tools/check_format.sh
./tools/check_qml.sh all
./tools/check_clang_tidy.sh build/release

# Tracked-file length ceiling:
./tools/check_file_length.sh
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

- `clang-format` 22 (config in `.clang-format`) and `clang-tidy` 22 (policy in
  `.clang-tidy`) are the verified tool versions. Static analysis runs from the
  Clang release compilation database; the GCC sanitizer preset provides the
  separate ASan/UBSan runtime gate.
- `qmlformat` and `qmllint` 6.10 enforce the declarative shell baseline. The
  tracked `.qmlformat.ini` is canonical, and lint warnings fail the gate.
- Warnings are errors (`-Werror`); keep the build clean.
- No tracked source or text file may exceed 2,000 physical lines. Split files
  along clear ownership or responsibility boundaries before reaching the
  ceiling; do not compress readable code or prose to evade the limit. Keep
  `file_length_guard` green.
- Prefer small, reviewable commits with descriptive messages.

## Public repository safety

This repository is public. Treat every tracked file, commit message, branch, and
release artifact as published material.

- Never commit secrets, credentials, tokens, private keys, private hostnames or
  URLs, personal data, machine-local configuration, private notes, or diagnostic
  captures.
- Use synthetic, neutral values in examples and tests. Do not use real email
  addresses, user/host pairs, home-directory paths, or private infrastructure.
- Keep `.env*`, credentials, key material, local logs, reports, `.archon/`, and
  machine-local contributor instructions untracked.
- Configure a repository-local Git author name and no-reply address intended for
  public attribution. Never publish a personal email address through commit
  metadata.
- Inspect staged changes before every commit. `.gitignore` is a backstop, not a
  security boundary; stop and investigate any ambiguous content.
- Keep `public_repository_guard` green. It rejects tracked secret-file names,
  private-key and common token signatures, personal home paths, private-network
  references, internal workflow narration, unsafe commit attribution,
  `Co-Authored-By` trailers, and at-signs in tracked text. Public clone links use
  HTTPS so email-like syntax is unnecessary.

## Engineering record

`DEVLOG.md`, documentation, comments, and commit messages form a public
engineering record. Describe the behavior, implementation, decisions,
verification, and known gaps in the project's voice. Do not narrate work
assignments or use internal workflow terminology in tracked text.

Update `DEVLOG.md` with each accepted development milestone, in the same change
as the code or documentation it records. Commit subjects and bodies stay
factual and scoped. Do not add `Co-Authored-By` trailers.

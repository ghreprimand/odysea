# OdySea — Devlog

Public running record of OdySea development, in reverse-chronological order.
Each entry records what landed, how it was verified, and any known gaps. See
`docs/ROADMAP.md` for milestone status and `docs/DESIGN.md` for durable product
and architecture decisions.

---

## 2026-07-27 -- Public development record and repository safeguards

The repository now carries a public development log and explicit publishing
boundaries. Contributor guidance requires staged-change inspection, synthetic
fixtures, impersonal engineering prose, and exclusion of secrets, personal
data, private infrastructure, machine-local configuration, diagnostic captures,
and internal workflow artifacts.

Ignore rules cover common environment files, credentials, private keys, local
logs, crash captures, editor state, build output, reports, and local contributor
instructions. These patterns are a defensive backstop; tracked and staged
content still requires inspection before every commit.

The `public_repository_guard` test checks the tracked index for sensitive file
names, private-key and common token signatures, personal home paths, and
at-signs in text. The public clone example now uses HTTPS and the README links
this record.

Verified with the standalone repository guard, release and ASan/UBSan builds,
both two-test CTest suites, and a headless application smoke launch.

## 2026-07-27 -- Initial core and Qt Quick foundation

The first working slice separates a toolkit-agnostic C++20 filesystem core from
the Qt Quick application shell. The core reads directory entries, classifies
their kinds, records file sizes, filters hidden files, and sorts directories
before files with case-insensitive name ordering. A headless test executable
checks the listing behavior without Qt.

The shell exposes the core listing through a `QAbstractListModel` adapter and
renders it with a virtualized Qt Quick `ListView`. Release builds use Clang;
the sanitizer preset uses GCC with AddressSanitizer and UndefinedBehaviorSanitizer.

Verified with the release and sanitizer CMake presets: both builds completed
without warnings, both test runs passed, and the application remained healthy
during a headless smoke launch.

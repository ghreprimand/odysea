# OdySea — Devlog

Public running record of OdySea development, in reverse-chronological order.
Each entry records what landed, how it was verified, and any known gaps. See
`docs/ROADMAP.md` for milestone status and `docs/DESIGN.md` for durable product
and architecture decisions.

---

## 2026-07-27 -- Pointer hit testing and scan watcher lifetime

The list rubber-band surface now occupies only unused space below the final
visible row, so row clicks, modified clicks, and double-clicks reach their
delegates while blank-area drags still create range selections. A Qt Quick test
sends a left click through the rendered shell and verifies that the selection
model receives the row.

Asynchronous scans now use a single RAII-owned future watcher. The watcher
observes the newest scan without heap allocation or deferred manual deletion,
while generation checks continue to reject stale results. Selection input uses
the keyboard-modifier type directly across the QML boundary.

Verified with diagnostic-free QML linting, the Qt Quick pointer regression,
formatting and static-analysis gates, warning-clean release and sanitizer
builds, both CTest suites, the public-repository guard, and headless release and
sanitizer smoke launches.

## 2026-07-27 -- Input-parity navigation shell

The graphical shell now pairs pointer controls with keyboard shortcuts for
back, forward, up, refresh, location entry, filtering, hidden-file visibility,
sorting, tab creation and closure, pane activation, selection, entry
activation, and filesystem-operation requests. Selection supports single,
toggle, range, select-all, cursor-only movement, and rubber-band paths. The
pane workspace preserves independent tab and navigation state while the
transfer-oriented dual-pane layout remains a later milestone.

Verified with diagnostic-free QML linting, warning-clean release and sanitizer
builds, both CTest suites, and headless release and sanitizer smoke launches.

## 2026-07-27 -- Asynchronous shell model and navigation state

The Qt adapter now schedules directory reads away from the GUI thread and
discards stale scan results after newer navigation requests. It exposes loading
and error state, per-tab navigation history, two-pane workspace state,
presentation sorting and filtering, hidden-file control, and a multi-selection
model to the Qt Quick shell. Copy, move, rename, trash, and file-open requests
cross explicit adapter seams while their core and platform implementations
remain pending.

The adapter keeps filesystem behavior in the toolkit-agnostic core. Qt owns
only scheduling and presentation state.

Verified with warning-clean Clang and GCC sanitizer builds.

## 2026-07-27 -- Checkout-independent header analysis

Static analysis now anchors its header filter to the detected source root
instead of assuming the checkout directory is named `odysea`. App, core, and
test headers therefore receive identical analysis in renamed clones, CI
workspaces, and ordinary contributor checkouts.

Verified with `clang-tidy` 22 in both the primary checkout and a temporary clone
whose directory name does not contain the project name, plus the release and
ASan/UBSan CTest suites.

## 2026-07-27 -- Executable formatting, analysis, and publishing gates

Formatting and static analysis are now executable CTest gates rather than
documentation-only requirements. The formatting check pins `clang-format` 22
and scans every tracked C++ source and header. The analysis check pins
`clang-tidy` 22, uses the active compilation database, and applies the
repository's warning-as-error policy to each tracked translation unit. It runs
with the Clang release database; the GCC sanitizer preset retains its separate
ASan/UBSan runtime gate.

The publishing guard now verifies public no-reply commit attribution, rejects
`Co-Authored-By` trailers and high-signal internal workflow narration, scans for
private-network references, and checks its own content for private-key and token
signatures. All three repository-dependent gates report a CTest skip when a
source archive has no Git metadata.

Verified with the public repository, formatting, and static-analysis scripts,
plus release and ASan/UBSan builds and their complete CTest suites.

## 2026-07-27 -- Deterministic C++ formatting baseline

The complete tracked C++ corpus now matches the repository's `clang-format`
policy. This removes inherited formatting drift from the initial scaffold so
future formatting checks report only newly introduced changes.

Verified with `clang-format` 22 in dry-run error mode, the public repository
guard, and both release and ASan/UBSan test presets.

## 2026-07-27 -- Public development record and repository safeguards

The repository now carries a public development log and explicit publishing
boundaries. Contributor guidance requires staged-change inspection, synthetic
fixtures, impersonal engineering prose, and exclusion of secrets, personal
data, private infrastructure, machine-local configuration, diagnostic captures,
internal workflow artifacts, and personal commit metadata.

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

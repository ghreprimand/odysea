# OdySea — Devlog

Public running record of OdySea development, in reverse-chronological order.
Each entry records what landed, how it was verified, and any known gaps. See
`docs/ROADMAP.md` for milestone status and `docs/DESIGN.md` for durable product
and architecture decisions.

---

## 2026-07-28 -- Enforced file-length ceiling

Tracked source and text files now have a hard ceiling of 2,000 physical lines.
The `file_length_guard` checks both indexed content and the tracked working tree
so staged changes and later local edits cannot bypass the limit. Files
approaching the ceiling must be split along clear responsibility boundaries
without compressing readable code or documentation.

Verified against the complete tracked corpus and through the release and
ASan/UBSan CTest suites.

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
## 2026-07-27 -- Cancellable off-thread directory scanning

Navigation no longer has to wait for a directory to finish reading.
`DirectoryScanner` runs listings on a worker thread and delivers entries as
incremental batches, so a view can show its first screenful while the rest is
still arriving. Every request returns a token immediately, and the newest
request wins: starting a scan cancels the one in flight and supersedes anything
queued behind it, which is what keeps rapid navigation cheap. Cancellation is
checked per entry, so an abandoned directory stops costing work almost at once.

Every request receives exactly one completion summary reporting the token, the
number of entries delivered, any error, and whether it was cancelled, including
requests replaced before they ever started. Callbacks are invoked on the worker
thread and never while a lock is held; consumers with thread affinity marshal
batches themselves. Destroying a scanner cancels the scan in flight, joins the
worker, and suppresses further callbacks.

Batches arrive in filesystem discovery order, so the listing comparator is now
public alongside `sort_entries` and `make_entry`. A consumer merging batches
orders them exactly the way a complete listing is ordered, and the synchronous
`read_directory` path reuses the same helpers.

Verified with the release and sanitizer presets: warning-clean builds, six CTest
suites passing in both configurations, the scanning and watching suites repeated
to shake out timing flakiness, a headless application smoke launch, and
formatting checked on the changed sources. Known gap: no shell view consumes the
scanner yet, so the roadmap entry for off-thread scanning stays open.

## 2026-07-27 -- Incremental directory watching

`DirectoryWatcher` wraps Linux inotify so a view refreshes the entries that
actually changed instead of rescanning on a timer. Each `wait` drains the whole
kernel queue, so a burst of filesystem activity becomes one batch and one
refresh. Changes report their kind, the watched directory, the entry name, and
whether the entry is itself a directory.

Renames carry the kernel cookie, so the departing and arriving halves of a move
can be matched into a single rename rather than a delete followed by a create.
A queue overflow is surfaced explicitly as a change that instructs the caller to
rescan, because silently dropping events would leave a stale view. Deleting or
moving a watched directory reports the watch as removed and the watcher forgets
it.

The watcher is move-only and single-owner, with one deliberate exception:
`interrupt` may be called from any thread to release a blocked wait, which is
how a watching worker shuts down promptly. The raw descriptor is exposed for
callers that prefer to drive their own event loop.

Verified with the release and sanitizer presets: warning-clean builds, five
CTest suites passing in both configurations, and formatting checked on the
changed sources. Known gaps: watches are not recursive, and a directory that
becomes watchable later is not retried.

## 2026-07-27 -- Delete to the freedesktop.org trash

Deleting from OdySea moves an entry into the desktop trash rather than
destroying it. `move_to_trash` writes a `.trashinfo` record holding the
percent-encoded original path and a local deletion timestamp, then renames the
entry into the trash `files` directory. The record is created exclusively
before the entry moves, so two concurrent deletions of the same file name
cannot claim the same slot; a taken name becomes `report_1.txt`. A failed move
removes the record it claimed.

Trash location follows the specification. Entries on the same filesystem as the
home trash go to `XDG_DATA_HOME/Trash`, defaulting to `HOME/.local/share/Trash`.
Entries elsewhere go to a top-level trash on their own filesystem: a sticky
`.Trash/<uid>` when the administrator provided one, otherwise `.Trash-<uid>`.
Keeping the trash on the same filesystem keeps the delete a rename instead of a
silent copy of a large tree.

Supporting pieces: a move-only RAII descriptor type so POSIX handles close
exactly once, and a percent-encoder covering the specification's unreserved set.
Tests redirect the data-home variable into a temporary tree, so they never touch
a real desktop trash.

Verified with the release and sanitizer presets: warning-clean builds, all four
CTest suites passing in both configurations, and formatting checked on the
changed sources. Known gaps: restore-from-trash, trash emptying, and the
optional directory-size cache are not implemented, and no shell action is wired
to deletion yet, so the roadmap entry for filesystem operations stays open.

## 2026-07-27 -- Core copy, move, and rename primitives

The core gained toolkit-agnostic filesystem mutations: `copy_into`,
`move_into`, `rename_entry`, and the `resolve_destination` helper that previews
a final name before anything changes on disk. Nothing throws; every failure
returns a `std::error_code` so the presentation layer decides how to surface it.

Collisions are governed by an explicit policy: fail, overwrite, or auto-rename
to the next free `name (2)` variant. Overwrite replaces the destination rather
than merging into it, so a copied directory never inherits stale children.
Directory copies recurse and preserve symlinks as symlinks. Copying or moving a
directory into itself or into one of its own descendants is rejected, and moves
fall back to copy-then-remove when source and destination sit on different
filesystems.

A shared headless test harness now provides assertion helpers and a
self-cleaning temporary tree built entirely from synthetic paths. Each core test
file builds as its own CTest executable.

Verified with the release and sanitizer presets: warning-clean builds under
`-Werror`, all three CTest suites passing in both configurations, and formatting
checked on the changed sources. Known gap: delete-to-trash, directory watching,
and off-thread scanning are still open, so the roadmap entry for filesystem
operations remains unchecked.

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

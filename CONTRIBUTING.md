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
./tools/check_qml.sh format
./tools/check_qml.sh lint build/release/app   # built shell-module import root
./tools/check_qml_module.sh build/release/app/OdySea
./tools/check_clang_tidy.sh build/release

# Tracked-file length ceiling:
./tools/check_file_length.sh
```

### Gates in a source tree without repository metadata

The gates run in a build made from a release archive, which carries no `.git`.
Where a clone is available they read the tracked set and the index, so a gate
run before a commit judges what that commit would publish. Where it is not,
they read the source tree itself, excluding version-control metadata and any
CMake build tree beneath the root; a build tree is recognised by the
`CMakeCache.txt` inside it, not by its name.

Two things are unavailable without a repository and are reported as such rather
than passed over: the index, and the commit history the attribution checks
read. An archive has neither to check.

No gate declares a skip return code. Each one either resolves a corpus and
runs, or states a precondition and fails. The prerequisites above are therefore
requirements of a checked build and not merely of a developer checkout: `git`
is needed by the gate self-tests, which build throwaway repositories to
exercise the behaviour they pin.

## Project layout

- `core/` — toolkit-agnostic C++20 filesystem model. **No Qt or GUI types here.**
  It must remain unit-testable without a display server.
- `app/` — the Qt Quick shell (`main.cpp`, the `DirectoryListModel` adapter, and
  the `OdySea` QML module in `qml/`). This is the only place Qt is used. Every
  scene belongs to the module's file list; adding a `.qml` file without listing
  it there fails the module manifest gate.
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
- The formatting, static-analysis, privacy, file-length, and QML gates derive
  their inputs from tracked files. A new file is invisible to them until it is
  staged, so stage new files before running the gates; a gate run over unstaged
  additions proves nothing about them.
- Formatting covers every conventional C and C++ source and header extension,
  not only `.cpp` and `.hpp`, so a file cannot skip the gate by being named
  differently. `formatting_guard_self_test` holds the gate to that: it builds a
  throwaway repository per extension and requires unformatted code to be
  rejected and formatted code to be accepted in each one.
- `qmlformat` and `qmllint` 6.10 enforce the declarative shell baseline. The
  tracked `.qmlformat.ini` is canonical, and lint warnings fail the gate. Lint
  needs the built shell module on its import path, so pass the build tree's
  `app` directory as an import root; `ctest` does this automatically. Build
  before linting: the import root only carries type descriptions once the module
  has been built, and `qml_lint_guard` fails on a configure-only tree while
  `qml_module_guard` does not. That ordering check reads every manifest below an
  import root at any depth, because a module directory may carry nested
  manifests of its own. `qml_lint_order_self_test` holds it to that with
  throwaway import roots covering a built module, an unbuilt one, a manifest
  with no trailing newline, a module declaring no type descriptions, nested and
  deeply nested manifests, and an import root that does not exist.
  `.qmlformat.ini` deliberately leaves property normalization and import sorting
  off; `docs/STACK.md` records why, and changing either setting rewrites every
  tracked scene.
- The declarative shell is a linkable QML module. Application code and tests
  load scenes through `OdySea`, never by relative source-directory import, so a
  scene missing from the module fails both instead of only the application. A
  failed load is reported on the standard error stream naming the module and the
  type, so a broken install explains itself instead of exiting silently.
- `qml_module_guard` compares the tracked scene corpus under `app/qml` against
  the manifest the build produced. Scenes live in `app/qml`: the gate derives
  the tracked side from that directory, so a scene placed elsewhere fails the
  gate even when `QML_FILES` lists it correctly. Adding a scene without listing
  it in `QML_FILES`, or leaving an entry behind after a rename, fails the gate.
  Scene file names start with a capital letter, because the file stem is the
  type name the application instantiates. `qml_module_guard_self_test` holds
  the gate to that with throwaway repositories and manifests covering an
  omitted scene, a renamed entry, a renamed file, a stale entry, an absent
  manifest, an empty corpus, and a scene that cannot be a type.
- Static analysis separates fatal from advisory checks. `.clang-tidy` promotes
  the categories that indicate real defects — `bugprone-*`, owning-memory,
  no-malloc, and the clang analyzer — to errors; those fail the gate outright.
  Every remaining enabled check is advisory and is held against
  `tools/clang_tidy_baseline.txt`, which records one count per file and check.
  The gate fails when a file gains a diagnostic it did not have and equally
  when it sheds one without the baseline being updated, so the recorded set can
  only move downward. Fix a new diagnostic rather than recording it; after
  genuinely removing one, regenerate with
  `bash tools/check_clang_tidy.sh <build-directory> --update-baseline` and
  commit the smaller baseline. Advisory diagnostics are summarized rather than
  reprinted, because several hundred unchanged lines on every run hide the one
  that is new. All translation units are analyzed before the gate reports, so a
  single run lists every fatal diagnostic instead of stopping at the first.
- An advisory check is disabled only where it cannot be satisfied, never
  because it is loud, and the reason is recorded next to the exclusion.
  `app/tests/.clang-tidy` is the only such exclusion: a Qt test case is a
  private slot invoked through the meta-object system, which cannot invoke a
  static member function, so converting one as
  `readability-convert-member-functions-to-static` suggests removes the case
  from the run while leaving the suite green. The check stays enabled for
  application and core sources.
- Every QML test runner owns a leaf directory that no other runner scans. Qt
  Quick Test scans recursively and offers no exclusion, so a runner aimed at a
  parent of another runner's directory silently adopts its cases: they run
  twice and a failure is attributed to the wrong test entry. A runner aimed at
  a directory holding no `tst_*.qml` is quieter still — it prints nothing and
  exits successfully. Both faults are invisible in a passing run, so
  `qml_test_scopes` checks the declared scopes statically, and
  `qml_test_scopes_self_test` holds it to that with throwaway projects covering
  sibling scopes, a nested scope, a deeply nested scope, two runners sharing a
  directory, an empty scope, an absent scope, a sibling whose name is a prefix
  of another, and a build file declaring no runner at all.
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
- Use only the account-scoped GitHub no-reply address: the numeric account id,
  a plus sign, the account login, and the GitHub no-reply mail domain. Never
  construct a no-reply address from a project, product, or role name. Such an
  address is still syntactically valid, but GitHub resolves it to whichever
  unrelated account owns that login, publishing an unrelated account as a
  contributor.
- Run `tools/install_hooks.sh` once per clone. Hooks live under `.git/`, which
  is never cloned, so a fresh checkout starts with no enforcement until the
  tracked hooks in `tools/hooks/` are enabled. Never commit or push with
  verification disabled.
- Inspect staged changes before every commit. `.gitignore` is a backstop, not a
  security boundary; stop and investigate any ambiguous content.
- Keep `public_repository_guard` green. It rejects tracked secret-file names,
  private-key and common token signatures, personal home paths, private-network
  references, internal workflow narration, unsafe commit attribution,
  `Co-Authored-By` trailers, and at-signs in tracked text. Public clone links use
  HTTPS so email-like syntax is unnecessary.
- The at-sign ban is lifted for shell expansion syntax, which cannot be written
  without it: the array-at subscript inside a parameter expansion, the braced
  positional form, and the bare positional form, which covers both the quoted
  and unquoted spellings. Write those forms normally instead of substituting
  index loops or star subscripts, which is what the unqualified ban used to
  force. Every other at-sign in a shell file is still rejected, including in
  a comment or a string, and the ban stays blanket in
  every other file. The exception is granted to the syntax, not to the file: a
  line is scanned again with the permitted forms removed, so an address beside
  a legitimate expansion is still caught. Whole-file exclusions were rejected
  because each one stops the guard reading the rest of that file, and the list
  would grow one entry per script until the rule lived only in its own comment.
  `public_repository_guard_self_test` holds both directions, since an
  unexercised carve-out would let the guard keep reporting success after the
  exception had swallowed the rule.

## Engineering record

`DEVLOG.md`, documentation, comments, and commit messages form a public
engineering record. Describe the behavior, implementation, decisions,
verification, and known gaps in the project's voice. Do not narrate work
assignments or use internal workflow terminology in tracked text.

Update `DEVLOG.md` with each accepted development milestone, in the same change
as the code or documentation it records. Commit subjects and bodies stay
factual and scoped.

The record is split so that no single file approaches the 2,000-line ceiling.
`DEVLOG.md` holds the most recent entries, and a new entry goes at the top of
it and never into an archive. Older entries move verbatim into `docs/devlog/`,
which is linked from the live record, most recent first.

There are two archive shapes. A month that has closed moves whole into
`docs/devlog/YYYY-MM.md`. A month that reaches the line ceiling before it
closes is archived in numbered `docs/devlog/YYYY-MM-partN.md` files, each
holding a consecutive stretch of it, numbered from one without gaps and in
increasing date order, with the newest entries staying in the live record. A
month is archived one way or the other, never both, and parts are not merged
back when the month closes: a published part is as settled as a closed month.

Published entries are never edited, reordered, reworded, or removed; a
correction is a new entry. Moving entries is a transcription rather than a
rewrite, so prove byte identity mechanically instead of reading the result
over.

`docs/devlog/published-entries.txt` records every published entry heading in
reading order — the live record first, then each archive most recent first.
Append to it in the same change that publishes the entry. It exists because the
failure mode of a split record is silent loss: a stretch that survives neither
the move nor a later rebase leaves every remaining file structurally perfect,
and no arrangement check can see the hole. Within one tree it pins the record
against rewording, reordering, and duplication.

The manifest does not bound the record from below. It is a tracked file, so the
change that drops an entry drops its manifest line in the same commit, and
every arrangement rule then holds over a record with a hole in it. What bounds
the record is history: the guard walks the published branch, collects every
entry ever published and the text it carried when it was last published, and
requires each to be present and unchanged. The comparison is non-forgetting, so
an entry deleted several commits ago still fails today, and an entry history has
never seen must be in the live record rather than written straight into an
archive. Bodies are compared with boundary blank lines and the horizontal rule
between entries normalised away, because a move changes those and nothing that
was written.

The walk asks for full history. Default simplification follows a single parent
through a merge that is TREESAME to it for the record paths and discards the
other side, so a branch that touches nothing in the record, merged with the
record files resolved to its version, can retire an entry that no file and no
manifest mentions any more. That is an ordinary conflict resolution, not an
attack, and this branch already carries merge commits whose record-touching
parents simplification drops.

The baseline is the published branch alone and never every ref. Refs that are
not ancestors of it hold record states that were never published, and a
baseline drawn from those would import unpublished material into the standard
the public record is judged by. It is `origin/main` in preference to local
`main`, because the guard runs after the commit exists and a local branch
contains the change being judged — a rewrite committed there would be compared
against itself. `HEAD` is the last resort, so that a clone whose branch or
remote has been renamed is still measured against the history it is carrying
rather than reported as unchecked. The residue is stated rather than implied:
a rewrite committed on an integration branch already fast-forwarded past it, in
a clone with no `origin/main`, is judged against itself. Pushing closes it, and
nothing in the repository narrows it further.

`devlog_archive_guard` enforces all of it: every archive must be linked and
every link must resolve to a tracked archive file, archive filenames must match
the month of every entry they contain, the live record must hold nothing older
than the newest archived entry and nothing newer may sit in an archive, part
numbering and date ranges must be consecutive and disjoint, no entry may appear
in two files, the index must stay most-recent-first, the manifest and the files
must agree exactly, and every entry the published branch has ever carried must
still be present in them, unchanged. In a repository, loss and rewriting are
both caught mechanically. A source tree extracted from a release archive has no
history to compare against, so the run says by name that the published record
is unchecked there and checks the internal agreement that remains.
`devlog_archive_guard_self_test` holds the guard to each of those failure modes
plus an accepted layout, because a guard only ever observed passing cannot be
told apart from one that never reaches its checks.

Commits carry no attribution trailers. `Co-Authored-By`, `Assisted-by`,
`Generated-by`, `Created-by`, `Authored-by`, `Signed-off-by`, and
`On-behalf-of` are rejected in commit messages, amended messages, and squash
messages, because each one publishes an additional contributor. Automated
tools that add such a trailer by default must have that behavior disabled
before they are used against this repository.

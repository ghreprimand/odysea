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
./tools/check_smoke.sh ./build/release/app/odysea
./build/release/app/odysea ~        # run on a directory

# Full battery with coverage reconciliation (preferred before submitting):
./tools/run_verification_battery.sh build/release
# Runs the battery, then accounts for every registered entry against the live
# roster, and the entries able to skip against tools/skip_declarations.txt. A
# silently skipped, missing, or unexpected entry fails it, as does an entry that
# can skip without being declared; only a declared refusal is an allowed, listed
# opt-out. Plain `ctest` still works for quick iteration but does not reconcile
# coverage.

# Development build with sanitizers:
cmake --preset asan
cmake --build build/asan
ctest --preset asan                # core tests run under ASan/UBSan
./tools/run_verification_battery.sh build/asan    # with coverage reconciliation

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
runs, or states a precondition and fails. The few entries that do carry one are
listed in `tools/skip_declarations.txt` with the tolerance that applies to them
and the precondition they need; the coverage reconciler reads that file beside
the live roster and fails on an entry that can skip without a line in it, so
the ability to skip is written down in the change that grants it rather than
found on the first run that uses it. `build_configuration_guard` is the
reason that rule is worth stating for build directories as well: the two
presets pin different compilers and different options, so `cmake -S . -B
build/asan` is not `cmake --preset asan` once the directory has been wiped and
there is no cache left to inherit from. The bare form takes the system
defaults, and the quiet form of that failure is a sanitizer directory with the
sanitizer switched off, which builds, tests, and reports as a sanitizer pass.
The gate holds each build directory to every cache variable its preset
resolves to, and holds the preset file itself to pinning a compiler and a build
type for every preset and a sanitizer for at least one, so the comparison
cannot be satisfied by deleting what it compares against. A build directory no
preset names is a failure there, not a skip: configure elsewhere freely, but
results quoted from such a directory are not results about this project's
build. The prerequisites above are therefore
requirements of a checked build and not merely of a developer checkout: `git`
is needed by the gate self-tests, which build throwaway repositories to
exercise the behaviour they pin.

### Tests that render must not touch a session in use

Verification that renders runs offscreen, or against a compositor started for
that run and torn down after it. Nothing in this project may create, resize, or
remove a display output, move input focus, install window rules, or open a
window on a session someone is using. This is a hard rule, not a preference: a
test window that takes focus, or that lands on an output nobody is looking at,
leaves the surfaces a person is actually using unable to receive keyboard or
pointer input, and a run that ends abnormally leaves that state behind. The
compositor, the kernel, and the drivers all stay healthy throughout, which is
what makes the result difficult to diagnose from the outside.

The rule is enforced mechanically rather than trusted. Both the compositor
launcher and the shared runner behind the GPU-path suites refuse to run unless
the platform is non-rendering, or a declaration names a socket that matches
`WAYLAND_DISPLAY`, exists, and is proven to belong to the run making the claim.
A declaration is treated as a claim, not as evidence, in three distinct ways. A
harness whose compositor never bound presents the same environment as one that
succeeded, so the socket is checked rather than the name alone. A socket being
present proves only that some compositor is listening — which is exactly what
is always true of the session in use, whose socket is a socket and whose name
can be exported by hand. And a file written beside that socket proves only that
someone wrote a file, since the same person can export the value it is compared
against.

So `tools/isolated_compositor_gate.sh` creates a private run directory, writes
a per-run token into it, holds an exclusive lock on a file there for the whole
run, and exports the directory and the token alongside the socket name. A gate
accepts a declaration only when all of the following hold:

- the declared socket is a socket and is **not** a symbolic link;
- the directory holding it matches `ODYSEA_ISOLATED_COMPOSITOR_RUNDIR` **by
  device and inode**, not by spelling;
- that directory is not the login session's runtime directory, again by device
  and inode;
- it holds a file matching `ODYSEA_ISOLATED_COMPOSITOR_NONCE`;
- the harness's liveness lock there is **still held** by a running process.

Paths are never compared as text. Every spelling of one directory shares its
device and inode and no spelling of another does, so a trailing slash, a `.`
component, or a `..` round trip cannot turn a refusal into an acceptance. The
socket is inspected without following links, because a link in a writable
directory can point anywhere. The lock is the only condition that cannot be
produced by writing files: the kernel releases it when the holding process ends
by any means, so a gate that finds it held knows a harness is alive. It is
tested with a non-blocking `flock(2)` attempt, never `fcntl(F_GETLK)`, which on
Linux does not observe `flock` locks at all and would report every lock free.

Be exact about the limit. A local user who runs their own socket and holds
their own lock can still present something shaped like a harness; unforgeable
proof is not available to a check running as the same user as the thing it
checks. What is claimed, and all that is claimed: no declaration can resolve
onto the login session's compositor, and no accidental or hand-typed
declaration succeeds. `app_isolated_compositor_declaration` reproduces each of
those ways through as an executable case, including the accepting one, since a
check that refuses everything would satisfy every refusal test.

Starting a compositor is an obligation to end it, and the harness is held to
that obligation rather than trusted with it. The gate is stopped first and
waited for, because it is the process using the compositor and reading the
runtime directory that the following steps take away; then the compositor; and
each process group is escalated from SIGTERM to SIGKILL rather than signalled
once and forgotten. A run's record is deleted only once every process recorded
in it is confirmed gone. Anything that cannot be confirmed is kept and named,
because the record is the only thing that keeps a survivor reachable by a later
run, and a deleted record with a live process behind it is a compositor holding
the GPU and a seat that nothing can find again. A run that meets an earlier one
it cannot dispose of does not start beside it, and a gate that passed while its
own teardown failed is reported as a harness failure, not as a pass.

Identity is part of that, because every signal goes to a whole process group. A
pid alone would name whichever group now holds that number, so each pid is
recorded with its start time and both must match before anything is signalled.
A start time that cannot be read is a hard failure at the moment of recording:
an empty value can never match, which would disable teardown, the escalation,
and every later sweep at once while the record was deleted anyway.

What this does not yet do is measure anything on a real compositor. The two
compositor test entries are registered directly rather than through the
harness, and they decline, so the real-compositor path is unmeasured. The
harness refuses to start a compositor whose headless selection cannot be shown
to apply to the compositor actually installed, and the refusal has two halves.
The selector's **name** must appear in that program or a library it links:
containing the name is weak evidence that it is read, but its absence is
conclusive evidence that it is not, so a command setting a variable nothing
reads is inert and is refused. The selector's **value** must then be one known
to render nowhere, because the name decides only whether the variable is read
and the value decides where the compositor renders. A selector this harness
does not recognise has no confirmable headless spelling and is refused rather
than trusted.

On a machine whose compositor does not read the selector the default command
names, the harness refuses and the path stays unmeasured. That is the intended
outcome. A compositor started with no backend constraint falls back to whichever
backend it chooses by itself, which on a workstation is the one that takes the
seat, the virtual terminal, and DRM master on the display the session is using —
a worse outcome than the one this whole rule exists to prevent, produced by the
tool written to prevent it.

The refusal is a skip and stays a skip. `ODYSEA_REQUIRE_COMPOSITOR`, which
turns an inability to run into a failure so a skip cannot read as a pass, does
not override it — a gate that punished its own refusal would only argue for
removing the refusal. Running a suite binary directly is refused for the same
reason: where a suite renders is decided by test properties and by the
launcher, neither of which is part of the binary, and with no platform pinned
Qt falls back to whatever session is in the environment.

Consequently the real-compositor path is exercised only where an isolated
compositor can be started, and is reported as unmeasured elsewhere rather than
approximated against an ambient session.

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
  `tools/clang_tidy_baseline.txt`, which records one count per file and check
  together with a digest of that entry's diagnostic text.
  The gate fails when a file gains a diagnostic it did not have and equally
  when it sheds one without the baseline being updated, so the recorded set can
  only move downward. It also fails when the count holds but the text moves,
  which is what a fixed diagnostic and a newly introduced one look like
  together: parenthesizing one expression while getting the precedence wrong in
  another leaves the same file with the same number of occurrences of the same
  check, and a count-only comparison reported that as a clean tree. The digest
  covers message text only and carries no line or column, so moving a
  diagnostic down its file does not restate the baseline. Two occurrences of
  one check in one file whose messages are byte-identical remain
  interchangeable, because nothing outside their locations distinguishes them.
  Fix a new diagnostic rather than recording it; after
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
- An entry that filters its suite to named functions must list every function
  of every TestCase it names. One GPU-path entry passes
  `TestCase::function` arguments through to the suite it re-execs, and both
  directions of the drift are silent: a function added to that TestCase and
  left off the list runs nowhere on that path, and a listed function that no
  longer exists matches nothing, because Qt Quick Test does not object to a
  filter that matches none. Either way the entry stays green in the time it
  always took. Where the same function also runs under an unfiltered entry the
  first fault is survivable; it is not survivable for a test that is only
  meaningful on a real GL path, since such a test passes vacuously offscreen or
  guards itself away there and would then run nowhere at all.
  `rhi_function_filter` resolves each filtered entry through its launcher's
  `ODYSEA_PRESENTATION_BINARY` and that suite's `QUICK_TEST_SOURCE_DIR` to the
  QML it actually runs, and requires exact agreement in both directions.
  Completeness is required per TestCase rather than per directory: a scope may
  hold several TestCases and naming only some of them is a decision the build
  file is allowed to make, so a TestCase no filtered entry mentions is not
  bounded — that limit and the broken-chain failures are stated in the gate.
  `rhi_function_filter_self_test` holds it to twelve states, including a
  function added and unlisted, a listed function that does not exist, a listed
  TestCase that does not exist, a TestCase deliberately left out, one TestCase
  continued across two files, each hop of the resolution chain broken, a scope
  defining no test function, and a build file with no filtered entry at all.
- A skipped battery entry must never read as a pass. `battery_coverage_self_test`
  and `run_verification_battery.sh` reconcile every registered entry against the
  live roster and classify each one: it ran, it declared a refusal, it did not
  meet a declared precondition, it did not run and declares no skip capability
  at all, it skipped without declaring a refusal, it skipped silently, it is
  missing from the results, or it is in the results without being registered.
  Only a declared refusal is tolerated — a line of the exact form
  `<gate-name>: DECL -- declined: <reason>`, which the compositor gates print
  when no isolated compositor was declared for the run. Every other shortfall
  fails, including a capability skip that explained itself: an entry that could
  not run is a hole in the battery's coverage, and tolerating any skip that
  printed something would let one `echo` reopen the failure class the
  reconciler exists to close. A run where nothing executed fails too, so
  declaring a refusal buys tolerance for one entry and never for a battery.
  The practical consequence is deliberate: the battery is green only where the
  offscreen GPU gates can obtain a context, so a headless machine needs a
  virtual display rather than a looser gate.
- The ability to skip is itself declared. Every judgement above is made after a
  skip has happened, which left the mechanism unwatched: an entry could be given
  `SKIP_RETURN_CODE` and the reconciler would meet it for the first time on the
  run where it dropped out. So the roster is taken from
  `ctest --show-only=json-v1`, which reports per-entry properties, and is checked
  against `tools/skip_declarations.txt` before a single result is read. An entry
  that can skip with no line there fails; a line naming an entry that is not
  registered fails; a line naming a registered entry that carries no skip return
  code fails, so a declaration cannot outlive its mechanism; and a skip return
  code other than the project's own fails, so a second convention cannot appear
  beside the declared one. A line records one of two tolerances. `refusal` is
  the tolerated shortfall above. `capability` records a precondition the entry
  cannot satisfy itself — an offscreen OpenGL context, a JSON-capable
  interpreter — and a skip under it FAILS, with the recorded precondition
  printed beside the failure so the reader is told what to provide. That is what
  the skip return code buys there: the entry reports as not-run rather than as a
  broken test, and the reconciler is what turns the totals red, which is where
  every other coverage hole in this battery is already decided.
  `ODYSEA_REQUIRE_OFFSCREEN_GL` (offscreen GL) and `ODYSEA_REQUIRE_COMPOSITOR`
  (real compositor) turn the corresponding skips into failures inside the gate
  itself, where the capability is expected. The offscreen GPU launchers name
  "no display server reachable" versus "a display is reachable but the OpenGL
  context is unusable", because the two are different facts about a machine.
- `application_smoke` is the honest smoke: it forces `QT_FORCE_STDERR_LOGGING=1`
  and the offscreen software backend (`QT_QUICK_BACKEND=software`, the real
  software key — `QSG_RHI_BACKEND=software` is not a valid key and falls back to
  the default), removes ambient display variables, redirects XDG storage and
  the opened directory into a temporary workspace, and records the watchdog's
  harness-owned termination of a process that survived the observation window.
  An early exit — including status 124 — or signal death fails by name, as does
  an otherwise live process with a platform, RHI, scene-graph, or sanitizer
  fault on stderr. Zero bytes is not evidence of a healthy launch on its own —
  it is also what a journal-routed core dump leaves. The eight-scenario
  `application_smoke_self_test` discriminates those outcomes and checks the
  pinned environment.
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
  `Co-Authored-By` trailers, at-signs in tracked text, hosted-source references
  to an owner other than this repository's, derivative or rivalry framing, and
  a section heading whose subject is crediting or surveying other work. Public
  clone links use HTTPS so email-like syntax is unnecessary.
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

### Write about this project, not about others

Tracked text — commits, the record, documentation, code comments, test names,
and fixtures — states what OdySea does, why, how it was verified, and where it
falls short. Write every feature from the requirement it satisfies.

- Do not identify another project of the same kind, a derivative of one, or the
  accounts behind either. Reading other open-source work is a normal way to
  learn a problem; the decision that comes out of it is this project's own and
  is written that way.
- Do not present a decision as taken from, measured against, or in contest with
  another project, and do not add a section that credits or surveys other work.
- Cite upstream dependencies freely. A toolkit, a build system, a compiler, a
  bundled typeface and its license are named because a reader needs to know
  what the build requires, and their license terms are honoured in full.
- A hosted-source reference may name only this repository's own owner. The
  vendored dependency tree is the one exception, because the files there are an
  upstream's own provenance and license text reproduced verbatim as its terms
  require; the identical text one directory outside that tree is refused.

- Do not survey a field and place this project inside it. A positioning
  paragraph needs no name at all — a landscape, a gap found in it, this project
  set in the gap — and it makes the same claim. Describe what OdySea does and
  what is out of scope, in its own terms.

`public_repository_guard` enforces the category rather than a list of
instances, and deliberately so: a tracked list of names would publish those
names in the file written to suppress them, and would also disclose that the
suppression exists. Hashing such a list fixes neither, because a short known
name falls to a wordlist. The consequence is a stated gap — a bare name carried
without a hosted-source reference, without derivative framing, without a
positioning structure, and without a crediting heading is not detected, so the
rule is yours to keep, not only the guard's.

Weak comparatives are ordinary engineering prose and stay usable. The guard
matches a comparative only where it immediately governs a set this project is
being placed within or against, and it does not match interoperability prose:
saying that a divergent escaping would produce a cache no other application can
read is a specification statement, and it is meant to keep working. What is
refused is the enumerated peer group — a qualifier such as "most" or "existing"
in front of the project category — because there is no way to write that except
as a statement about a set of peers.

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

Date an entry the day it lands, not the day the branch was written. The record
reads newest first, and the two orders disagree whenever a branch waits: every
branch appends at the top, every rebase onto an advanced `main` collides there,
and the resolution puts the replayed entry back on top — so a branch authored
days before it lands is published above entries dated after it. Re-dating an
unpublished entry during integration is ordinary; a published entry never
moves, so this is fixed before the commit lands and never after.

Reading order is enforced from a fixed baseline entry upward: from the newest
entry down to and including the baseline named in
`tools/check_devlog_archive.sh`, which is the newest entry that existed when
the rule landed. Below it the record holds entries that are genuinely out of
order, and a rule reaching them would demand that published text be rewritten,
so it stops there. The limits are stated in the guard rather than implied: it
cannot see disorder below the baseline and it constrains nothing between
entries sharing one date.

Whether a date is truthful is a separate rule, because the ordering check
cannot answer it: a record where every entry is misdated by the same day is
perfectly ordered. An entry's heading date must equal the date of the commit
that added that heading, compared in the commit's own recorded timezone so the
answer does not change with who runs the check. Twelve entries in this record
were published carrying a date other than their commit's, and the shape recurs:
an entry written near midnight is dated in UTC while the commit is recorded in
local time, so the heading runs a day ahead of the commit that published it.
Date an entry the day it lands.

That rule reads local history rather than the published branch, which is the
opposite of every other history check here and is deliberate. A misdated entry
can only be corrected while it is unpushed, so a check waiting for `origin/main`
would first report a mistake that is already too late to fix and would then stay
red forever over text nobody may edit. It is bounded by its own baseline for the
same reason the ordering rule is. It cannot check an entry that is in no commit
yet — it names those rather than counting them as agreeing — and it cannot
detect a clock that was wrong at commit time, since both sides of the comparison
are things that commit asserts.

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

Naming a baseline is a precondition, not something the guard may decline. Those
three candidates cover every ordinary clone, but they are names and history
does not depend on names: renaming the branch and the remote and then checking
out an unborn branch leaves every commit reachable while none of the three
resolves. A repository that holds commits and cannot name a baseline fails, so
that somebody names one. Reporting the bound unchecked stays available only
where there is genuinely nothing to read — a source tree with no repository, or
a repository with no commit in it — because an honest skip and a completed
check otherwise read the same in a summary.

The size of the bound has a floor. Everything the comparison demands is an
entry the walk found, so the size of that walk is the strength of the check,
and a walk that reads less than it read before reports success in exactly the
same shape as one that read everything. It shrinks for ordinary reasons: a
baseline ref left behind by a fetch that never ran, a narrowed or truncated
walk. The guard therefore refuses a reading below a count this record has
already published, since published entries are never removed and the number can
only grow. That floor is conditioned on the oldest published entry, so it is a
statement about this record rather than about every record the script is
pointed at, and the anchor cannot go quietly missing: it lives in an archive
file, so a walk that stopped finding it would leave it archived and never
published, which is refused by name. The count may be raised to a number the
baseline branch has already published. Lowering it is how a shrinking bound is
made to look healthy, and it is the one edit the rule forbids.

`devlog_archive_guard` enforces all of it: every archive must be linked and
every link must resolve to a tracked archive file, archive filenames must match
the month of every entry they contain, the live record must hold nothing older
than the newest archived entry and nothing newer may sit in an archive, part
numbering and date ranges must be consecutive and disjoint, no entry may appear
in two files, the index must stay most-recent-first, the manifest and the files
must agree exactly, entries published from the baseline upward must read newest
first, and every entry the published branch has ever carried must still be
present in them, unchanged. In a repository, loss and rewriting are
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

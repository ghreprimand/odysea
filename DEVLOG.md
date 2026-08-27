# OdySea — Devlog

Public running record of OdySea development, in reverse-chronological order.
Each entry records what landed, how it was verified, and any known gaps. See
`docs/ROADMAP.md` for milestone status and `docs/DESIGN.md` for durable product
and architecture decisions.

This file holds the most recent entries. Older ones move verbatim into
`docs/devlog/`, either as `YYYY-MM.md` once a month has closed or, when the
record reaches the tracked-file line ceiling before that, as numbered
`YYYY-MM-partN.md` files holding consecutive stretches of an open month. A
month is archived one way or the other, never both. A new entry belongs at the
top of this file and never in an archive. Published entries are never edited,
reordered, reworded, or removed, whether they live here or in the archive;
`docs/devlog/published-entries.txt` records every entry heading in reading
order, and the archive gate compares it against what the files actually hold.

**Archived record**, most recent first:

- [2026-08 part 3](docs/devlog/2026-08-part3.md)
- [2026-08 part 2](docs/devlog/2026-08-part2.md)
- [2026-08 part 1](docs/devlog/2026-08-part1.md)
- [2026-07](docs/devlog/2026-07.md)

---

## 2026-08-27 -- Which battery entries may skip is declared, not discovered

The coverage reconciler judged every skip after it happened, which left the
mechanism itself unwatched. An entry could be given `SKIP_RETURN_CODE` in the
build configuration and the reconciler would meet that fact for the first time
on the run where the entry dropped out -- and if that skip printed the refusal
line, it was tolerated from then on without anyone having decided it should be.
An honest skip and a completed check read identically in a summary, so the
defect was never the skip mechanism; it was a skip the reconciler did not know
existed.

The roster is now taken from `ctest --show-only=json-v1`, which reports
per-entry properties, so each line carries its skip return code beside its name.
`tools/skip_declarations.txt` records every entry allowed to have one, and the
two are reconciled before a single result is read. An entry that can skip
without a declaration fails. A declaration naming no registered entry fails. A
declaration naming a registered entry that carries no skip return code fails, so
a declaration cannot outlive its mechanism. A skip return code other than the
project's own fails, so no second convention can appear beside the declared one.

A declaration records one of two tolerances. `refusal` is the shortfall the
reconciler already tolerated: a policy this project enforces, printed as
`<gate-name>: DECL -- declined: <reason>`, which the two compositor entries use
to decline a session they were not given to own. `capability` records a
precondition the entry cannot satisfy itself -- an offscreen OpenGL context, a
JSON-capable interpreter -- and a skip under it FAILS, with the recorded
precondition printed beside the failure so the reader is told what to provide.
The skip return code still earns its place there: the entry reports as not-run
rather than as a broken test, and the reconciler is what turns the totals red,
which is where every other coverage hole in this battery is already decided.

A fifth class comes free. An entry that did not run and declares no skip
capability at all now fails by that name, which covers the skips CTest itself
invents -- a missing executable, an unsatisfied dependency -- and which no
output-shaped rule could see.

The practical effect on the two offscreen GPU entries is stated rather than
softened. Without a display server they are a declared capability failure naming
the context they need, instead of an unexplained shortfall that a reader had to
be told to expect. The answer remains a virtual display, not a wider tolerance.

Verified by 26 self-test scenarios, up from 13, with the suite failing by name
when it reports fewer results than it contains. The load-bearing scenario is the
one where every result is clean -- nothing skipped, nothing missing, nothing
unexpected -- and the reconciliation fails anyway because an entry was given the
ability to skip and nobody wrote it down. Eleven mutations were planted against
the reconciler, each diffed to confirm the edit landed before it ran, and each
was caught by its own scenarios: the four static checks removed one at a time,
an undeclared entry treated as a declared refusal, an unmet precondition
tolerated, a missing declarations file read as nothing declared, a malformed
line skipped over, an unknown tolerance accepted, a duplicate declaration
accepted, and the roster's skip-code column ignored. A twelfth mutation dropping
a self-test scenario is caught by the reported-count floor.

What this still cannot do is judge whether a recorded precondition is the true
one. The registry's text is printed with a failure, not tested against it.

---

## 2026-08-27 -- Conflict markers honour Git's configured size

Git's default conflict marker is seven characters, but its marker size is
configurable. The earlier record's claim that Git emits exactly seven
characters was wrong. Both record-protection guards now recognize every marker
run of seven or more characters: the corpus guard refuses it before it can be
published, and the archive guard removes it from a previously published body
before comparing that body against history. The two rules move together so a
non-default marker cannot enter the record and then make its own repair look
like a rewrite.

The archive parser accepts a space or tab before a marker label, or the end of
the line, and its whitespace set is a strict subset of the corpus guard's. It
drops an entire marker line, including a label, so that relation is
load-bearing: the broader corpus ban is what prevents label text from becoming
invisible to the history comparison. The corpus check retains its line-start
anchor and no file-level exclusions for Git-text files. As with any text scan,
Git-binary files and a first-line UTF-8 BOM sit outside that anchor's reach;
neither form is in the tracked corpus.

Verification adds a complete nine-character conflict region to the corpus
guard self-test and a nine-character published-record repair to the archive
guard self-test. Mutating either guard back to an exact seven-character run
makes its respective new case fail while the other guard remains unchanged.

---

## 2026-08-26 -- Ambient display variables cannot select a compositor gate

The real-compositor launcher now has a named, non-rendering contract around
the platform boundary its session interlock enforces. `DISPLAY`,
`WAYLAND_DISPLAY`, and `QT_QPA_PLATFORM` describe ambient sessions; none of
them authorises a platform choice. Only a proved isolated-compositor
declaration may reach the OpenGL probe or the presentation suite.

The contract launches the real binary against three deliberately unreachable
environments: X11 alone, Wayland alone, and both together. Each run must exit
with the declaration-refusal status and message before Qt creates an
application. A capability skip, failure, or run message is rejected even when
its process status is also 77, so the old environment-selected path cannot be
mistaken for the required refusal. The endpoints are synthetic and absent;
the check cannot contact an interactive session.

The durable platform matrix now describes the shipped boundary rather than
the launcher's first form: isolated Wayland only, no ambient X11 fallback, and
`ODYSEA_REQUIRE_COMPOSITOR` able to turn a capability failure red without
overriding a policy refusal.

Verified in both directions. The contract passes all three named cases with
the interlock intact. A planted platform-selection branch before the
declaration check makes `ambient_x11`, `ambient_wayland`, and `ambient_mixed`
all fail by name. The columns ownership gate was also rechecked on the current
tree: removing `QQmlEngine::setObjectOwnership` makes
`shell_load_diagnostic` fail at
`anInvokableColumnListingRemainsCppOwned`, observing JavaScript ownership
where C++ ownership is required; restoring it returns the gate to green. No
real-compositor entry was invoked.

---

## 2026-08-26 -- The record's history bound may not shrink or be skipped

Two ways the archive gate's history comparison stopped bounding the record, one
by getting smaller and one by not running at all. Both were reproduced on the
unmodified tree before anything was written.

The comparison demands every entry the walk found, so the size of the walk is
the whole strength of it, and nothing measured that size. Pointing a clone's
`origin/main` at a commit five entries back — the state a fetch that never ran
leaves behind — dropped the bound from 112 published entries to 107 at exit
zero, and an entry published in that gap could then be deleted from both the
record and the manifest without complaint. The only trace was a number on the
success line that nothing compared against anything. The bound now carries a
floor: a reading below a count this record has already published is refused,
because published entries are never removed and that number can only grow. The
floor is conditioned on the oldest published entry so that it is a claim about
this record and not about every record the script is pointed at, and the anchor
cannot go quietly missing — it sits in an archive file, so a walk that stopped
finding it leaves it archived and never published, which is already refused by
name.

Naming a baseline was also optional. The candidate refs cover every ordinary
clone, but they are names, and history does not depend on names: renaming the
branch and the remote and then checking out an unborn branch leaves every
commit reachable under other refs while none of the candidates resolves. In
that state a full clone carrying 168 commits reported the bound unchecked on
standard output, exited zero, and accepted a record with a published entry
deleted from it. A repository that holds commits and cannot name a baseline now
fails. Reporting the bound unchecked remains available only where there is
genuinely nothing to read — a source tree with no repository, or a repository
with no commit yet — because an honest skip and a completed check otherwise
read identically in a summary.

Verification: the self-test grew from 44 scenarios to 49 and gained a floor on
its own reported count, so a suite that stops early fails instead of passing
short. The new scenarios put a record on each side of the count floor, hold the
guard to saying which side it found, pin the keeper that stops the floor going
inert, and drive a repository into the unnameable-baseline state with an intact
record so that only the refusal is under test. Eleven planted mutations were
each asserted to have changed the file before being run; all eleven were caught,
none survived, and none went unmeasured. Both reproductions above now fail by
name, and the shipped record stays green at 112 of 112 compared.

---

## 2026-08-22 -- Repair a record published with its conflict unresolved

The record went out with nine unresolved merge-conflict markers in it, left by
a three-way integration of three entries that each added text at the top of the
file. No entry was lost and no entry's prose was touched -- the markers fell
between entries -- but the top of the published record read as a conflicted
file.

Every gate that exists to protect the record passed it. The archive gate
compares each entry's heading and body against the form the published branch
last carried, and the markers changed neither, so it agreed. Worse, once the
damage was published it became the form every later comparison was made
against: the gate had adopted the break as its baseline and would have refused
the repair. The corpus guard scanned the same file for secrets, private paths
and address syntax, and had no pattern for a marker at all.

Two changes, in opposite directions. The corpus guard now refuses any tracked
line that begins with a conflict marker, so a commit carrying one is rejected
before it can be made rather than discovered after it is published. The pattern
covers the base marker the diff3 and zdiff3 styles add as well as the two sides
every style writes, requires exactly the seven characters Git emits followed by
a space or the end of the line, and is anchored at the start of a line. The
anchor is what lets the check carry no exclusion list: a marker composed inside
a `printf` argument does not begin its line, so the guard, its own self-test,
and any fixture that has to build one are all still scannable by it. A rule of
dashes or equals signs in prose is unaffected.

The archive gate now drops marker lines from an entry's body wherever they
appear, before comparing. This is narrow on purpose and is not the same
exemption as the one already made for trailing separators: a marker is not text
an entry can legitimately be published with, so it must not become part of the
form the entry is judged against. Only lines that are entirely a marker are
removed, so the surrounding prose is still compared byte for byte and a genuine
rewrite is still refused. The pair means a marker cannot enter the record and
removing one is always permitted.

Repairing the record itself removed the nine marker lines and restored the
blank line each had displaced. All thirty entries were checked against the form
the branch published, and against the individual commits the three conflicting
entries came from, before the change was made: every one is byte-identical.
Three further headings elsewhere in the record are not preceded by a blank
line. They predate this and were left alone, because widening a repair to
tidy published text is how published text stops being reliable.

Verified: the corpus guard refuses the exact nine markers the record carried
and accepts the repaired file; twelve new self-test scenarios cover all four
marker spellings, a full conflicted region, a marker in a source file, and five
near-miss forms that must still be accepted; five mutations of the pattern were
planted and all five were caught, each by its own named case. The archive gate
accepts the marker removal and still refuses a repair that also rewords an
entry under an unchanged heading, and removing either the marker rule or one
spelling from it turns the accepting case red.

## 2026-08-22 -- A hard link defeated the socket interlock

The interlock that keeps compositor-dependent gates off a session in use
refused a symbolic link to the login session's socket and accepted a hard link
to it.

`lstat` was doing the work of ruling out a link, and it rules out only the
symbolic kind. A hard link is the socket itself under a second name, so `lstat`
reports a socket, not a link. The run directory defaults to the same filesystem
the session's socket lives on, which is what makes the link constructible: the
session socket could be placed inside a directory that genuinely was the run
directory, beside a token that genuinely matched and a lock a live process
genuinely held. Every condition passed on its own terms, and the accepted
socket was, by inode, the compositor on screen. The one property the interlock
claims to have -- that no declaration resolves onto the login session -- was
false.

A socket created by `bind` has exactly one link, so requiring a link count of
one separates the two, and the field is already present in the `lstat` the
check performs. It is preferred over comparing the declared socket's inode
against the session socket's, which would answer only for the one socket it
names while this holds for any socket reached by a second name. The check errs
toward refusal: linking a genuine harness socket would also raise its count and
refuse the run, which costs a gate that declines rather than a window on
somebody's desktop.

Verified: measured in both directions against the real session socket. A hard
link of it, in a directory shaped exactly as a run directory with a matching
token and a held lock, is refused; a freshly bound socket in that same
directory is accepted. A test case builds the same hard link from a socket
bound elsewhere and reports rather than passes where the two temporary
directories do not share a filesystem, and deleting the link-count check turns
that case red and no other. The header now records the link count among what is
proved, and says plainly that the claim it restores has been falsified once.

## 2026-08-22 -- A build directory now has to be the one its preset describes

Two ways of configuring this project look interchangeable and are not.
`cmake --preset release` applies the pins the preset carries; `cmake -S . -B
build/release` applies whatever the machine defaults to. With a cache already
present the difference is invisible, because CMake reuses what the cache holds.
Once the directory has been wiped there is nothing to reuse, and the bare form
silently takes the system compiler with every option left at its default. The
standing practice of wiping both build directories whenever new entries
register is exactly what makes this reachable, and it is reachable on the runs
whose numbers get quoted.

The loud direction is not the problem. The release tree pins Clang, and built
with the system default it fails outright on a warning nobody can misread. The
quiet direction is: the sanitizer preset pins a different compiler and turns
the sanitizer on, so a wiped `build/asan` configured bare has the sanitizer
off. That tree builds, tests, and reports green. Nothing in the run is false
except what the run is called.

A gate now holds each build directory to the configuration its preset
describes. The directory is matched to a preset by the path that preset names,
and every cache variable that preset resolves to must be present in the cache
with that value -- the compiler, the build type, the prefix path, and the
sanitizer switch alike. Inheritance is resolved rather than read at one level,
because every pin here lives on a hidden base preset and a single-level read
would find nothing pinned anywhere and pass everything. The compiler is
compared as a located, link-resolved program rather than as text, since a
preset names a program and a cache records where it was found.

That comparison is only worth as much as the thing it compares against, so the
preset file is held to a floor from every build directory: each shipped preset
must pin a compiler and a build type, and at least one must pin the sanitizer
on. Deleting a pin fails by name instead of quietly making the check vacuous.

A build directory no preset names fails rather than skipping. This project's
gates do not declare skip return codes -- each one resolves what it checks and
runs, or states a precondition and fails -- and the precondition here is a
record of what the directory was meant to be. Configuring elsewhere stays
possible; quoting a verification result from a directory with no such record
does not.

The failing direction was reproduced before the gate existed: a wiped
`build/release` configured bare recorded `/usr/bin/c++` against a pinned
`clang++` and dropped the prefix path, and the same treatment of `build/asan`
recorded the sanitizer off, the build type wrong, and the compiler wrong. Both
are now refused by name, and both correctly configured directories pass.

Verification is by consequence against fixture trees, so nothing is configured
or built to measure it: the gate reads a preset file and a cache, and fixtures
present configurations a real tree would take minutes to produce. Twelve
scenarios cover the two bare cases, a directory differing in exactly one
pinned value, a pin reached only through inheritance, a compiler spelled as a
link to the pinned program, both floor removals, an unplaceable directory, an
unconfigured directory, and an unreadable preset file. Ten mutations of the
gate were planted and all ten caught, each by its own scenarios; an eleventh
removed a scenario from the runner list and is caught by a floor on the number
of results reported.

One defect was introduced and caught during the work rather than shipped. The
binary directory a preset uses is inherited here, and the template contains the
preset's own name, so expanding it while walking up the inheritance chain
substituted the parent's name and resolved `release` to a directory belonging
to no preset. Every real build directory then read as unplaceable, and the gate
answered about none of them. It is now expanded once, against the preset being
resolved, and a mutation restoring the earlier behaviour is among the ten.

Known limits, stated. The gate compares what a cache records, so it cannot see
a compiler that was replaced on disk after configuration, and it compares
locations rather than versions: two compilers of different versions at the same
path are the same program to it. Where a system default is a link to the pinned
compiler the comparison accepts it, which is the honest answer, since the build
really was produced by the pinned program; on the machine this was written
against the default is a separate file and is refused. Registered gates run
only when the battery is run, so a directory that never runs the battery is
unaffected by any of it.

---

## 2026-08-21 -- Compare paths by identity, and prove a harness is alive

The interlock published yesterday was measured again and let three things
through, all of them onto the session it exists to protect.

Paths were compared as text. The socket at `/run/user/N/wayland-1` was refused,
and the same socket spelled `/run/user/N//wayland-1` was accepted — one extra
character, a spelling a shell or a configuration file produces by accident. So
were the `/.` and `/../N/` forms. The kernel resolves every one of them to the
login compositor, and the refusal that was supposed to be the interlock's
strongest property did not hold for any spelling but the literal one. The
socket was also inspected with a call that follows symbolic links, so a link in
any writable directory pointing at the live socket was accepted outright: the
directory examined was the link's own, which is not the session's, and the
socket check looked through the link and saw a socket. No path trick was needed
for that one at all.

Both are now decided by identity rather than spelling. A directory is resolved
to its device and inode, which every spelling of it shares and no spelling of a
different directory does, and the socket is inspected without following links.
Identity is what the kernel agrees with; a string is what a caller typed.

The third was a design error rather than an oversight. The token was described
as proof because it is unpredictable, but nobody has to guess it: the check
compared a file the declarer writes against a value the declarer exports, and
any pair of equal strings satisfies that. Unpredictability buys nothing when
the verifier accepts any value that matches itself. What cannot be fabricated
by writing files is a lock the kernel releases when its holder dies, and the
harness already holds one on its run directory for the whole run. A gate now
requires that lock to be held, tested with a non-blocking lock attempt — never
`fcntl(F_GETLK)`, which on Linux does not observe these locks at all and would
have reported every lock free, turning the check into one that always refuses.
The socket's directory must also match the run directory the harness exports,
so "not the session's" is no longer the only bar a directory has to clear.

The headless proof had the same shape of hole one level down: it checked that
the compositor reads the selector and never looked at what the selector was set
to. Asking for the drm backend reached "compositor ready" and exited zero. On a
real wlroots compositor that is precisely the backend that takes the seat, the
virtual terminal and DRM master — the outcome the check exists to prevent,
reachable through the documented knobs rather than by sabotage. Values are now
allow-listed per selector, and a selector the harness does not recognise is
refused rather than trusted. Two smaller corrections follow it: the library
sweep reads ELF headers instead of executing the dynamic loader against the
binary, and the bypass used by the self-test is honoured only alongside a
non-production state directory, so one exported variable can no longer disable
the control that keeps this harness off the seat.

Two claims are corrected rather than softened. The comment describing the
selector search as "the observable trace of code that reads it" was wrong, and
the proof of it was sitting in the same commit: the positive control is a shell
script whose only occurrence of the name is inside an unexecuted here-document.
It does not read the variable in any sense and it passes. What the search
measures is the presence of a string. The direction that carries weight is the
failing one, and it is exact — a program that never mentions the name cannot be
reading it — which is why the value is now checked separately. And the harness
no longer claims to start nothing before its first refusal; it inspects files
and forks tools, and what it does not do is start a compositor.

The checks are now gated by `app_isolated_compositor_declaration`, which builds
its own run directories, sockets and locks and asks the shared check what it
makes of them. Each way through is an executable case, including the accepting
one: a check that refused everything would satisfy every refusal test and be
indistinguishable from a working one. Five mutations were planted and all five
caught, each by its own named case. One of those runs found a defect in the
test rather than the code — the login-session cases were being refused because
no token was present, so they proved nothing about the directory rule, and
reverting that rule to a string comparison left the suite green. The cases now
satisfy every other condition deliberately, so only the rule under test can
refuse. The files they place outside a temporary directory are uniquely named,
are not names any compositor uses, and are removed when the case ends.

State the limit plainly, since overstating it is what carried the previous two
versions past review. A local user who runs their own socket and holds their
own lock can still present something shaped like a harness. Unforgeable proof
is not available to a check running as the same user as the thing it checks.
What is claimed is that no declaration can resolve onto the login session's
compositor and no accidental or hand-typed declaration succeeds.

Verified: the declaration gate passes seventeen cases and each repair has a
failing witness; the harness self-test passes eleven scenarios including both
directions of the value allow-list; a genuine harness run is still accepted end
to end, so the refusal discriminates rather than closing the path. The
real-compositor path remains unmeasured on this machine and the contributor
guide continues to say so.

## 2026-08-20 -- Starting a compositor is an obligation to end it

The harness that runs compositor-dependent gates started a compositor and a
gate, each in its own session, and recorded both. What it did on the way out
was weaker than what it had taken on. Teardown signalled the gate's process
group once and moved on without waiting for it or escalating, then stopped the
compositor and deleted the runtime directory. A gate that did not stop for
SIGTERM therefore kept running against a display being torn down, inside a
directory being removed, with nothing said about it. A gate that ignores that
signal demonstrates it exactly.

The sweep that disposes of runs abandoned by an untrappable kill had the same
shape and a worse consequence. It sent SIGTERM to each recorded process group,
deleted the directory whatever happened next, and reported a reap. A compositor
that ignores SIGTERM survived it, and deleting the record removed the only
thing that named the survivor, so nothing could find it again -- while the log
said it had been disposed of. In production that survivor holds the GPU, a
seat, and a runtime directory.

Underneath both sat an identity problem. A pid is not an identity: numbers are
reused, and every signal here goes to a whole process group, so a stale record
would name whichever group now holds that number. Each pid is therefore
recorded with its start time and both must match before anything is signalled.
The start time was read in a way that returns empty on any failure to read it,
and an empty recorded value can never match, so one transient failure disabled
teardown, the escalation, and every later sweep at once -- silently, and with
the record deleted anyway. It is now a hard failure at the moment of recording:
a run that could not be torn down afterwards does not start.

What replaces all of it is one rule applied in both places. The gate is stopped
first, because it is the process using the compositor and reading the directory
that the following steps take away, and teardown waits for it. Each group is
escalated from SIGTERM to SIGKILL. A record is deleted only once every process
in it is confirmed gone; anything that cannot be confirmed is kept and named,
because the record is what keeps a survivor reachable. A run that finds an
earlier one it cannot dispose of does not start beside it. And a gate that
passed while its own teardown failed is reported as a harness failure rather
than as a pass, since something it started is still alive on the machine.

One defect was introduced and caught here rather than shipped. A process that
is exiting normally passes through a window where the kernel still answers
`kill -0` for it while its `/proc` entry has already stopped being readable.
Treating the first such reading as "cannot be accounted for" failed every
healthy teardown in the suite. What separates that window from a record that
has genuinely stopped being checkable is only that the second is still the
answer when the wait runs out, so the judgement is made there and not on first
sight.

Verification is by consequence, with stubs standing in for a compositor: a real
one is neither started nor needed for any of it. The self-test grew from nine
scenarios to eighteen, and the four defects were reproduced against the
unmodified harness first -- the ordering probe reported a refused connection,
the SIGTERM-ignoring gate outlived the harness, the SIGTERM-ignoring orphan
survived the sweep, the sweep claimed a reap it had not performed and deleted
the record, and an unreadable start time ran a gate that could not have been
torn down. Eleven mutations were then planted in the fixed harness and all
eleven were caught, each by the scenarios written for it and by no others; the
mutation runner compares each mutated file against the original, so a mutation
that failed to apply is reported as unmeasured rather than as a survivor.

Known gaps, stated rather than implied. The real-compositor path stays
unmeasured on this class of machine: the headless proof refuses a compositor
whose backend selector it cannot show to apply, and everything above is proven
with stubs. A process that genuinely survives SIGKILL cannot be fabricated, so
that branch is reported but not witnessed; the witness for the
cannot-be-accounted-for path instead makes the process records stop being
readable while the processes are still running. Liveness is asked of the kernel
rather than of the record, which has one known limit: a pid this user may not
signal reads as gone, a case that does not arise for a harness's own children.
And the early refusal for a record carrying no start time at all is a clearer
message on a path the general one already covers, so removing it changes no
result.

---

## 2026-08-20 -- Make workspace location follow the focused Miller column

Miller navigation now advances the pane's workspace adapter whenever keyboard
or pointer input focuses a different live column. The adapter remains the sole
owner of tabs and history, so the window title, path navigator, breadcrumbs,
tab label, navigation actions, and recent-destination state all observe the
same location as the column that holds focus.

The synchronization is directional rather than a binding loop. A
column-originated change updates workspace location without rebuilding the
chain it came from. Workspace navigation to a path already represented in the
chain focuses that column, while a path outside the chain replaces it with a
new root. Entry and transfer actions remain resolved through the focused
column listing throughout; updating workspace navigation state does not make
the invisible list/grid listing an action target.

The model test records the exact focused-path transition sequence across
right, left, right, and collapse. Rendered-shell coverage drives the same
transitions through keyboard and pointer paths, proves the live chain survives
its own location updates, proves an external destination resets the root, and
keeps the active entry model distinct from the workspace adapter. Release and
ASan/UBSan builds are warning-clean. Release passed 65 non-compositor entries
plus static analysis; ASan/UBSan passed the same 65 with static analysis
disabled. The two offscreen RHI probes skipped in both presets, and the two
real-compositor entries remained excluded under the session-safety policy.
Repository, formatting, QML, file-length, attribution, and application-smoke
gates pass.

## 2026-08-20 -- The filtered RHI gate carries every device-scaling check

The offscreen OpenGL validation entry deliberately filters the device-scaling
TestCase instead of running unrelated visual cases. Its per-axis-rounding
companion was added to that TestCase but omitted from the filter, leaving the
entry that supplies its GPU surface unable to run it. The registered function
list now includes the companion. `rhi_function_filter` rejects either
direction of that drift, so a future TestCase addition cannot silently become
offscreen-GPU coverage in name only.

## 2026-08-20 -- Protected-well mask edges retain their coverage

The presentation pipeline samples the protected-well mask independently from
the content frame. The shaders classify coverage at one half so an edge with
partial coverage stays protected without expanding into the one-device-pixel
ring outside a well. The source texture, however, inherited nearest filtering.
At a source/destination texel phase mismatch, a nearest lookup can select the
transparent texel beside a covered edge, exposing that pixel to bloom,
scanlines, and vignette even though the well's logical rectangle includes it.

The mask source now requests linear filtering explicitly. Its existing
half-coverage classification turns the filtered edge into the intended binary
decision: a covered border remains byte-true, while the outside ring remains
processed. No well geometry is inflated; a scale-independent expansion cannot
satisfy both boundaries at every device ratio. The device-pixel suite makes
the filtering choice observable, so changing the source back to nearest fails
before a frame comparison can quietly stop exercising the condition.

The mutation is measured on the offscreen OpenGL RHI surface: setting the
source to nearest fails `DevicePixelScaling::initTestCase`, and restoring
linear filtering passes the same entry. The real-compositor surface remains
unmeasured: the isolated-compositor harness currently refuses its production
backend selector before starting a compositor, and no interactive-session
render is an acceptable substitute.

## 2026-08-20 -- A declared compositor now has to be one this run created

The interlock that keeps GPU-path gates off a session in use required three
things of a declaration: `WAYLAND_DISPLAY` equal to `ODYSEA_ISOLATED_COMPOSITOR`,
a non-empty `XDG_RUNTIME_DIR`, and the resolved path being a socket. Every one
of those is satisfied by the machine's own session. Its Wayland socket is a
socket, and exporting two variables by hand was enough to authorise a window
that asks to be brought to the front on the surfaces someone is looking at. The
check proved that a compositor was listening, which is the single thing that is
always true of a live session, and never that this run brought one into being.

Proof of origin replaces proof of presence. The harness reads an unpredictable
token per run, writes it into the private runtime directory it creates, and
exports it. A declaration is accepted only when the directory holding the socket
is not the login session's runtime directory and holds a file, owned by this
user, whose contents equal the exported token exactly. Each condition is a stat
or a bounded read, each failure is a refusal, and an environment with nothing
set is refused. The token is not a secret — anyone who can read the private
directory can read it — and it is not meant to be. It separates a declaration
this harness made from one an environment inherited or a person typed, which is
the failure the interlock exists to stop. Both refusals were measured: a
declaration naming the live session is declined at both gate sites before any
window can exist, and a declaration the harness made is still accepted and still
reaches the graphics probe, so the refusal discriminates rather than closing the
path outright.

The harness's own default was worse than the hole it guarded. It set
`WLR_BACKENDS=headless` and described itself as headless. That variable belongs
to wlroots; the compositor installed here links a different backend library
whose selection uses an unrelated set of names, and the string appears in
neither the binary nor that library. The assignment was inert, so the compositor
would have started with no backend constraint at all — and with `WAYLAND_DISPLAY`
deliberately cleared, which is what selects a nested backend, the remaining
plausible choice takes a seat, a virtual terminal, and DRM master on the
machine's own display. A tool written to protect an interactive session would
have taken it over. The harness now reads the selector out of its own command
and refuses unless that program, or a library it links, contains the variable
name; absence is treated as proof the variable is unread, because that direction
produces a refusal rather than a run. On this machine it refuses, so the
real-compositor path stays unmeasured, and that is the correct trade.

Three further boundaries were closed at the same edge. The compositor no longer
inherits the session bus: its address is an absolute path that a private runtime
directory does not isolate, and this compositor's startup routine rewrites the
activation environment of whatever bus it can reach, which would have left the
live session pointed at a throwaway compositor and then unset those variables on
exit. It no longer inherits `HOME` or the configuration tree, so the real user's
startup entries cannot launch a second copy of that session's daemons. Neither
it nor the gate inherits `DISPLAY` or `XAUTHORITY`; the gates strip `DISPLAY`
themselves once they accept, but the harness accepts any command, and a fallback
onto an inherited X display is a hole this interlock has already had. The
harness also refuses to run with no `XDG_RUNTIME_DIR`, where its state would
otherwise fall back to a path any local user can create first — including as a
symlink that redirects the harness's own recursive delete.

Two claims in tracked text were false and are corrected rather than softened.
The harness header said it chose `WAYLAND_DISPLAY`; it clears the variable and
then discovers whatever name the compositor bound, which on a compositor that
numbers from zero is routinely the same name the live session uses. The
protection was always the private directory, never the name. And the contributor
guide said the two compositor entries run through the harness; they are
registered directly and decline, so nothing has yet run against a real
compositor. A refusal in place of a measurement is an honest state to be in, but
only while the text says so.

One line survives that cannot fail: the harness refuses a private runtime
directory equal to the ambient one, which a freshly created directory under the
run parent can never be. It is left in place as an invariant assertion and
labelled as one. It used to read as the control preventing a gate from being
pointed back at the inherited session, and a line that cannot fail is not a
defence.

Verified: the harness self-test covers nine scenarios including both directions
of the headless proof, with one command whose program contains the selector and
one that does not, differing in that respect alone. Release and ASan/UBSan
builds are warning-clean and their suites pass. The refused production path
starts no process, creates no directory, and exits before the lock is taken.

## 2026-08-12 -- One smoke gate replaces two independent implementations

Two independently complete implementations of the same application smoke gate
reached integration. Keeping both would run one criterion twice, inflate the
verification roster, and leave two scripts whose behaviour could drift. They
are reconciled into one `application_smoke` entry and one self-test under the
established `tools/check_smoke.sh` name, so the coverage battery still counts
one registered smoke rather than accepting duplicated evidence.

The consolidated gate keeps the stronger lifecycle and isolation properties.
It launches the application in its own process group, removes ambient display
variables, redirects XDG storage and the opened directory into a temporary
workspace, pins the offscreen software backend, and forces Qt diagnostics onto
standard error. A watchdog marker distinguishes the harness terminating an
application that survived the observation window from an early status 124;
signal deaths name the signal, and fatal Qt, scene-graph, or sanitizer output
fails an otherwise live launch.

The self-test now discriminates eight outcomes: a correctly isolated process
that stays alive, one that ignores SIGTERM and requires harness-owned SIGKILL,
immediate SIGABRT and SIGSEGV deaths, early statuses 0 and 124, a live process
reporting a platform failure, and a missing executable. The redundant script,
self-test, and CTest registrations are removed rather than retained as aliases.

The reconciled battery accounted for all 66 registered entries: 64 ran and the
two real-compositor entries declared their policy refusal before rendering.
Both offscreen OpenGL entries ran against an X context with the Wayland display
removed; no real-compositor path was invoked.

## 2026-08-12 -- A filtered gate now has to list what its TestCase defines

One GPU-path entry does not run its whole suite. `shell_visual_validation_rhi`
passes `TestCase::function` arguments straight through to the suite it
re-execs, because the validation scope also holds the broader visual cases,
which do not belong on the GPU path. That filter was a second thing to keep in
step with the QML, and nothing kept it there.

Both directions of the drift are silent. A function added to the filtered
TestCase and left off the list runs nowhere on that path: the entry passes, in
the time it always took, because it never ran the new function. A listed
function that no longer exists matches nothing, because Qt Quick Test does not
object to a filter matching none, so a rename leaves an argument behind that
covers nothing and the entry still reports success.

The first fault is survivable where the same function also runs under an
unfiltered entry — the two offscreen validation entries carry no filter, so
they would catch a plain failure. It is not survivable for a test that is only
meaningful on a real GL path, because such a test passes vacuously offscreen or
guards itself away there. Left off the list as well, it runs nowhere at all and
no entry turns red. That is not a hypothetical shape: it is the shape of the
ring-outside-the-well test, which is what exposed the hole.

`rhi_function_filter` resolves each filtered entry to the QML it actually runs
and requires exact agreement in both directions. The resolution is three hops,
all read from the build file rather than assumed: the entry names a launcher,
the launcher declares the suite binary it re-execs, and the suite declares the
directory its cases live in. A break anywhere along that chain fails by name,
because a gate that could not follow the chain has not checked the entry.

Completeness is required per TestCase, not per directory, and the distinction
decides whether the gate survives contact. A scope may hold several TestCases
and a filtered entry may legitimately want only some of them; demanding every
function in the directory would demand that the visual suite be added to a list
it does not belong on, and the first person to hit that would delete the gate.
So selection at TestCase granularity is treated as intentional and completeness
is required within each TestCase the filter names. The limit that follows is
stated rather than left to be found: a TestCase no filtered entry mentions is
not bounded at all, because omitting one is indistinguishable from choosing not
to run it.

The QML is parsed by walking each line character by character rather than by
matching whole lines, which two of the mutations below justify: a `name:` may
share a line with the brace that opens its block, and a brace inside a string
is not a block boundary.

Verified against the real record before anything else: adding a function to the
device-pixel TestCase without listing it is refused by name, and adding a listed
argument for a function that does not exist is refused by name; both restore
clean. Twelve self-test states, nine planted mutations, nine caught. Two of
those nine survived their first round because the fixtures could not tell the
readings apart — every fixture had exactly one named block, so attributing a
function to the outermost name rather than the nearest one gave the same
answer, and the only stray brace sat in a comment, which is stripped before the
walk. The fixtures now nest a named TestCase inside a named outer block and
carry a closing brace inside a real string above the functions, and both
mutations fail four scenarios each. A stray opening brace would still prove
little, since attribution falls back outward to the same TestCase; the closing
one is the brace that bites.

---

## 2026-08-11 -- Staged content is scanned before a commit exists, not after

The pre-commit hook checked attribution and nothing else. The
public-repository guard, which is what keeps private paths, personal data and
process vocabulary out of tracked text, ran only as a battery entry — so a
commit carrying such content was created successfully and found bad later, at
which point removing it means rewriting history rather than declining a commit.
This was not theoretical: a commit was made with process vocabulary in a new
comment and was not blocked.

The guard reads the index, so it judges exactly what the commit would publish.
It now runs in the hook, and its status is read directly rather than through a
pipe -- a pipeline reports the status of its last stage, which is how the same
check had already been run and its failure missed once.

It is skipped only when absent, which happens in a checkout predating it or an
export without `tools/`, and that case prints a warning rather than passing
quietly. A check that cannot distinguish "ran and was satisfied" from "was
never there" is the failure this project keeps finding in its own gates, so the
skip announces itself.

Verified by planting the same content that got through before: staged, the
commit is refused by name and no commit object is created; unstaged, commits
proceed. The hooks self-test passes unchanged.

---

## 2026-08-11 -- Make a skipped battery entry impossible to read as a pass

The verification battery could report every entry green while a third of them
never ran. A skipped entry keeps the headline at "100% tests passed" and is
named only in a trailing block below it, so the executed count could fall from
the full set to a subset without changing a number any check watched. Two
shapes of this were live: the two offscreen GPU launchers exited with the skip
code and zero bytes on both streams, leaving no cause anywhere; and the
smoke-launch criterion, "zero bytes on both streams", is exactly what a core
dump also produces once Qt routes its diagnostics to the journal rather than a
terminal.

A coverage reconciler now accounts for every registered entry. It captures the
roster live from `ctest -N`, so it can never lag the real suite, runs the
battery capturing per-entry results, and matches the two: an entry that ran, an
entry that declared a refusal, a skip that declared none, a silent skip, a
registered entry missing from the results, and a result not on the roster are
each classified and printed.

Exactly one shortfall is tolerated, along the distinction the compositor gates
already draw. A gate that cannot run is missing a capability, and that is a
hole in the battery's coverage. A gate that will not run is enforcing a
policy — refusing to render an activating window into a session it was not
given to own — and turning that red would pressure the next reader into
deleting the refusal rather than respecting it. So a skip is accepted only when
the entry printed a declared refusal, a line of the exact form
`<gate-name>: DECL -- declined: <reason>`; every other skip fails, including
one that explained itself at length. Tolerating any skip that printed something
would let a single `echo` reopen the whole failure class this is built to
close. The bound stays two-sided by construction — an empty roster, an empty
results file, and results that merely do not exceed the roster all fail — and a
run where nothing executed fails even when every entry declared a refusal, so a
declaration covers one entry and never a battery. It runs from
`tools/run_verification_battery.sh`, after ctest returns rather than as one
more entry inside it, because an in-battery reconciler would run mid-run under
`-j` and could not see the entries scheduled after itself.

The price of that standard is stated rather than discovered later: a machine
where the offscreen GPU launchers cannot obtain a context no longer produces a
green battery. Measured both ways here — without a display server the run is
red and names the two launchers as skips that declared no refusal; with one it
is green. The answer on a headless machine is a virtual display, not a wider
tolerance.

The two offscreen GPU launchers now name their skip and distinguish its cause:
"no display server reachable", where the offscreen platform has no display to
obtain a context from, is a different fact from "a display is reachable but the
OpenGL context is unusable", and the two no longer read the same. Where an
offscreen context is expected, `ODYSEA_REQUIRE_OFFSCREEN_GL` turns the skip
into a failure instead.

The smoke criterion is replaced. `application_smoke` launches the application
with `QT_FORCE_STDERR_LOGGING=1` so a fault names itself, and requires the
process to be alive when the timeout closes it — exit status exactly the
timeout signal — with no platform-plugin, RHI, scene-graph, or sanitizer fault
on stderr. An early exit and an abort are each reported by name rather than
read as quiet. Zero bytes is no longer sufficient evidence; the evidence is the
timeout status together with clean diagnostics. The launch is forced onto the
offscreen software scene graph, which renders at device pixel ratio 1 and runs
with or without a display; a real-GPU launch is the real-compositor gate's
stronger and separate job, not a second offscreen smoke run twice. Its backend
is selected with `QT_QUICK_BACKEND=software`, the real software key —
`QSG_RHI_BACKEND=software` is not a valid key and silently falls back to the
default, and no tracked file spells the software path that way.

The smoke gate and the reconciler each ship a self-test registered in the
battery, so a gate that stopped biting shows up as its own red entry. The
reconciler's self-test holds it to thirteen scenarios, and the load-bearing
ones are the tight half: an explained capability skip, a mention of the token
in prose rather than as the line's own declaration, and a declaration with no
reason after it each have to fail, separately and by name, alongside the silent
skip, the missing entry, the unexpected entry, the empty roster, and the floor
where nothing ran.

Verified: through the runner on a display, 66 registered, 64 run, and 2
declared refusals — the real-compositor gates, which decline because no
isolated compositor was declared for the run — with all 66 accounted for;
without a display the same tree is red and names the two offscreen launchers.
The real-compositor path itself is not measured here and is not claimed to be.
Warning-clean release build, static analysis at the unchanged baseline,
formatting, privacy, file-length, and QML gates all pass.

## 2026-08-11 -- The record's promised order is checked from a fixed point up

This file's header has always said the record reads in reverse-chronological
order, and nothing checked it. It is not true today: three entries dated
2026-08-09 sit above six dated 2026-08-10. Every date is honest about its own
commit, so what disagrees is landing order and date order, not the record and
reality.

The cause is integration rather than authorship. A branch appends its entry at
the top of this file, so every rebase onto an advanced record collides in
exactly that place, and the resolution puts the replayed entry back on top. A
branch written days before it lands therefore publishes an entry dated before
the entries already above it. The remedy at authoring time is small — an
unpublished entry is dated the day it lands — but nothing enforced it, and the
archive guard could not see it: it compares the manifest against the files and
compares published text against history, and neither of those reads a date.

The guard now enforces reading order from a fixed baseline entry upward. Order
is checked from the newest entry down to and including the baseline, which is
the newest entry that existed when the rule landed. The bound is not a
convenience. Published entries are never edited, reordered, or removed, so a
rule demanding order over the whole record would demand that published text
move; it would fail on the day it landed and be deleted the day after. Bounded,
it prevents every recurrence without touching a single published entry, and the
disorder already in the record stays visible rather than being quietly
rewritten.

What it cannot catch is recorded with it, in the guard and in `CONTRIBUTING.md`:
disorder below the baseline, by construction; an untruthful date, since order
is checked against the dates the record carries and nothing compares an entry's
date against its commit's; and the order of entries sharing one date, which is
unconstrained by design. Its own baseline is a constant in the guard, and
advancing it would retire the rule for everything in between — so the constant
carries that prohibition next to it rather than leaving it to be inferred.

Six scenarios hold the guard to both halves: disorder below the baseline is
accepted in the live record and inside an archive, an entry published above a
newer one after the baseline is refused by name with both headings, an entry
landing directly above the baseline out of order is refused — a check stopping
one entry short would accept exactly that, and it is the commonest shape of the
defect — two entries sharing a date are accepted in either order, and a
baseline naming an entry the record does not hold fails rather than checking
nothing. Every existing scenario's fixture record now carries the baseline
entry, so the whole suite runs with the check live: 42 scenarios, all passing.
---

## 2026-08-11 -- The prohibition is published, not only the mechanism

The rule that verification must not act on a session in use existed only as
mechanism. No tracked file stated it. That is the same condition that produced
the practice the mechanism now blocks: a procedure carried outside the
repository, inherited by nobody, and reconstructed from scratch by whoever
needs it next. A contributor reading the tracked policy would have found
public-repository safety, attribution, the file-length ceiling and verification
discipline, and nothing at all about the display session.

`CONTRIBUTING.md` now carries it. Tests that render run offscreen or against a
compositor started for the run; nothing creates, resizes or removes an output,
moves focus, installs window rules, or opens a window on a session in use. The
text states how it is enforced, that a declaration is checked as a claim rather
than trusted as evidence, and why the refusal is a skip that the required-mode
override deliberately does not convert into a failure.

It also states the cost, because a rule that hides its price gets removed by
whoever meets the price later: the real-compositor path is exercised only where
an isolated compositor can be started, and is reported as unmeasured elsewhere
rather than approximated against whatever session is available.

---

## 2026-08-11 -- An isolated compositor is created and owned for GPU gates

The real-compositor GPU gates now have a harness that creates the compositor
they may target. It makes a private runtime directory, starts the compositor
there, waits for the socket it created, and gives the gate that socket through
`WAYLAND_DISPLAY` and `ODYSEA_ISOLATED_COMPOSITOR`. The latter now carries the
socket name rather than a constant, so the launcher's socket-binding interlock
can reject a stale or incomplete environment before a test window is created.
A compositor that never advertises its socket is refused before the gate runs.

The harness holds a fixed `flock` across worktrees, not only within one CTest
invocation. Its teardown handles a passed or failed gate, SIGINT, and SIGTERM;
it closes lock descriptors for child processes so a killed harness cannot leave
an orphan holding the lock. An abandoned run is reaped by the next invocation
only after its liveness lock is free, and recorded process start times prevent a
reused pid from being signalled.

The headless compositor command remains a capability-dependent integration
path. The self-test uses a real local Unix socket without opening a window and
checks the socket declaration, no-socket refusal, result pass-through,
cross-worktree exclusion, signal cleanup, SIGKILL residue reaping, and argument
validation. All eight scenarios passed.

## 2026-08-11 -- A declaration is a claim, not evidence

The session interlock accepted a declaration that named a Wayland socket
matching `WAYLAND_DISPLAY` without checking that anything was listening on it.
A harness whose compositor died, or never bound, presents exactly the
environment of one that succeeded, so the gate treated the failed case as an
authorised session and went on to probe for a context.

The reported symptom was a wrong state: an absent socket produced an inability
to run rather than a refusal. The message that came with it was worse than the
state, because it said a compositor was advertised when nothing was listening.
Keeping those two readings apart is the entire purpose of the separate refusal.

Underneath was a second and larger problem in the suite runner. That runner is
reached with no platform pinned, so a Wayland socket that cannot be connected
to does not end the run: Qt falls back to xcb and renders on the ambient X
display. Accepting an unbound socket there would have handed a window to the
session the check exists to protect, by way of a declaration that looked
correct. The socket must therefore exist, and `DISPLAY` is removed once a
declaration is accepted so no fallback can reach a session no harness created.

Verified by discrimination on both binaries. A declared socket that is absent,
and a declared name that resolves to a regular file rather than a socket, are
both refused; the absent case stays a refusal under the required-mode override
rather than turning red. Against a real listening socket both binaries accept
the declaration and proceed, failing afterwards on their own terms because a
bare socket is not a compositor -- which is the correct boundary for a check
that answers whether a session was prepared, not whether it works. Release
62/62 with the two compositor entries declining.

---

## 2026-08-09 -- Cancellable fuzzy find across the current tree

The shell now exposes current-tree fuzzy find through a toolbar button, the
shared action registry and command palette, and `Ctrl+Shift+F`. Its modal
surface follows the command palette's focus, dismissal, keyboard-navigation,
pointer-activation, and accessibility conventions. Activating a directory
enters it. Activating a file enters its parent and reveals that exact entry as
the current selection, including when the previous folder filter would have
hidden it.

The Qt-free core separates tree indexing from ranking. Opening the surface
starts one cancellable recursive walk, which never follows directory links and
stays on the starting filesystem unless explicitly configured otherwise. The
result becomes an immutable in-memory corpus with folded names and relative
paths. Each keystroke submits only that corpus and the new query to a separate
newest-request-wins ranker; it never rescans or copies the tree. Both loops poll
cancellation per entry, and hidden subtrees follow the active view setting.

The performance gate ranks 50,000 synthetic paths over eight successive
keystrokes. Every completed query must account for all 50,000 candidates and a
nonzero number of character comparisons, while bounding comparisons per
candidate so an instrument that stops counting cannot satisfy the ceiling.
The release measurement averaged 1 ms per keystroke with a 3 ms maximum. An
adapter gate separately builds an 8,224-path filesystem tree and measures five
successive queries below the clock's 1 ms resolution while requiring the
filesystem-walk count to remain exactly one. Revealing a result in a 514-row
listing built 2,824 keys and 14 row indexes, inside nonzero bounded counters.

Verification passed the warning-clean release and ASan/UBSan builds, all 53
executed runtime and safety checks in each supported offscreen configuration,
staged static analysis, formatting and QML module gates, the public-repository
and file-length guards, and eight-second software smoke launches. Four GPU and
compositor probes skipped where the offscreen environment could not exercise
them. A separate real-compositor presentation pass succeeded at 1.67x scale.

## 2026-08-09 -- Make columns ownership verification discriminating

Column listings are owned by their C++ controller even when QML obtains them
through the controller's invokable accessor. The existing teardown coverage
exercised that lifetime, but it still passed if the explicit ownership marker
was removed, so it did not guard the contract it described.

The shell loader test now evaluates the invokable accessor through a real QML
context and checks the returned listing's engine ownership. A deliberately
broken build without the ownership marker changed the observed value from C++
ownership to JavaScript ownership and failed the new assertion. The mutation
path restores safe ownership before reporting the failure so the test itself
does not create a second owner.

Release and ASan/UBSan suites passed, including 55 release checks and 54
sanitizer-enabled checks; four renderer or compositor capability cases skipped
in the headless environment. Scoped static analysis and eight-second release
and sanitizer software-renderer smoke launches also passed.

## 2026-08-09 -- Keep cross-pane transfers on visible listings

Transfers now resolve both ends through the listings each pane displays. When
columns mode exposes a deeper directory in the opposite pane, copy and move
use that directory instead of the workspace location from which its chain
opened. An explicit readiness condition disables the action while either
columns listing is unavailable. Miller rows also publish URI drag payloads and
accept directory drops, matching the pointer path already present in list and
grid views.

Two planted regressions were rejected: restoring the hidden workspace
destination failed the two-pane depth case, and removing the URI payload failed
the pointer-drag case. All 55 release checks and 54 enabled ASan/UBSan checks
passed; four hardware capability probes skipped in each offscreen run. Static
analysis completed in 216.39 seconds, and both eight-second software smoke
launches stayed silent and sanitizer-clean.

## 2026-08-10 -- The watch-burst cost bound had no floor under it

The gate that holds a folder-watch burst to a linear cost checked only that the
number of keys constructed stayed below a ceiling. A ceiling alone is satisfied
by an instrument that has stopped reporting: zero is below every ceiling. That
is not a hypothetical failure in this repository - a counter guarded by a
conditional that stopped applying has already read as a pass here once, and the
same instrument is bounded on both sides everywhere else it is used.

The bound now has a floor, and the floor is derived from behaviour rather than
picked to sit under the observed number. A burst ends by reconciling the
listing against the presented rows, and that reconciliation identifies a row by
its key, so the work cannot cost fewer than one key per presented row. The
fixture presents every entry it creates, holding no hidden or filtered names,
so the floor is the entry count. The measurement sits exactly on it: 800 keys
for 800 presented rows, unchanged across repeated runs. Sitting on the floor is
the point - a later change that made the reconciliation cheaper would have to
restate the claim rather than pass quietly under it.

Both bounds are now shown to discriminate, each separately. Removing the
increment that feeds the counter leaves the burst reading zero keys and fails
the floor, where it previously passed the ceiling. Restoring a per-name linear
scan over the listing takes the same burst to 241,000 keys and fails the
ceiling. Both failures name the quantity, the population it was measured
against, and the bound it crossed, so neither reports only that a comparison
was false.

No behaviour changed. This is the cost claim being made falsifiable in the
direction it was not.

Verified: release 59/59 and ASan/UBSan 58/58 with a display attached,
warning-clean.

## 2026-08-10 -- Static-analysis drift reported new diagnostics as departed ones

The static-analysis gate compares the current advisory diagnostic set against a
recorded baseline and reports which way each entry moved. The comparison loaded
the recorded set with awk's record-number idiom, which identifies the first of
two files by whether the record number has restarted. That test is true for the
second file's records whenever the first file is empty, so with an empty
baseline the current set was loaded as the recorded one. A genuinely new
diagnostic then matched itself, was deleted from the recorded map, and was
finally reported as a diagnostic that no longer occurs.

The gate still failed, so nothing passed that should not have. What it printed
was the opposite of what happened, on a gate whose only output is telling a
reader which direction the set moved. An empty baseline is not a hypothetical
state either: a ratchet that only turns downward has zero as its target, so the
wrong reading gets more likely the closer the tree gets to the goal.

The two sets are now tagged and read as a single stream, so which set a record
belongs to is carried by the record instead of inferred from how many have been
seen. This is the same defect and the same repair as the one made in the devlog
archive guard, where it was written and then found by that guard's own
zero-baseline scenario.

No floor was added on the recorded set being empty, and that is a deliberate
refusal rather than an omission. Refusing an empty baseline would forbid the
ratchet from reaching the state it exists to reach. The vacuity that can be
refused already is: with nothing analysed there is nothing to compare, and the
existing translation-unit floor rejects that by name. What was missing was any
way to tell an exhausted baseline from a held one in the log, since both print
as a pass, so the success line now states the number of recorded entries as
well as the diagnostic total.

Verified: 10 self-test scenarios, three of them new - a new diagnostic against
an empty baseline is reported as new, is separately required not to be reported
as departed, and an exhausted baseline over a clean tree passes while stating
its size. The second of those is asserted separately because the gate prints
one line per drifted entry, so a comparison emitting both readings would
satisfy the positive assertion alone. Restoring the previous comparison exactly
fails those two scenarios and no others. Four mutations of the comparison, the
stream tags, the reported size and the translation-unit floor, four caught.
Against the repository itself the recorded set is unchanged at 98 entries and
291 diagnostics across 52 translation units.

## 2026-08-10 -- What the history baseline could still be talked out of

The record's lower bound is the published branch's history, and three ways
around it were found and closed. Each was reproduced end to end before anything
changed, and each fix is pinned by a scenario that fails when only that fix is
reverted.

A merge could retire a published entry. `git rev-list <ref> -- <paths>` applies
default history simplification: where a merge is TREESAME to one parent for
those paths, the walk follows that parent and discards the other side entirely.
A branch that touches nothing in the record, merged back with the record files
resolved to its version — an ordinary take-theirs resolution — is TREESAME to
it, so an entry published on the other parent is never walked and never
demanded. The entry is then in no file, in no manifest, and in nothing the
guard reads, and the run reports a clean record. This was not hypothetical: on
this branch simplification was already discarding four record-touching commits
behind its eight merges, 99 walked against 103. The walk now asks for full
history. Reproduced by publishing an entry, branching from before it, changing
one unrelated file, and merging back: 89 published, 88 compared, refused by
name, where the previous guard reported 88 of 88 and exited zero.

The baseline ref was not itself out of reach. The commits history holds cannot
be edited by the change being judged, but the choice of ref could be, and the
guard's own header claimed otherwise. The gate runs after the commit exists, so
with local `main` as the baseline a rewrite committed there was compared
against itself: the same one-word edit failed in a working tree and passed once
committed. The baseline is now `origin/main` in preference to `main`. The
residue is recorded rather than implied — a rewrite committed on an integration
branch already fast-forwarded past it, in a clone with no `origin/main`, is
still judged against itself, and pushing is what closes it.

Resolving only two ref names left the unchecked path reachable in a repository
carrying the whole of history. Renaming the branch and the remote produced a
clean exit and an unchecked notice while a published entry was missing, and
that notice goes to standard output where a passing run hides it. `HEAD` is now
the last candidate, and it resolves in any repository that has commits: the
same tree is refused at 88 published, 87 compared.

An overclaim is corrected rather than carried. The manifest is excluded from
the blobs history is read from, and the reason recorded for that exclusion
being unreachable was the order the blobs arrive in. That is wrong. Removing
the exclusion alone changes no behaviour, but not because `DEVLOG.md` wins a
race: the blob filter admits only `.md` files, so a `.txt` manifest is rejected
before the name is ever compared. The exclusion is the principled check and the
extension is the accident, so it stays, and the self-test pins the pair —
exclusion removed and filter widened, which is what renaming the manifest to a
`.md` file would amount to — because no mutation of the exclusion by itself can
be observed while the manifest keeps a name the filter rejects.

Verified: 36 self-test scenarios, up from 30. Eight mutations planted, six
caught, each failing precisely the scenarios it should and no others; the two
survivors are the manifest exclusion taken alone, equivalent for the reason
above, and no scenario was invented to make them look load-bearing. The guard
reads 88 published and 88 compared against the working tree in 0.62 s. Release
59 of 59 and sanitizer 58 of 58 from wiped build directories, warning-clean,
with the advisory static-analysis baseline unchanged. The two real-compositor
entries report themselves skipped and are recorded as not measured rather than
counted as green.

One measurement correction belongs here rather than in a later entry. A smoke
launch on the offscreen platform with no display server available loads the
software scene-graph backend even when the OpenGL backend is requested, and it
says so only when Qt diagnostics are forced to standard error. Two smoke
configurations were therefore measured here, not four: the pair that differed
only by requesting OpenGL were the same software run twice. Both completed
silently, stayed alive for the full launch window, ended on termination, and
left the shared-memory directory as they found it.

## 2026-08-10 -- The session interlock names the compositor it authorises

The interlock added with the compositor gate's refusal was a presence check on
a single environment variable. Presence is not a declaration. An empty value
passed it, and so did the string `false`, which meant a harness that failed to
create the socket it meant to name would export exactly the marker that
authorised the run it had not prepared.

Worse, the marker authorised a run without constraining what that run targeted.
The gate went on to read the ambient environment for a display, and its second
choice was X11. A declaration set alongside an inherited `DISPLAY` therefore
resolved to an X11 session the harness never created and would not tear down.
Measured rather than reasoned about: with a declaration present and a
nonexistent X display, the gate passed the interlock and reached the OpenGL
probe, failing only because that display does not exist. A working one leads to
the exec.

The declaration now carries the Wayland socket, so it says what it authorises
instead of merely that something is authorised. An empty value is refused.
`WAYLAND_DISPLAY` must equal the named socket, so an authorisation for one
session cannot be spent on another. The X11 path is gone rather than
deprioritised, because the declaration cannot name an X display and any such
fallback could only reach a session the harness did not create; `DISPLAY` is
removed from the environment before the suite is exec'd.

The suite binary enforces the same rule itself. Where a suite renders was
decided entirely by CMake test properties and by the launcher, and neither is
part of the binary, so running it directly -- the first thing anyone does when
investigating a compositor failure -- reached the ambient session with nothing
in the way, and with no platform pinned Qt falls back to it unprompted. The
scenes call `requestActivate()`. The shared runner for the GPU-path suites now
permits only a non-rendering platform or a declared isolated compositor, and
exits 77 with both permitted forms named so the next step a reader takes is a
safe one. A policy enforced for the registered entries and documented for
humans is the shape of the practice this exists to end.

Verified by discrimination on each closed hole: empty declaration, the literal
`false`, a declaration whose socket disagrees with `WAYLAND_DISPLAY`, and a
declaration alongside an inherited `DISPLAY` all refuse, the last one refusing
under the required-mode override too. The distinction the refusal was built to
preserve still holds -- a matching declaration pointed at a socket with nothing
behind it reports an inability, not a refusal, and still exits 1 under that
override. Direct runs of the suite binary against an ambient display refuse,
while the offscreen form every registered entry uses is unaffected: release
59/59 with the two compositor entries declining, and the offscreen GPU gates
still running at full duration, 76.27 s and 25.54 s.

---

## 2026-08-10 -- The compositor gate will not run against a session it does not own

The real-compositor validation gate rendered onto whatever display server was
advertised in its environment, and the suite it launches asks its window to be
activated. Running the test suite inside a session someone was using could
therefore move that session's input focus onto a test surface, and if the run
ended without restoring it, the focus stayed moved. Where the surface was not
visible, the result was a session that ignored keyboard and pointer input while
the compositor, the kernel, and the drivers were all working normally. That
outcome was reached, and recovering from it required restarting the session.

The gate now declines any compositor that was not declared as started for the
run. `ODYSEA_ISOLATED_COMPOSITOR` is that declaration, and the check runs before
the environment is read for a display, before the OpenGL probe is forked, and
before any window can exist, so declining touches nothing at all. A test that
can take over an input device is opt-in against a disposable compositor, not
opt-out against whatever happens to be there.

`ODYSEA_REQUIRE_COMPOSITOR` does not override the refusal, which is the part
worth stating precisely, because that override exists to stop a skip reading as
a pass. An inability to run still turns red under it: with the interlock
satisfied and no display advertised, the gate reports FAIL and exits 1 exactly
as before. A refusal exits 77 whether or not the override is set. The two are
different states with different exit texts, because a gate that punished the
refusal would only teach the next reader to delete the refusal.

What this costs is stated rather than absorbed: no harness starts an isolated
compositor yet, so the entry declines everywhere and the real-compositor path
is unmeasured. Its earlier results are not a baseline either. That gate was
observed passing five of five runs on one day and failing four of five on
another with nothing changed between them, because its outcome depended on
which output the window reached and on the set of outputs being changed around
it. Results gathered against an unowned session are treated as uninformative in
both directions rather than retained as history.

Verified by exercising all four exit paths. No declaration, exit 77 with the
refusal text. No declaration with the override set, exit 77, unchanged. The
declaration present with no display advertised, exit 77 with the inability text
and the old skip reason. The same with the override set, exit 1. The refusal is
what stops the first two: satisfying the interlock alone moves the gate to a
different exit path with a different message.

---

## 2026-08-10 -- The record is bounded by history, not by a file it ships with

The manifest introduced with the part split was described as bounding the
record from below. It does not, and the claim was wrong in the tracked text as
well as in the guard. `docs/devlog/published-entries.txt` is a tracked file, so
the change that drops a published entry drops its manifest line in the same
change, and every remaining rule then holds over a record with a hole in it:
nothing duplicated, nothing misordered, no month misfiled, manifest and files
in exact agreement. Removing one entry and its line took one edit and the gate
reported success by name over the result. A record cannot be bounded by a file
whoever removes the entry also writes.

The baseline is now the published branch's own history, which the change under
test cannot edit. The guard walks every commit that touched the record, reads
every state the record has ever been published in, and requires each published
entry to be present and to still carry the text it was last published with.
The comparison is non-forgetting: an entry deleted several commits ago is still
demanded today, which a comparison against the previous commit alone would not
do. An entry history has never seen is new, and a new entry that sits in an
archive rather than the live record is refused - that is how an entry ends up
published where nobody reads first.

Three constraints shape it. Bodies are compared with boundary blank lines and
the horizontal rule between entries normalised away, because moving an entry
changes exactly those and nothing that was written; a byte-exact comparison
would call every part split a rewrite and could not have landed. The baseline
is one branch and never every ref, because refs that are not ancestors of it
hold record states that were never published, and a baseline drawn from those
would import unpublished material into the standard the public record is judged
by and then demand entries nobody ever published. And an entry that history
holds in more than one form is measured against the most recent one, because
against its oldest the gate would demand text the branch itself no longer
carries.

What is checkable divides by environment, and the guard now states which side
it is on. In a repository, loss and rewriting are both caught mechanically,
because history is the copy of what was published that the comparison needs. A
source tree extracted from a release archive carries no history, so the run
says the published record is unchecked there by name and checks the internal
agreement that remains. The earlier claim that no check could see a rewrite was
true of an archive and false of a repository.

The first version of the comparison carried the defect it was written to
prevent. It read two streams as two files and told them apart by record count,
which silently reads the second file as the first when the first is empty: with
nothing ever published, every entry present was treated as published and every
published entry as missing. The scenario that publishes nothing found it, and
the streams are now tagged and read as one. The same defect is recorded as
still living in the static-analysis drift comparison, where an empty recorded
baseline makes a new diagnostic report as one that no longer occurs.

Verified: 31 self-test scenarios, including an entry dropped three commits back
with its manifest line, a verbatim move, a move whose boundary whitespace
changed, a move whose text changed under an unchanged heading, an unpublished
entry written straight into a part, an entry published in more than one form, a
commit on a ref outside the published branch, and a baseline that read nothing.
12 planted mutations of the baseline, the comparisons, the normalisation and
the floor, 12 caught. One survivor is not counted as coverage: excluding the
manifest from the historical read is defence in depth rather than behaviour,
because a tree whose manifest names an entry no file holds is already refused,
and within a commit the record files are read before it. ASan/UBSan 58/58 and
release 58 of 59 with a display attached, warning-clean. The one failure is
`shell_visual_validation_compositor_2x`, which is unstable on this display
independently of this change: it fails two runs in five on the unmodified
parent commit, and in three different checks across runs - a device-resolution
comparison, a well-border colour, and an over-protected device pixel - while
reporting that the window manager did not apply the size the layout audit
requested. Nothing outside `tools/`, `CMakeLists.txt`, and the record itself
differs from that parent.

---

## 2026-08-09 -- Guards a build from a release archive would not have run

Fourteen of eighteen gates declined to run in a source tree without repository
metadata, and the summary still read as a complete pass: a skipped test is
reported outside every line that counts, so a battery that ran a third of
itself prints what one that ran all of it prints. The condition was single -
every guard derived its corpus from Git, and every one was gated on the same
predicate. That is the environment a source archive is compiled in, and
packaging is on the roadmap, so the first packaged build would have reported a
green battery having run neither the privacy guard, nor the attribution
checks, nor the file-length ceiling, nor static analysis.

The corpus is now resolved in one place behind one interface. Where a
repository is present it is the tracked set read through the index, which is
the stricter reading and is what a commit would publish. Where it is not, it is
the source tree, less version-control metadata, the ignored workflow directory,
and every CMake build tree beneath the root - recognised by the cache file
inside it rather than by its name, so a build directory called anything at all
is still excluded. Matching is shared and only enumeration differs, and the
accompanying self-test requires the two enumerations to answer identically over
the same tree rather than checking each against a description of what it ought
to return.

Two things are genuinely unavailable without a repository: the index, and the
history the attribution checks read. Those checks now state that they did not
run and why, rather than the whole guard disappearing. Everything else runs.

No gate declares a skip return code any more. Each one resolves a corpus and
runs, or states a precondition and fails. The two self-tests that declined when
Git was absent now report that as a failure, because a self-test that ran
nothing has established nothing, and their prerequisites are recorded as
requirements of a checked build rather than of a developer checkout.

Verified: every gate executed in a tree extracted from an archive carrying no
repository metadata, the privacy guard among them, and it found an address
planted there. 11 planted mutations of the enumeration, the build-tree
exclusion, the index read, the root inference, the restored skip and the
floors; 10 caught. The eleventh removes a conditional whose effect is not
observable - the checks behind it read an empty history either way - and it is
recorded as such rather than counted as a catch. Release 59/59 and ASan/UBSan
58/58 with a display attached, warning-clean.

## 2026-08-09 -- Gates that measured nothing reported success

Two gates passed over an empty corpus. The formatting gate printed that zero
files had passed and called it a pass; the file-length gate printed no count at
all, so its success line read the same over the whole tree as over none. Every
file in an empty corpus is formatted correctly, and every file in it is under
the ceiling: both results were true and neither meant anything. The corpus is
enumerated from a repository root, which is exactly the kind of thing that can
resolve somewhere unintended with no visible sign.

Both now refuse an empty corpus by name and state the size of what they
measured, so the success line can be checked by reading it. The file-length
gate carries a second floor, because a corpus holding only binary assets
measures nothing either. Static analysis gained the same floor, where the
coincidence is sharper: with nothing analysed there are no diagnostics, and an
empty set matches an emptied baseline exactly.

The three gates without a self-test were the file-length ceiling, the QML lint
pass, and static analysis, and two of those three were the two that passed
vacuously. Each now has one. The ceiling is exercised at exactly 2,000 and
2,001 lines, separately in the working tree and in the index. The lint
self-test requires a scene with an unqualified access to be rejected, and
counts the corpus in its success line. The static-analysis self-test holds the
baseline in both directions: an unrecorded diagnostic is refused as new, and a
recorded one that stopped occurring is refused as well. Every scenario requires
a specific message rather than a bare non-zero exit, because a gate failing for
an unrelated reason would satisfy the weaker check.

Verified: 13 planted mutations of the floors, the ceiling boundary, both halves
of the index comparison, the fatal report, the drift comparison and the lint
threshold; 13 caught, keyed on exit status. Release 58/58 and ASan/UBSan 57/57
with a display attached, warning-clean.

Known gap: with an empty recorded baseline, the static-analysis comparison
reports a new diagnostic in the wording meant for a cleared one. The gate still
fails, so nothing passes that should not; the message is wrong. The self-test
keeps clear of that path rather than pinning it.

## 2026-08-09 -- The record is archived in parts before the month closes

The development record had 105 lines of headroom under the 2,000-line ceiling
and was growing by roughly forty lines an entry. The rule that keeps it small
moves a month into `docs/devlog/YYYY-MM.md` once the month closes, and August
has not closed, so the only sanctioned relief was unavailable for three more
weeks. Nothing further could be recorded, and shortening prose to fit is
explicitly not a way out of a length ceiling.

The rule now permits archiving an open month in numbered parts. What made this
the smallest available change is that the file already describes the mechanism:
entries move verbatim, and a published entry is never edited, reordered,
reworded, or removed. Moving entries was already sanctioned; only the timing
was not. A month is archived whole or in parts, never both, parts are numbered
from one without gaps and hold consecutive increasing stretches, and they are
not merged back when the month closes — a published part is as settled as a
closed month.

August is now held in three parts and the live record keeps the newest entries.
`DEVLOG.md` falls from 1,895 lines to 145, so the ceiling is no longer near.
The move was a transcription: recombining the four files reproduces the
pre-split body byte for byte, 114,683 bytes with md5 checksum
58ab090854827896176b0d0becf6fe01, and that was checked rather than eyeballed.

Structure alone cannot protect a record split across files. Every arrangement
check here — naming, linking, ordering, month membership — is satisfied by a
record with a stretch missing from it, and the missing stretch is exactly what
a split or a later rebase loses. So the record now carries
`docs/devlog/published-entries.txt`, every published heading in reading order,
and the gate requires it and the files to agree exactly. That bounds the record
from below as well as above: an entry cannot be dropped, and equally cannot be
reworded, reordered, or duplicated.

Stated plainly, because a manifest invites more confidence than it earns: it
pins headings, not bodies. Rewriting the text under an unchanged heading passes
untouched, and nothing here can see that without a copy of what was published
to compare against, which a source tree extracted from a release archive does
not carry. The rule against editing a published entry rests on review; what is
mechanical is that no entry silently disappears, changes place, or changes
name.

Verified: 21 self-test scenarios covering each refusal and an accepted layout;
18 planted mutations of the manifest comparison, the part rules, the ordering
and the floors, 18 caught. Two needed re-planting before they counted. Widening
the archive name pattern to anything beginning with a month survived the first
round, because the only misnamed fixture did not itself begin with one; the
scenario that discriminates it was added and the mutation planted again. The
second was re-planted because the code it targeted had been rewritten under it,
and a mutation that no longer applies has measured nothing. Release 55/55 and
ASan/UBSan 54/54 with a display attached, warning-clean.

## 2026-08-09 -- Restate the columns gap to the one that remains

The roadmap described two gaps behind the columns view. One of them has since
closed: entry actions resolve against the focused column listing, and the pane
header names that listing. The roadmap text still asserted the closed gap, so
it described a defect the tree no longer has. It now states only the remaining
one, that the shell's location does not follow the column chain. The entry
stays unclaimed.

## 2026-08-09 -- A directory is watched before it is read, not after

A listing was acquired by reading the directory and then establishing a watch
over it. Those two steps describe adjacent stretches of time, and in that
order they do not meet. A reading reports what was there while it ran; a watch
reports what happens after it exists. Between the reading passing an entry and
the watch coming into being there is an interval that belongs to neither, and
a change made in it is not late but absent: it is not in the listing that
replaces the presented rows, and no event ever describes it. The listing stays
wrong until something forces another reading.

The interval was not a narrow one. Establishing the watch only queued the
request for another thread, and the request was made after the reading
completed, so the gap ran from the moment the reading passed an entry to some
point after completion. One code path acquires every listing, so every
navigation, refresh, and post-operation reload carried it.

The order is now reversed. A reading starts only once the watch over the
directory reports itself established, so a change is either already in what
the reading returns or is reported as an event, with no moment that is
neither. The watch service gained that report: a request to watch a directory
is answered when the watch has been attempted, carrying the failure when there
was one, so a caller waiting on it is never left waiting for a watch that will
not exist. A service that has been stopped answers immediately for the same
reason.

Reversing the order moves the events into the middle of the reading, where
applying them would be wrong: the completed listing replaces the presented
rows outright, so a change applied to the rows being assembled would be undone
by the completion that follows it. Deliveries that arrive while a reading is
in flight are therefore held and applied on top of the listing it produces,
before the model reports itself idle. Idle now means the listing is current,
not that the reading finished.

Two smaller consequences came with it. The model can no longer report itself
idle before the watch exists, because the watch is established while it is
already busy. And a reading that has been superseded is still delivering
during the moment its replacement spends establishing a watch, so the current
reading's token is cleared when the replacement is asked for rather than when
it starts; without that, a superseded delivery would be published under the
new directory's name.

Verified by eight planted defects, each rebuilt and run: reading before
watching, holding nothing, holding without applying, not reporting the watch
established, reporting it established before establishing it, a stopped
service accepting a request it cannot answer, and leaving the superseded
token current. All eight were rejected. Reading without waiting for the report
was rejected only by the check that the reading has not started while the
report is outstanding; no case observes the residual interval it leaves, which
is one thread hop wide, and none is claimed to. The branch that reports an
established watch when no watcher could be created at all is also uncovered:
nothing in the suite makes the kernel refuse one.

Release and sanitizer suites pass warning-clean with the GPU-path gates run
against a display, and both binaries completed silent offscreen smoke launches
on the OpenGL and software paths.

## 2026-08-09 -- Harden columns ownership and focused-view actions

Column listings now remain under one explicit C++ owner when exposed to QML,
so tearing down the shell with a live columns chain destroys each adapter once.
The sanitizer shell-load gate covers that exact lifetime boundary with a real
directory listing active at engine teardown.

The workspace adapter continues to own tabs, history, and the location from
which a columns chain begins. Entry operations instead follow the focused view:
copy, move, rename, trash, select-all, operation dialogs, status, and cross-pane
transfers use the active column listing while columns mode is visible. The pane
header identifies that active listing's path, so an operation cannot resolve to
a selection retained invisibly by the list or grid model.

Text filtering applies only to the active column and releases descendants
before filtering can remove their anchor. Shared presentation settings still
fan out over the live chain, so their work remains proportional to the number
of visible columns; the per-keystroke filter path no longer pays that factor.
The model gate bounds key construction and row-index rebuilding above and below
using deterministic counters, and separately proves that selecting an already
open branch reuses its live listing. The five-level, 5,004-row fixture loaded in
253 ms in release and 502 ms under ASan/UBSan; filtering its active 1,000-row
listing built exactly 1,000 keys and one row index in both presets. Mutations
that disabled same-branch reuse or tripled filter work failed their dedicated
gates.

The warning-clean release build passed all 50 executed checks, including the
189.62-second static-analysis gate; two RHI probes skipped because the offscreen
platform supplied no usable RHI context. ASan/UBSan passed all 49 executed
checks, with static analysis disabled and the same two RHI probes skipped.
Release and sanitizer binaries completed silent eight-second software smoke
launches. The changed shell interaction ran at the test environment's default
1x logical scale; it adds no pixel or frame-comparison claim.

## 2026-08-09 -- Unclaim the columns view pending action and location wiring

The roadmap claimed the Miller columns view. Two gaps make that claim
premature and it is withdrawn until both close.

Entry actions resolve their target through the list model, not through the
view that holds focus. With the columns view focused and holding its own
selection, a delete, copy, move, or rename acts on the list model's selected
rows in a different directory, and nothing on screen indicates the mismatch.
A filter applied while a chain is open also narrows ancestor columns, so the
row anchoring a live column can leave the listing while the chain stays open.

The columns view also does not drive the shell's location: its current path
and activation signal have no consumers, so the path navigator and the rest
of the shell stay where they were while the chain moves.

The view itself is unchanged and remains reachable. Its own navigation,
accessibility declarations, virtualization, and retained-state bounds hold as
recorded.

#!/usr/bin/env bash

# Holds a build directory to the configuration its preset pins.
#
# WHY THIS EXISTS. `cmake --preset release` and `cmake -S . -B build/release`
# look interchangeable and are not. With a cache already present the bare form
# is harmless, because CMake reuses what the cache holds. Once the directory is
# wiped there is no cache to reuse, and the bare form silently takes the
# system defaults instead: whatever `c++` resolves to, with every option the
# preset would have set left at its default. The standing practice of wiping
# both build directories whenever new entries register is exactly what makes
# that reachable, and it is reachable on the runs whose results are quoted.
#
# The loud direction is not the problem. On this class of machine a release
# tree built with the wrong compiler fails outright, which is a result nobody
# can misread. The quiet direction is: a wiped `build/asan` reconfigured bare
# has ODYSEA_ASAN off, so the whole battery runs green with no sanitizer in it
# and reports as an AddressSanitizer pass. Nothing in that run is false except
# what it is called.
#
# WHAT IS CHECKED. Two things, and they fail in different circumstances on
# purpose.
#
#   1. The corpus floor, checked from any build directory: every configure
#      preset the project ships must pin the compiler and the build type, and
#      at least one must pin the sanitizer on. This is what stops the check
#      below from being satisfiable by deleting the pin it compares against.
#
#   2. The directory itself: when the build directory is the one a preset
#      names, every cache variable that preset resolves to must be present in
#      the cache with that value. The compiler is compared as a resolved path,
#      since a preset names a program and a cache records where it was found.
#
# A build directory no preset names is a failure here, not a pass and not a
# skip. This project's gates do not declare a skip return code: each one either
# resolves what it checks and runs, or states a precondition and fails. The
# precondition this gate needs is a directory whose intended configuration is
# written down, and results from a directory without one do not mean what they
# say -- which is the entire finding this gate exists for. Configuring
# elsewhere on purpose stays possible; running this gate against the result and
# calling it verified does not.

set -euo pipefail

readonly gate_name=build_configuration_guard

readonly build_directory="${1:-}"
if [[ -z "$build_directory" ]]; then
    printf '%s: expected the build directory to check\n' "$gate_name" >&2
    exit 2
fi

readonly script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly source_directory="$(cd "$script_directory/.." && pwd)"
readonly presets_file="$source_directory/CMakePresets.json"

if [[ ! -f "$presets_file" ]]; then
    printf '%s: no CMakePresets.json at %s\n' "$gate_name" "$presets_file" >&2
    printf '%s: the pinned configuration is the thing this gate compares against\n' "$gate_name" >&2
    exit 1
fi

readonly cache_file="$build_directory/CMakeCache.txt"
if [[ ! -f "$cache_file" ]]; then
    printf '%s: no CMakeCache.txt under %s\n' "$gate_name" "$build_directory" >&2
    printf '%s: configure the tree before running the gate\n' "$gate_name" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    printf '%s: python3 is required to read CMakePresets.json and is not installed\n' \
        "$gate_name" >&2
    exit 1
fi

# The comparison is done in Python because CMakePresets.json is JSON with
# inheritance, and resolving `inherits` correctly matters more here than
# avoiding an interpreter: a half-resolved preset would compare against the
# wrong pin and still print a pass.
#
# Exit status is the gate's own: 0 pass, 1 fail. There is no third answer.
python3 - "$presets_file" "$cache_file" "$source_directory" "$build_directory" "$gate_name" <<'PYTHON'
import json
import os
import shutil
import sys

presets_path, cache_path, source_directory, build_directory, gate = sys.argv[1:6]

FAIL = 1

# Cache variables every shipped configure preset must pin. These are the two
# the project's published results are stated in terms of: which compiler
# produced the warning-clean build, and which configuration it was.
REQUIRED_PINS = ("CMAKE_CXX_COMPILER", "CMAKE_BUILD_TYPE")
# The sanitizer claim rests on exactly one preset turning it on. Without this
# floor, a preset set could satisfy everything above while shipping no
# sanitizer build at all, and the battery that reports itself as an
# AddressSanitizer run would be a plain debug run.
SANITIZER_PIN = ("ODYSEA_ASAN", "ON")


def fail(*lines):
    for line in lines:
        print(f"{gate}: {line}", file=sys.stderr)
    sys.exit(FAIL)


try:
    with open(presets_path, encoding="utf-8") as handle:
        presets_document = json.load(handle)
except (OSError, ValueError) as error:
    fail(f"CMakePresets.json could not be read as JSON: {error}")

configure_presets = presets_document.get("configurePresets")
if not isinstance(configure_presets, list) or not configure_presets:
    fail("CMakePresets.json defines no configure presets, so nothing pins the toolchain")

by_name = {}
for preset in configure_presets:
    if isinstance(preset, dict) and isinstance(preset.get("name"), str):
        by_name[preset["name"]] = preset


def resolve(name, seen=None):
    """A preset's own fields merged over everything it inherits, nearest first.

    `inherits` may name one preset or several, and CMake applies them left to
    right with earlier entries winning. Resolving this rather than reading a
    preset's own cacheVariables is the difference between comparing against
    the real pin and comparing against nothing: every pin in this project's
    file lives on the hidden base preset, so an unresolved read finds no
    compiler pinned anywhere and passes everything.
    """
    seen = seen or set()
    if name in seen or name not in by_name:
        return {}
    seen.add(name)
    preset = by_name[name]
    merged = {}
    parents = preset.get("inherits") or []
    if isinstance(parents, str):
        parents = [parents]
    # Later parents lose to earlier ones, so they are applied in reverse.
    for parent in reversed(parents):
        merged.update(resolve(parent, seen))
    own = preset.get("cacheVariables")
    if isinstance(own, dict):
        for key, value in own.items():
            # A cache variable may be written as a bare value or as
            # {"type": ..., "value": ...}; both mean the same pin.
            if isinstance(value, dict):
                value = value.get("value")
            if value is None:
                continue
            merged[key] = str(value)
    return merged


def _parents_of(name):
    parents = by_name.get(name, {}).get("inherits") or []
    return [parents] if isinstance(parents, str) else parents


def _binary_directory_template(name, seen=None):
    """The binaryDir a preset uses, which is usually not its own.

    Every preset in this project inherits binaryDir from a hidden base, and the
    template there contains ${presetName}. Expanding it while walking up the
    chain substitutes the PARENT's name, which resolves `release` to
    `build/base` -- a directory that exists for no preset, so every real build
    directory reads as belonging to none and the gate declines instead of
    checking. The template is therefore returned unexpanded and substituted
    once, against the preset actually being resolved.
    """
    seen = seen or set()
    if name in seen or name not in by_name:
        return None
    seen.add(name)
    own = by_name[name].get("binaryDir")
    if isinstance(own, str) and own:
        return own
    for parent in _parents_of(name):
        inherited = _binary_directory_template(parent, seen)
        if inherited:
            return inherited
    return None


def binary_directory_of(name):
    template = _binary_directory_template(name)
    if not template:
        return None
    return (
        template.replace("${sourceDir}", source_directory)
        .replace("${presetName}", name)
    )


visible = [
    name
    for name, preset in by_name.items()
    if not preset.get("hidden")
]
if not visible:
    fail("every configure preset is hidden, so no preset can be configured or compared")

# --- 1. The corpus floor ----------------------------------------------------
# Checked from whichever build directory the gate runs in, so removing a pin
# fails everywhere rather than only in the tree that happens to use it.
floor_problems = []
for name in sorted(visible):
    pinned = resolve(name)
    for required in REQUIRED_PINS:
        if required not in pinned or not pinned[required].strip():
            floor_problems.append(
                f"configure preset '{name}' pins no {required}, so a build made with it "
                f"is whatever the machine defaults to"
            )

sanitizer_key, sanitizer_value = SANITIZER_PIN
sanitizer_presets = [
    name for name in visible if resolve(name).get(sanitizer_key) == sanitizer_value
]
if not sanitizer_presets:
    floor_problems.append(
        f"no configure preset pins {sanitizer_key}={sanitizer_value}, so no shipped preset "
        f"produces the sanitizer build the battery reports as one"
    )

if floor_problems:
    fail(*floor_problems)

# --- 2. This build directory ------------------------------------------------
def same_directory(left, right):
    try:
        return os.path.samefile(left, right)
    except OSError:
        return False


owning_preset = None
for name in sorted(visible):
    candidate = binary_directory_of(name)
    if candidate and same_directory(candidate, build_directory):
        owning_preset = name
        break

if owning_preset is None:
    fail(
        f"{build_directory} is not the build directory of any configure preset, so nothing "
        f"records what it was meant to be configured with",
        "configure through a preset (cmake --preset release, cmake --preset asan) before "
        "quoting results from a directory",
        f"the preset floor itself passed: {len(visible)} preset(s), sanitizer pinned by "
        f"{', '.join(sorted(sanitizer_presets))}",
    )

cache = {}
try:
    with open(cache_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("//"):
                continue
            name_and_type, separator, value = line.partition("=")
            if not separator:
                continue
            key = name_and_type.split(":", 1)[0]
            cache[key] = value
except OSError as error:
    fail(f"CMakeCache.txt could not be read: {error}")

expected = resolve(owning_preset)
if not expected:
    fail(
        f"configure preset '{owning_preset}' resolves to no pinned cache variables, so this "
        f"directory would be compared against nothing"
    )


def as_program(value):
    """A compiler pin and a cache entry compared as the same program.

    A preset names a program (`clang++`); a cache records where it was found
    (`/usr/bin/clang++`). Comparing the two as text reports a difference that
    is not one, so both sides are located on PATH and resolved through any
    symbolic links first.

    Where that leaves the system default is worth stating rather than
    assuming. On the machine this was written against, `/usr/bin/c++` is a
    separate file from `/usr/bin/g++` -- not a link to it, and not the same
    inode -- so a directory configured bare is reported as differing from a
    `g++` pin as well as from a `clang++` one. Somewhere that default is a
    link to the pinned compiler, this comparison would accept it, which is the
    honest answer: the build really was produced by the pinned program.
    """
    located = shutil.which(value) or value
    return os.path.realpath(located)


problems = []
for key in sorted(expected):
    wanted = expected[key]
    if key not in cache:
        problems.append(
            f"{key} is pinned to '{wanted}' by preset '{owning_preset}' and is absent from "
            f"the cache; this directory was configured without the preset"
        )
        continue
    found = cache[key]
    if key == "CMAKE_CXX_COMPILER":
        if as_program(found) != as_program(wanted):
            problems.append(
                f"{key} is pinned to '{wanted}' ({as_program(wanted)}) by preset "
                f"'{owning_preset}' and this directory was configured with '{found}' "
                f"({as_program(found)})"
            )
        continue
    if found != wanted:
        problems.append(
            f"{key} is pinned to '{wanted}' by preset '{owning_preset}' and this directory "
            f"holds '{found}'"
        )

if problems:
    fail(
        *problems,
        f"reconfigure with: cmake --preset {owning_preset}",
        "results from this directory describe a build the project does not ship",
    )

print(
    f"{gate}: {build_directory} matches preset '{owning_preset}': "
    f"{len(expected)} pinned cache variable(s) verified"
)
print(
    f"{gate}: preset floor passed for {len(visible)} preset(s); "
    f"{sanitizer_key}={sanitizer_value} pinned by {', '.join(sorted(sanitizer_presets))}"
)
PYTHON

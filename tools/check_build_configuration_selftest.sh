#!/usr/bin/env bash

# Proves tools/check_build_configuration.sh by consequence, against fixture
# trees rather than against this repository.
#
# A fixture is a directory holding a CMakePresets.json and one or more
# directories holding a CMakeCache.txt. That is the entire input the gate reads,
# so a fixture can present any configuration a real tree could -- including the
# ones that matter here, which are the configurations a wiped-and-bare-
# configured directory produces. No CMake runs and nothing is built: this gate
# reads two files, and a self-test that configured real trees would take
# minutes to measure the same thing.
#
# The gate script is copied into each fixture, because it locates
# CMakePresets.json relative to its own directory. Running the repository's
# copy against a fixture would compare fixture caches to the repository's
# presets, which is a different question and would make several scenarios below
# pass for the wrong reason.

set -euo pipefail

readonly script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly gate="$script_directory/check_build_configuration.sh"

if [[ ! -f "$gate" ]]; then
    echo "build_configuration_guard_self_test: the gate is missing at $gate" >&2
    exit 1
fi

# Stated as a precondition rather than skipped: a self-test that declined to
# run would report nothing while reading as a pass in the battery summary.
if ! command -v python3 >/dev/null 2>&1; then
    echo "build_configuration_guard_self_test: python3 is required and is not installed" >&2
    exit 1
fi

readonly sandbox="$(mktemp -d)"
trap 'rm -rf -- "$sandbox"' EXIT

# How many scenarios must report a result. A suite that runs part of itself
# prints nothing but passes and reads as a clean run; bounding the reported
# count below is what makes that impossible to mistake for one.
readonly expected_scenario_count=12

failures=0
reported=0
report() {
    local outcome="$1" scenario="$2"
    printf '%-5s %s\n' "$outcome" "$scenario"
    reported=$((reported + 1))
    [[ "$outcome" == PASS ]] || failures=$((failures + 1))
}

# --- Fixture construction ---------------------------------------------------
# The shipped preset shape: a hidden base carrying every pin, two visible
# presets inheriting it, and the sanitizer preset overriding the compiler. The
# inheritance is not incidental -- a gate that reads a preset's own
# cacheVariables finds nothing pinned in this shape and passes everything.
default_presets() {
    cat <<'JSON'
{
    "version": 6,
    "configurePresets": [
        {
            "name": "base",
            "hidden": true,
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/${presetName}",
            "cacheVariables": {
                "CMAKE_CXX_COMPILER": "fixture-clang",
                "CMAKE_PREFIX_PATH": "/fixture/qt"
            }
        },
        {
            "name": "release",
            "inherits": "base",
            "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
        },
        {
            "name": "asan",
            "inherits": "base",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "ODYSEA_ASAN": "ON",
                "CMAKE_CXX_COMPILER": "fixture-gcc"
            }
        }
    ]
}
JSON
}

# Two stand-in compilers on PATH, so the resolved-path comparison has real
# programs to resolve. They are never executed.
readonly fixture_bin="$sandbox/bin"
mkdir -p "$fixture_bin"
printf '#!/bin/sh\nexit 0\n' >"$fixture_bin/fixture-clang"
printf '#!/bin/sh\nexit 0\n' >"$fixture_bin/fixture-gcc"
printf '#!/bin/sh\nexit 0\n' >"$fixture_bin/fixture-default"
chmod +x "$fixture_bin/fixture-clang" "$fixture_bin/fixture-gcc" "$fixture_bin/fixture-default"
export PATH="$fixture_bin:$PATH"

# Creates a fixture tree and echoes its root.
new_fixture() {
    # Declared and assigned separately: bash makes every name in one `local`
    # local before assigning any of them, so referring to an earlier one in the
    # same statement reads it as unset under `set -u`.
    local name="$1"
    local root="$sandbox/$name"
    mkdir -p "$root/tools"
    cp "$gate" "$root/tools/check_build_configuration.sh"
    default_presets >"$root/CMakePresets.json"
    printf '%s' "$root"
}

# Writes a CMakeCache.txt holding exactly the given NAME:TYPE=VALUE lines.
write_cache() {
    local directory="$1"
    shift
    mkdir -p "$directory"
    {
        echo "# This is the CMakeCache file."
        echo "//A comment line the reader must not mistake for an entry"
        local entry
        for entry in "$@"; do
            printf '%s\n' "$entry"
        done
    } >"$directory/CMakeCache.txt"
}

# The cache a correct `cmake --preset release` produces, plus the unrelated
# entries a real cache is full of.
write_release_cache() {
    write_cache "$1" \
        "CMAKE_BUILD_TYPE:STRING=Release" \
        "CMAKE_CXX_COMPILER:FILEPATH=$fixture_bin/fixture-clang" \
        "CMAKE_PREFIX_PATH:PATH=/fixture/qt" \
        "CMAKE_HOME_DIRECTORY:INTERNAL=/somewhere"
}

# The cache a bare `cmake -S . -B build/asan` produces on a wiped directory:
# the system default compiler, no sanitizer, and none of the preset's options.
write_bare_cache() {
    write_cache "$1" \
        "CMAKE_BUILD_TYPE:STRING=" \
        "CMAKE_CXX_COMPILER:FILEPATH=$fixture_bin/fixture-default" \
        "CMAKE_HOME_DIRECTORY:INTERNAL=/somewhere"
}

run_gate() {
    local root="$1" build="$2"
    bash "$root/tools/check_build_configuration.sh" "$build" >"$sandbox/out.txt" 2>&1
}

gate_status() {
    local status=0
    run_gate "$@" || status=$?
    printf '%s' "$status"
}

output_has() { grep -q -- "$1" "$sandbox/out.txt"; }

# --- Scenario 1: a correctly configured directory passes --------------------
scenario_preset_configured_passes() {
    local root
    root="$(new_fixture correct)"
    write_release_cache "$root/build/release"
    local status
    status="$(gate_status "$root" "$root/build/release")"
    if [[ "$status" != 0 ]]; then
        report FAIL "a directory configured by its preset should pass (got $status): $(head -2 "$sandbox/out.txt" | tr '\n' ' ')"
        return
    fi
    if ! output_has "matches preset 'release'"; then
        report FAIL "the passing run did not name the preset it checked against"
        return
    fi
    report PASS "a directory configured by its preset passes and names the preset"
}

# --- Scenario 2: the pin lives on an inherited preset -----------------------
# The compiler pin is on the hidden base, not on `release`. A gate that reads a
# preset's own cacheVariables sees nothing to check here and passes a directory
# built with the wrong compiler.
scenario_inherited_pin_is_enforced() {
    local root
    root="$(new_fixture inherited)"
    write_cache "$root/build/release" \
        "CMAKE_BUILD_TYPE:STRING=Release" \
        "CMAKE_CXX_COMPILER:FILEPATH=$fixture_bin/fixture-default" \
        "CMAKE_PREFIX_PATH:PATH=/fixture/qt"
    local status
    status="$(gate_status "$root" "$root/build/release")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "a compiler pinned only on an inherited preset was not enforced (got $status)"
        ok=0
    elif ! output_has "CMAKE_CXX_COMPILER is pinned to 'fixture-clang'"; then
        report FAIL "the refusal did not name the inherited compiler pin"
        ok=0
    fi
    ((ok)) && report PASS "a pin inherited from a hidden preset is enforced and named"
}

# --- Scenario 3: the wiped-and-bare release directory -----------------------
scenario_bare_release_is_refused() {
    local root
    root="$(new_fixture bare_release)"
    write_bare_cache "$root/build/release"
    local status
    status="$(gate_status "$root" "$root/build/release")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "a bare-configured release directory should be refused (got $status)"
        ok=0
    elif ! output_has "cmake --preset release"; then
        report FAIL "the refusal did not say how to reconfigure the directory"
        ok=0
    fi
    ((ok)) && report PASS "a wiped release directory configured without the preset is refused by name"
}

# --- Scenario 4: the quiet one -- a sanitizer build with no sanitizer -------
# This is the case the whole gate exists for. A bare-configured build/asan
# builds and tests cleanly; every result is true except the name of the run.
scenario_bare_asan_loses_the_sanitizer() {
    local root
    root="$(new_fixture bare_asan)"
    write_bare_cache "$root/build/asan"
    local status
    status="$(gate_status "$root" "$root/build/asan")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "a sanitizer directory with no sanitizer in it should be refused (got $status)"
        ok=0
    elif ! output_has "ODYSEA_ASAN is pinned to 'ON'"; then
        report FAIL "the refusal did not name the missing sanitizer setting"
        ok=0
    fi
    ((ok)) && report PASS "a wiped sanitizer directory configured without the preset is refused, by the sanitizer setting"
}

# --- Scenario 5: a single wrong value, everything else correct ---------------
# The bare cases differ from the pin in several ways at once, so any one check
# firing would satisfy them. This one differs in exactly one.
scenario_single_wrong_value_is_caught() {
    local root
    root="$(new_fixture single_difference)"
    write_cache "$root/build/asan" \
        "CMAKE_BUILD_TYPE:STRING=Debug" \
        "CMAKE_CXX_COMPILER:FILEPATH=$fixture_bin/fixture-gcc" \
        "CMAKE_PREFIX_PATH:PATH=/fixture/qt" \
        "ODYSEA_ASAN:BOOL=OFF"
    local status
    status="$(gate_status "$root" "$root/build/asan")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "one pinned value differing should be enough to refuse (got $status)"
        ok=0
    elif ! output_has "ODYSEA_ASAN is pinned to 'ON' by preset 'asan' and this directory holds 'OFF'"; then
        report FAIL "the refusal did not name the one value that differed"
        ok=0
    fi
    ((ok)) && report PASS "a directory differing in exactly one pinned value is refused, naming it"
}

# --- Scenario 6: the compiler is compared as a program, not as text ---------
# A preset names a program and a cache records where it was found. Reporting
# that as a difference would make the gate cry wolf on every correct build,
# which is how a gate gets deleted.
scenario_compiler_path_spelling_is_not_a_difference() {
    local root
    root="$(new_fixture spelling)"
    local link_directory="$sandbox/linked"
    mkdir -p "$link_directory"
    ln -sfn "$fixture_bin/fixture-clang" "$link_directory/fixture-clang"
    write_cache "$root/build/release" \
        "CMAKE_BUILD_TYPE:STRING=Release" \
        "CMAKE_CXX_COMPILER:FILEPATH=$link_directory/fixture-clang" \
        "CMAKE_PREFIX_PATH:PATH=/fixture/qt"
    local status
    status="$(gate_status "$root" "$root/build/release")"
    if [[ "$status" != 0 ]]; then
        report FAIL "a compiler reached through a link to the pinned program should pass (got $status): $(head -1 "$sandbox/out.txt")"
        return
    fi
    report PASS "a cache naming the pinned program through a link is not reported as a difference"
}

# --- Scenario 7: a directory no preset owns is a failure, not a pass --------
# The cache in this fixture is a correct one, so nothing about the
# configuration itself is wrong. What is missing is any record of what the
# directory was meant to be -- and a pass here would be the same vacuity the
# gate exists to remove, reachable by configuring one directory to the side.
scenario_unowned_directory_fails() {
    local root
    root="$(new_fixture unowned)"
    write_release_cache "$root/elsewhere"
    local status
    status="$(gate_status "$root" "$root/elsewhere")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "a directory outside every preset should fail, not pass (got $status)"
        ok=0
    elif ! output_has "is not the build directory of any configure preset"; then
        report FAIL "the refusal did not say why the directory could not be checked"
        ok=0
    fi
    if grep -q "DECL -- declined:" "$sandbox/out.txt"; then
        report FAIL "the gate declared a skip, which this project's gates do not do"
        ok=0
    fi
    ((ok)) && report PASS "a build directory no preset names fails by name rather than passing or skipping"
}

# --- Scenario 8: deleting the pin must not satisfy the comparison ------------
# Without a floor, the cheapest way to make this gate green is to remove the
# thing it compares against.
scenario_removed_compiler_pin_fails() {
    local root
    root="$(new_fixture no_compiler_pin)"
    python3 - "$root/CMakePresets.json" <<'PY'
import json, sys
path = sys.argv[1]
document = json.load(open(path))
for preset in document["configurePresets"]:
    preset.get("cacheVariables", {}).pop("CMAKE_CXX_COMPILER", None)
json.dump(document, open(path, "w"), indent=4)
PY
    write_release_cache "$root/build/release"
    local status
    status="$(gate_status "$root" "$root/build/release")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "a preset set that pins no compiler should fail (got $status)"
        ok=0
    elif ! output_has "pins no CMAKE_CXX_COMPILER"; then
        report FAIL "the refusal did not name the missing pin"
        ok=0
    fi
    ((ok)) && report PASS "removing the compiler pin fails by name instead of making the gate vacuous"
}

# --- Scenario 9: the sanitizer preset cannot quietly stop being one ----------
scenario_sanitizer_pin_floor() {
    local root
    root="$(new_fixture no_sanitizer_pin)"
    python3 - "$root/CMakePresets.json" <<'PY'
import json, sys
path = sys.argv[1]
document = json.load(open(path))
for preset in document["configurePresets"]:
    preset.get("cacheVariables", {}).pop("ODYSEA_ASAN", None)
json.dump(document, open(path, "w"), indent=4)
PY
    write_release_cache "$root/build/release"
    local status
    status="$(gate_status "$root" "$root/build/release")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "a preset set with no sanitizer preset should fail (got $status)"
        ok=0
    elif ! output_has "no configure preset pins ODYSEA_ASAN=ON"; then
        report FAIL "the refusal did not name the missing sanitizer preset"
        ok=0
    fi
    ((ok)) && report PASS "a preset set that ships no sanitizer build fails by name"
}

# --- Scenario 10: the floor is checked before the directory is identified ----
# The floor and the directory check fail in different circumstances. Checking
# the floor only once a preset has been matched would make a missing pin
# invisible from any directory the gate cannot place -- and the message would
# then be about the directory rather than about the pin that is gone.
scenario_floor_applies_before_directory_identity() {
    local root
    root="$(new_fixture floor_from_elsewhere)"
    python3 - "$root/CMakePresets.json" <<'PY'
import json, sys
path = sys.argv[1]
document = json.load(open(path))
for preset in document["configurePresets"]:
    preset.get("cacheVariables", {}).pop("CMAKE_BUILD_TYPE", None)
json.dump(document, open(path, "w"), indent=4)
PY
    write_release_cache "$root/elsewhere"
    local status
    status="$(gate_status "$root" "$root/elsewhere")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "a missing build-type pin should fail even from a directory no preset names (got $status)"
        ok=0
    elif ! output_has "pins no CMAKE_BUILD_TYPE"; then
        report FAIL "the refusal did not name the missing build-type pin"
        ok=0
    fi
    ((ok)) && report PASS "the preset floor is reported as the pin that is missing, not as the directory that cannot be placed"
}

# --- Scenario 11: an unconfigured directory is a failure, not a pass ---------
scenario_missing_cache_fails() {
    local root
    root="$(new_fixture missing_cache)"
    mkdir -p "$root/build/release"
    local status
    status="$(gate_status "$root" "$root/build/release")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "a directory with no cache should fail (got $status)"
        ok=0
    elif ! output_has "no CMakeCache.txt"; then
        report FAIL "the refusal did not name the missing cache"
        ok=0
    fi
    ((ok)) && report PASS "a build directory with no cache fails by name"
}

# --- Scenario 12: a preset file that cannot be read is a failure ------------
scenario_unreadable_presets_fail() {
    local root
    root="$(new_fixture broken_presets)"
    printf 'not json at all\n' >"$root/CMakePresets.json"
    write_release_cache "$root/build/release"
    local status
    status="$(gate_status "$root" "$root/build/release")"
    local ok=1
    if [[ "$status" != 1 ]]; then
        report FAIL "an unreadable preset file should fail (got $status)"
        ok=0
    elif ! output_has "could not be read as JSON"; then
        report FAIL "the refusal did not say the preset file could not be read"
        ok=0
    fi
    ((ok)) && report PASS "a preset file that cannot be read fails by name instead of checking nothing"
}

scenario_preset_configured_passes
scenario_inherited_pin_is_enforced
scenario_bare_release_is_refused
scenario_bare_asan_loses_the_sanitizer
scenario_single_wrong_value_is_caught
scenario_compiler_path_spelling_is_not_a_difference
scenario_unowned_directory_fails
scenario_removed_compiler_pin_fails
scenario_sanitizer_pin_floor
scenario_floor_applies_before_directory_identity
scenario_missing_cache_fails
scenario_unreadable_presets_fail

if ((failures == 0)); then
    # Only meaningful on the passing path: a failing scenario reports each
    # broken expectation separately, so the count can exceed the number of
    # scenarios and the suite fails on its own terms anyway.
    if ((reported != expected_scenario_count)); then
        echo "build_configuration_guard_self_test: $reported scenario(s) reported a result, expected $expected_scenario_count; the suite did not run in full" >&2
        exit 1
    fi
    echo "build_configuration_guard_self_test: all $reported scenarios passed"
    exit 0
fi

echo "build_configuration_guard_self_test: $failures scenario(s) failed" >&2
exit 1

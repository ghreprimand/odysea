cmake_minimum_required(VERSION 3.28)

project(odysea
    VERSION 0.0.1
    DESCRIPTION "A fast, keyboard-centric file manager for modern Linux desktops."
    LANGUAGES CXX)

# --- Language standard -------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clang-tidy / editor tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Project-wide warning policy ---------------------------------------------
# Warnings are errors: the compiler is the first line of the memory-safety net.
add_library(odysea_warnings INTERFACE)
target_compile_options(odysea_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Werror
    -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
    -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
    -Wdouble-promotion)

# --- Optional AddressSanitizer -----------------------------------------------
# Enabled via the "asan" CMake preset. Detects use-after-free, buffer overflow,
# and leaks at runtime with an exact stack trace.
option(ODYSEA_ASAN "Build with AddressSanitizer + UBSan" OFF)
add_library(odysea_sanitizers INTERFACE)
if(ODYSEA_ASAN)
    target_compile_options(odysea_sanitizers INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(odysea_sanitizers INTERFACE
        -fsanitize=address,undefined)
endif()

# Core scanning runs off the UI thread.
find_package(Threads REQUIRED)

enable_testing()

add_test(
    NAME public_repository_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_public_repo.sh)
add_test(
    NAME public_repository_guard_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_public_repo_selftest.sh)
add_test(
    NAME attribution_hooks_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_hooks_selftest.sh)
add_test(
    NAME formatting_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_format.sh)
add_test(
    NAME formatting_guard_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_format_selftest.sh)
add_test(
    NAME qml_formatting_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml.sh format)
add_test(
    NAME qml_lint_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml.sh lint ${CMAKE_BINARY_DIR}/app)
add_test(
    NAME qml_lint_guard_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml_lint_selftest.sh)
add_test(
    NAME qml_lint_order_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml_lint_order_selftest.sh)
add_test(
    NAME qml_module_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml_module.sh
        ${CMAKE_BINARY_DIR}/app/OdySea)
add_test(
    NAME qml_module_guard_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml_module_selftest.sh)
add_test(
    NAME qml_test_scopes
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml_test_scopes.sh)
add_test(
    NAME qml_test_scopes_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml_test_scopes_selftest.sh)
# Companion to qml_test_scopes: that gate checks which directory a runner
# scans, this one checks which functions a filtered entry actually runs. A
# function added to a filtered TestCase and left off the list executes nowhere
# on that path, and a listed function that no longer exists matches nothing;
# both leave the entry green in the time it always took.
add_test(
    NAME rhi_function_filter
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_rhi_function_filter.sh)
add_test(
    NAME rhi_function_filter_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_rhi_function_filter_selftest.sh)
add_test(
    NAME file_length_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_file_length.sh)
add_test(
    NAME file_length_guard_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_file_length_selftest.sh)
add_test(
    NAME devlog_archive_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_devlog_archive.sh)
add_test(
    NAME devlog_archive_guard_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_devlog_archive_selftest.sh)
add_test(
    NAME key_construction_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_key_construction.sh)
add_test(
    NAME key_construction_guard_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_key_construction_selftest.sh)
add_test(
    NAME static_analysis
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_clang_tidy.sh ${CMAKE_BINARY_DIR})
add_test(
    NAME static_analysis_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_clang_tidy_selftest.sh)
add_test(
    NAME guard_corpus_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_guard_corpus_selftest.sh)
# Proves the isolated-compositor harness (tools/isolated_compositor_gate.sh)
# that legitimately sets ODYSEA_ISOLATED_COMPOSITOR: the cross-run lock, the
# teardown trap over success, failure, SIGINT and SIGTERM, and the reaper that
# bounds what an untrappable SIGKILL leaves behind. It stands a stub in for the
# compositor and starts none, so it needs no display and touches no session.
add_test(
    NAME compositor_gate_harness_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/isolated_compositor_gate_selftest.sh)
set_tests_properties(compositor_gate_harness_self_test PROPERTIES TIMEOUT 180)

# The smoke gate and the coverage reconciler each ship with a self-test, so a
# gate that stopped biting shows up as its own red entry. The smoke self-test
# needs nothing beyond coreutils and never declines.
add_test(
    NAME application_smoke_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_smoke_selftest.sh)
add_test(
    NAME battery_coverage_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_battery_coverage_selftest.sh)

# No guard declares a skip return code. Each one either resolves its corpus and
# runs, or states a precondition and fails; none of them decline quietly. A
# skipped test is reported outside every summary line CTest prints, so a gate
# that skips is a gate whose absence has to be noticed by hand, and the whole
# set of them was skipping in any tree without Git metadata. The sole exception
# is the coverage reconciler's self-test: it parses JUnit with python3, so when
# python3 is absent it declines by name rather than measuring nothing. That is a
# genuine toolchain precondition, not a corpus it could have resolved, so it
# alone carries a skip return code.
set_tests_properties(battery_coverage_self_test PROPERTIES SKIP_RETURN_CODE 77)
if(ODYSEA_ASAN)
    set_tests_properties(static_analysis PROPERTIES DISABLED TRUE)
endif()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)

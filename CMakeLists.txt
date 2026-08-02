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
    NAME qml_module_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml_module.sh
        ${CMAKE_BINARY_DIR}/app/OdySea)
add_test(
    NAME qml_module_guard_self_test
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_qml_module_selftest.sh)
add_test(
    NAME file_length_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_file_length.sh)
add_test(
    NAME static_analysis
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_clang_tidy.sh ${CMAKE_BINARY_DIR})

set_tests_properties(
    public_repository_guard attribution_hooks_self_test formatting_guard
    formatting_guard_self_test qml_formatting_guard qml_lint_guard
    qml_module_guard qml_module_guard_self_test file_length_guard
    static_analysis
    PROPERTIES SKIP_RETURN_CODE 77)
if(ODYSEA_ASAN)
    set_tests_properties(static_analysis PROPERTIES DISABLED TRUE)
endif()

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)

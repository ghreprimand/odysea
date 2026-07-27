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

enable_testing()

add_test(
    NAME public_repository_guard
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_public_repo.sh)

add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(tests)

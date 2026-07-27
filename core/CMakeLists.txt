# odysea_core: the toolkit-agnostic filesystem model.
#
# This library has NO Qt or GUI dependency. It must stay unit-testable without a
# display server so the core logic can be verified headless and reused by any
# presentation layer.
add_library(odysea_core
    src/directory_model.cpp
    src/directory_watcher.cpp
    src/file_operations.cpp
    src/trash.cpp)

target_include_directories(odysea_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(odysea_core
    PUBLIC Threads::Threads
    PRIVATE odysea_warnings odysea_sanitizers)

add_library(odysea::core ALIAS odysea_core)

# odysea_core: the toolkit-agnostic filesystem model.
#
# This library has NO Qt or GUI dependency. It must stay unit-testable without a
# display server so the core logic can be verified headless and reused by any
# presentation layer.
add_library(odysea_core
    src/appearance.cpp
    src/digest.cpp
    src/directory_model.cpp
    src/directory_scanner.cpp
    src/directory_watcher.cpp
    src/entry_metadata.cpp
    src/file_operations.cpp
    src/fuzzy_find.cpp
    src/operation_journal.cpp
    src/storage_usage.cpp
    src/thumbnail.cpp
    src/thumbnail_service.cpp
    src/transfer.cpp
    src/transfer_engine.cpp
    src/trash.cpp)

target_include_directories(odysea_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The freedesktop thumbnail standard namespaces recorded failures by
# application and version, so a later version retries what an earlier one
# refused. The build is the only place that knows the version.
target_compile_definitions(odysea_core
    PRIVATE ODYSEA_THUMBNAIL_NAMESPACE="odysea-${PROJECT_VERSION}")

target_link_libraries(odysea_core
    PUBLIC Threads::Threads
    PRIVATE odysea_warnings odysea_sanitizers)

add_library(odysea::core ALIAS odysea_core)

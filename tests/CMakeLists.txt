# Core tests. Pure C++, no Qt: the filesystem model must be verifiable headless.
# Built under the "asan" preset these run with AddressSanitizer + UBSan active,
# so memory errors surface here with an exact stack trace.
function(odysea_add_core_test test_name source_file)
    add_executable(${test_name} ${source_file})

    # The core source directory is on the include path so tests can reach the
    # internal seams that make otherwise environment-dependent failure paths,
    # such as a move across filesystems, deterministic.
    target_include_directories(${test_name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/core/src)

    target_link_libraries(${test_name}
        PRIVATE odysea::core odysea_warnings odysea_sanitizers)

    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

odysea_add_core_test(core_appearance test_appearance.cpp)
odysea_add_core_test(core_directory_model test_directory_model.cpp)
odysea_add_core_test(core_file_operations test_file_operations.cpp)
odysea_add_core_test(core_trash test_trash.cpp)
odysea_add_core_test(core_directory_watcher test_directory_watcher.cpp)
odysea_add_core_test(core_directory_scanner test_directory_scanner.cpp)
odysea_add_core_test(core_thumbnail test_thumbnail.cpp)
odysea_add_core_test(core_thumbnail_service test_thumbnail_service.cpp)

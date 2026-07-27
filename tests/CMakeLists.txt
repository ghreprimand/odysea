# Core tests. Pure C++, no Qt: the filesystem model must be verifiable headless.
# Built under the "asan" preset these run with AddressSanitizer + UBSan active,
# so memory errors surface here with an exact stack trace.
add_executable(odysea_core_tests
    test_directory_model.cpp)

target_link_libraries(odysea_core_tests
    PRIVATE odysea::core odysea_warnings odysea_sanitizers)

add_test(NAME core_directory_model COMMAND odysea_core_tests)

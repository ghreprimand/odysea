# odysea: the Qt Quick application shell.
#
# The shell is GPU-rendered through Qt Quick's scene graph. It consumes the
# toolkit-agnostic odysea::core library for all filesystem work, keeping the
# performance-critical model free of framework overhead.
find_package(Qt6 6.6 REQUIRED COMPONENTS Concurrent Core Gui Qml Quick QuickControls2 QuickTest Test)

qt_standard_project_setup(REQUIRES 6.6)
qt_policy(SET QTP0004 NEW)

qt_add_executable(odysea
    src/main.cpp)

qt_add_library(odysea_app_adapter STATIC
    src/directory_list_model.cpp
    src/directory_list_model_async.cpp
    src/directory_list_model_operations.cpp
    src/directory_list_model_thumbnails.cpp
    src/directory_list_model.hpp
    src/entry_launcher.cpp
    src/entry_launcher.hpp
    src/directory_watch_service.cpp
    src/directory_watch_service.hpp
    src/filesystem_operation_job.cpp
    src/filesystem_operation_job.hpp
    src/shell_loader.cpp
    src/shell_loader.hpp
    src/thumbnail_backend.cpp
    src/thumbnail_backend.hpp
    src/thumbnail_image_provider.cpp
    src/thumbnail_image_provider.hpp)

target_include_directories(odysea_app_adapter
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# The declarative shell is a linkable module rather than a module attached to
# the executable. Anything that links odysea_shell_qml resolves OdySea through
# the module registry, so the tests exercise the same qmldir the application
# loads instead of reading the source directory by relative path.
qt_add_qml_module(odysea_shell_qml
    STATIC
    URI OdySea
    VERSION 1.0
    # Pinned so the module directory is named after the URI regardless of the
    # target name. Import roots and the manifest gate depend on that layout.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/OdySea
    # The appearance state lives in the module as the ShellTheme type, so the
    # application and every scene test configure the same implementation.
    SOURCES
        src/theme_controller.cpp
        src/theme_controller.hpp
        src/theme_palettes.cpp
        src/theme_palettes.hpp
    QML_FILES
        qml/AppearancePanel.qml
        qml/BreadcrumbBar.qml
        qml/DirectoryGridView.qml
        qml/DirectoryListView.qml
        qml/EntryContextMenu.qml
        qml/Main.qml
        qml/FilesystemDialogs.qml)

target_include_directories(odysea_shell_qml
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(odysea_shell_qml
    PUBLIC
        odysea::core
        Qt6::Quick Qt6::QuickControls2
    PRIVATE
        odysea_warnings odysea_sanitizers)

target_link_libraries(odysea_app_adapter
    PUBLIC
        odysea::core
        Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Quick
    PRIVATE
        odysea_warnings odysea_sanitizers)

target_link_libraries(odysea
    PRIVATE
        odysea_app_adapter odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
        odysea_warnings odysea_sanitizers)

install(TARGETS odysea RUNTIME DESTINATION bin)

qt_add_executable(odysea_app_tests
    tests/tst_directory_list_model.cpp)

target_include_directories(odysea_app_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

target_link_libraries(odysea_app_tests
    PRIVATE
        odysea_app_adapter Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_directory_list_model COMMAND odysea_app_tests)

qt_add_executable(odysea_thumbnail_backend_tests
    tests/tst_thumbnail_backend.cpp)

target_link_libraries(odysea_thumbnail_backend_tests
    PRIVATE
        odysea_app_adapter Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_thumbnail_backend COMMAND odysea_thumbnail_backend_tests)

qt_add_executable(odysea_thumbnail_model_tests
    tests/tst_thumbnail_model.cpp)

target_link_libraries(odysea_thumbnail_model_tests
    PRIVATE
        odysea_app_adapter Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_thumbnail_model COMMAND odysea_thumbnail_model_tests)

# The appearance state ships in the shell module; the test drives it as a
# plain object, so persistence and font resolution are covered without a scene.
qt_add_executable(odysea_theme_controller_tests
    tests/tst_theme_controller.cpp)

target_link_libraries(odysea_theme_controller_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_theme_controller COMMAND odysea_theme_controller_tests)

# The rendered-shell tests run from an executable that links the shell module,
# so `import OdySea` resolves exactly as it does in the application. The generic
# qmltestrunner cannot do that: it would only ever see the source directory.
qt_add_executable(odysea_shell_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_shell_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")

target_link_libraries(odysea_shell_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_input_parity COMMAND odysea_shell_tests)

# Renders the real scene from the module against the real decoder, so the
# thumbnail path is covered from a file on disk to a loaded Image.
qt_add_executable(odysea_shell_render_tests
    tests/tst_shell_thumbnail_render.cpp)

target_link_libraries(odysea_shell_render_tests
    PRIVATE
        odysea_app_adapter odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_thumbnail_render COMMAND odysea_shell_render_tests)

# The application's entry point, built against a scene name the module does not
# contain. Nothing links this into a shipped artifact: it exists so the startup
# failure branch is measured as a real process with real output streams and a
# real exit status, which is the only form in which a user or packager meets it.
qt_add_executable(odysea_broken_startup
    src/main.cpp)

target_compile_definitions(odysea_broken_startup
    PRIVATE ODYSEA_STARTUP_SCENE_TYPE_NAME="NoSuchStartupScene")

target_link_libraries(odysea_broken_startup
    PRIVATE
        odysea_app_adapter odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
        odysea_warnings odysea_sanitizers)

# Loads the shell exactly as the application does, so the failure report that
# stands between a broken install and a silent non-zero exit stays covered.
qt_add_executable(odysea_shell_loader_tests
    tests/tst_shell_loader.cpp)

target_compile_definitions(odysea_shell_loader_tests
    PRIVATE
        ODYSEA_STARTUP_BINARY="$<TARGET_FILE:odysea>"
        ODYSEA_BROKEN_STARTUP_BINARY="$<TARGET_FILE:odysea_broken_startup>")

target_link_libraries(odysea_shell_loader_tests
    PRIVATE
        odysea_app_adapter odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test
        odysea_warnings odysea_sanitizers)

add_dependencies(odysea_shell_loader_tests odysea odysea_broken_startup)

add_test(NAME shell_load_diagnostic COMMAND odysea_shell_loader_tests)

qt_add_executable(odysea_shell_entry_interaction_tests
    tests/tst_shell_entry_interactions.cpp)

target_link_libraries(odysea_shell_entry_interaction_tests
    PRIVATE
        odysea_app_adapter odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_entry_interactions COMMAND odysea_shell_entry_interaction_tests)

set_tests_properties(
    shell_input_parity shell_thumbnail_render shell_entry_interactions
    shell_load_diagnostic app_theme_controller
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

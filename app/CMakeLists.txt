# odysea: the Qt Quick application shell.
#
# The shell is GPU-rendered through Qt Quick's scene graph. It consumes the
# toolkit-agnostic odysea::core library for all filesystem work, keeping the
# performance-critical model free of framework overhead.
find_package(Qt6 6.6 REQUIRED COMPONENTS Concurrent Core Gui Qml Quick QuickControls2 QuickTest ShaderTools Test)

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
        qml/DeepFieldGround.qml
        qml/DirectoryGridView.qml
        qml/DirectoryListView.qml
        qml/EffectBlurPass.qml
        qml/EffectBrightPass.qml
        qml/EffectComposite.qml
        qml/EntryContextMenu.qml
        qml/Main.qml
        qml/FilesystemDialogs.qml
        qml/LongFormText.qml
        qml/PresentationLayer.qml
        qml/VectorIcon.qml
        qml/WellMaskLayer.qml
    RESOURCES
        third_party/victor-mono/OFL.txt
        third_party/victor-mono/README.md
        third_party/victor-mono/VictorMono-Bold.otf
        third_party/victor-mono/VictorMono-BoldItalic.otf
        third_party/victor-mono/VictorMono-Italic.otf
        third_party/victor-mono/VictorMono-Regular.otf)

# The presentation shaders compile to qsb at build time and land in the
# module's resource tree next to the scenes, so the QML references resolve
# relative to the importing file.
qt_add_shaders(odysea_shell_qml "odysea_shell_shaders"
    BATCHABLE
    PRECOMPILE
    OPTIMIZED
    PREFIX "/qt/qml/OdySea"
    FILES
        qml/shaders/blur1d.frag
        qml/shaders/brightpass.frag
        qml/shaders/composite.frag)

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
#
# Every QML runner below scans its source directory recursively, so each one is
# pointed at a leaf directory that no other runner owns. A runner aimed at a
# parent of another runner's directory silently adopts that runner's cases:
# they execute twice, and a failure is then reported under the wrong test entry,
# sending a diagnosing reader to a suite that does not contain the fault.
qt_add_executable(odysea_shell_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_shell_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/shell")

target_link_libraries(odysea_shell_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_input_parity COMMAND odysea_shell_tests)

# The presentation-pipeline tests run on the default RHI scene graph so the
# effect chain actually renders. The uniform-binding assertions hold on any
# backend; the frame-comparison tests skip themselves when the environment
# has no usable GPU path and the scene graph falls back to software.
qt_add_executable(odysea_presentation_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_presentation_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/presentation")

target_link_libraries(odysea_presentation_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_presentation COMMAND odysea_presentation_tests)

# The GPU-path gate. Probes for a usable OpenGL context and, when one
# exists, re-executes the presentation suite with the OpenGL RHI scene graph
# forced, so the frame comparisons and shader-failure assertions run as part
# of the shipped gate on every machine that can render them. Without a
# context the probe exits with the CTest skip code instead of aborting.
qt_add_executable(odysea_presentation_rhi_launcher
    tests/tst_presentation_rhi_launcher.cpp)

target_compile_definitions(odysea_presentation_rhi_launcher
    PRIVATE ODYSEA_PRESENTATION_BINARY="$<TARGET_FILE:odysea_presentation_tests>")

target_link_libraries(odysea_presentation_rhi_launcher
    PRIVATE
        Qt6::Gui
        odysea_warnings odysea_sanitizers)

add_dependencies(odysea_presentation_rhi_launcher odysea_presentation_tests)

add_test(NAME shell_presentation_rhi COMMAND odysea_presentation_rhi_launcher)

set_tests_properties(shell_presentation_rhi PROPERTIES
    SKIP_RETURN_CODE 77
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt")

# The same pipeline under the forced software scene graph: the capability
# fallback must disengage silently while content and controls keep working.
qt_add_executable(odysea_presentation_fallback_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_presentation_fallback_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/software")

target_link_libraries(odysea_presentation_fallback_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_presentation_fallback COMMAND odysea_presentation_fallback_tests)

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

# The leak-checker suppression travels with the tests that need it — the ones
# whose font resolution makes fontconfig parse its configuration, a one-time
# third-party allocation that never frees. Attaching it here means a direct
# `ctest --test-dir` invocation inherits it exactly as `ctest --preset asan`
# does, while a deliberate project leak in the same code path stays fatal: the
# suppression matches only allocation stacks with a libfontconfig frame.
set_tests_properties(
    shell_input_parity shell_thumbnail_render shell_entry_interactions
    shell_load_diagnostic app_theme_controller shell_presentation_fallback
    PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt")

# The presentation suite does not pin a scene-graph backend. The offscreen
# platform selects the software scene graph by default, where the
# uniform-binding and control assertions run and the frame comparisons skip
# themselves. Forcing RHI here would abort the whole test process on any
# machine without a working GL context; the shell_presentation_rhi entry
# above closes the gap by probing for a context first and running the full
# suite — frame comparisons included — wherever one exists.
set_tests_properties(
    shell_presentation
    PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt")

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
        src/fuzzy_find_model.cpp
        src/fuzzy_find_model.hpp
        src/miller_columns_model.cpp
        src/miller_columns_model.hpp
        src/storage_usage_model.cpp
        src/storage_usage_model.hpp
        src/theme_controller.cpp
        src/theme_controller.hpp
        src/theme_palettes.cpp
        src/theme_palettes.hpp
    QML_FILES
        qml/ActionBar.qml
        qml/ActionButton.qml
        qml/ActionMenu.qml
        qml/ActionRegistry.qml
        qml/AppearancePanel.qml
        qml/BreadcrumbBar.qml
        qml/ChromeStrip.qml
        qml/CommandPalette.qml
        qml/DeepFieldGround.qml
        qml/DirectoryGridView.qml
        qml/DirectoryListView.qml
        qml/DirectoryPane.qml
        qml/DualPaneLayout.qml
        qml/EffectBlurPass.qml
        qml/EffectBrightPass.qml
        qml/EffectComposite.qml
        qml/FuzzyFindOverlay.qml
        qml/Main.qml
        qml/MillerColumnsView.qml
        qml/FilesystemDialogs.qml
        qml/LongFormText.qml
        qml/PanePlaceholder.qml
        qml/PathNavigator.qml
        qml/PaneFrame.qml
        qml/PresentationLayer.qml
        qml/ShellAction.qml
        qml/ShellActions.qml
        qml/ShellButton.qml
        qml/ShellTextField.qml
        qml/ShellToolBar.qml
        qml/StatusBar.qml
        qml/StorageUsageList.qml
        qml/StorageUsageMap.qml
        qml/StorageUsagePanel.qml
        qml/TabStrip.qml
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
        odysea_app_adapter odysea_warnings odysea_sanitizers)

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

qt_add_executable(odysea_app_cost_tests
    tests/tst_directory_list_model_cost.cpp)

target_include_directories(odysea_app_cost_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

target_link_libraries(odysea_app_cost_tests
    PRIVATE
        odysea_app_adapter Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_directory_list_model_cost COMMAND odysea_app_cost_tests)

qt_add_executable(odysea_app_interaction_tests
    tests/tst_directory_list_model_interaction.cpp)

target_include_directories(odysea_app_interaction_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

target_link_libraries(odysea_app_interaction_tests
    PRIVATE
        odysea_app_adapter Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_directory_list_model_interaction COMMAND odysea_app_interaction_tests)

# Gates the session interlock the GPU-path gates depend on. It renders nothing
# and starts no compositor: it builds run directories, sockets and locks of its
# own and asks the shared check what it makes of them. Registered as an ordinary
# entry because the property it protects — that no declaration can resolve onto
# the login session's compositor — must be checked on every machine, including
# those with no display at all.
qt_add_executable(odysea_isolated_compositor_declaration_tests
    tests/tst_isolated_compositor_declaration.cpp)

target_link_libraries(odysea_isolated_compositor_declaration_tests
    PRIVATE
        Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_isolated_compositor_declaration
    COMMAND odysea_isolated_compositor_declaration_tests)

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

qt_add_executable(odysea_storage_usage_model_tests
    tests/tst_storage_usage_model.cpp)

target_link_libraries(odysea_storage_usage_model_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_storage_usage_model COMMAND odysea_storage_usage_model_tests)

qt_add_executable(odysea_miller_columns_model_tests
    tests/tst_miller_columns_model.cpp)

target_link_libraries(odysea_miller_columns_model_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_miller_columns_model COMMAND odysea_miller_columns_model_tests)

qt_add_executable(odysea_fuzzy_find_model_tests
    tests/tst_fuzzy_find_model.cpp)

target_link_libraries(odysea_fuzzy_find_model_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Test
        odysea_warnings odysea_sanitizers)

add_test(NAME app_fuzzy_find_model COMMAND odysea_fuzzy_find_model_tests)

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
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/shell/input")

target_link_libraries(odysea_shell_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_input_parity COMMAND odysea_shell_tests)

# The reusable chrome components, each instantiated standalone against a
# recording model stand-in. A component that silently depends on the shell
# scene fails here even while the full-shell suites stay green, so this is
# the gate that keeps the components genuinely reusable.
qt_add_executable(odysea_component_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_component_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/components")

target_link_libraries(odysea_component_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

# Keyboard navigation, type-ahead, tab, and view-state cases have their own
# leaf runner so action-heavy pointer and dialog coverage can grow without
# rebuilding another monolithic QML test file.
qt_add_executable(odysea_shell_navigation_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_shell_navigation_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/shell/navigation")

target_link_libraries(odysea_shell_navigation_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_components COMMAND odysea_component_tests)

# The shared action system: registry mechanics, the concrete shell
# declarations, and the shared context menu, all driven against recording
# stand-ins. The shortcut-conflict and cross-surface enablement cases live
# here, so a conflicting declaration or a surface-dependent gate is a test
# failure rather than a runtime surprise.
qt_add_executable(odysea_action_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_action_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/actions")

target_link_libraries(odysea_action_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_action_system COMMAND odysea_action_tests)

# The command palette, driven standalone against recording stand-ins. The
# registry-sourcing invariants live here: a declaration added at runtime
# becomes reachable with no palette-side change, disabled actions stay
# listed with their declared reason, and activation cannot bypass the
# registry's trigger-time revalidation.
qt_add_executable(odysea_palette_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_palette_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/palette")

target_link_libraries(odysea_palette_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_command_palette COMMAND odysea_palette_tests)

# Two simultaneous directory models driven through the real shell. This leaf
# runner owns the dual-pane parity and splitter cases without adopting any
# other QML suite recursively.
qt_add_executable(odysea_dual_pane_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_dual_pane_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/panes")

target_link_libraries(odysea_dual_pane_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_dual_pane COMMAND odysea_dual_pane_tests)

# The storage-usage map and its always-visible accessible list equivalent run
# together against one recording model, covering pointer and keyboard parity
# without involving filesystem timing in the scene test.
qt_add_executable(odysea_storage_usage_view_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_storage_usage_view_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/storage")

target_link_libraries(odysea_storage_usage_view_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_storage_usage COMMAND odysea_storage_usage_view_tests)

# Current-tree fuzzy find follows the command palette's modal interaction
# shape while exercising its own keyboard, pointer, and accessibility routes.
qt_add_executable(odysea_fuzzy_find_view_tests
    tests/tst_shell_quick.cpp)

target_compile_definitions(odysea_fuzzy_find_view_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/find")

target_link_libraries(odysea_fuzzy_find_view_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_fuzzy_find COMMAND odysea_fuzzy_find_view_tests)

add_test(NAME shell_navigation_input COMMAND odysea_shell_navigation_tests)

# The presentation-pipeline tests run on the default RHI scene graph so the
# effect chain actually renders. The uniform-binding assertions hold on any
# backend; the frame-comparison tests skip themselves when the environment
# has no usable GPU path and the scene graph falls back to software.
#
# This suite uses the shared GPU-gate QuickTest main rather than the plain
# runner: the real-compositor gate exports ODYSEA_REQUIRE_GPU_FRAMES, and the
# setup main publishes it to the scene so a software fallback under that gate
# is a failure instead of a silent skip. Every other entry leaves the variable
# unset, so the property is false and the suite behaves exactly as before.
qt_add_executable(odysea_presentation_tests
    tests/tst_gpu_gate_main.cpp)

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

# RESOURCE_LOCK serializes the two GPU-path gates against each other under a
# parallel `ctest -j`. Both re-exec their suite with the OpenGL RHI backend
# forced and drive synchronous window grabs; two of them holding a GPU context
# at once contends the shared device and the render thread, which can return a
# grab before its frame is composited. The gates' own bounded settle-retries
# absorb a momentarily starved frame; this lock removes the largest avoidable
# source of that contention without serializing the whole suite — the lock is
# shared only among the GPU gates, so software tests still run alongside them.
# SKIP_RETURN_CODE is declared, not merely set: this entry has a line in
# tools/skip_declarations.txt recording it as a capability precondition, and the
# coverage reconciler fails on an entry that can skip without one. The same
# applies to every other entry below that carries the property.
set_tests_properties(shell_presentation_rhi PROPERTIES
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK gpu
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt")

# The real-compositor gate. The offscreen RHI gate above renders on a real
# OpenGL context but on the offscreen QPA platform, which never exercises a
# real compositor's frame lifecycle — first-frame exposure, surface
# configure/commit, frame-callback pacing — where six frame-grabbing tests
# were failing undetected. This launcher runs the full presentation suite on
# a declared isolated Wayland compositor with OpenGL RHI forced, and runs the
# whole suite rather than a named function list, so a
# newly added presentation test is exercised on the compositor path
# automatically. It exits 77 with a declaration refusal when no harness-owned
# compositor is proved, and with a capability skip when the proved compositor
# has no usable GL context. The capability skip is a hard failure instead when
# ODYSEA_REQUIRE_COMPOSITOR is set in the environment; that override never
# weakens the declaration boundary.
qt_add_executable(odysea_presentation_compositor_launcher
    tests/tst_presentation_compositor_launcher.cpp)

target_compile_definitions(odysea_presentation_compositor_launcher
    PRIVATE ODYSEA_PRESENTATION_BINARY="$<TARGET_FILE:odysea_presentation_tests>")

target_link_libraries(odysea_presentation_compositor_launcher
    PRIVATE
        Qt6::Gui
        odysea_warnings odysea_sanitizers)

add_dependencies(odysea_presentation_compositor_launcher odysea_presentation_tests)

add_test(NAME shell_presentation_compositor COMMAND odysea_presentation_compositor_launcher)

# Non-rendering contract for the platform-selection boundary. It hands the
# launcher deliberately unreachable X11 and Wayland endpoints and requires a
# declaration refusal before Qt can connect to either one. This keeps an
# inherited DISPLAY, WAYLAND_DISPLAY, or QT_QPA_PLATFORM from becoming an
# opt-out that selects a different platform and skips the fractional-surface
# assertions on hardware capable of running them.
add_test(NAME compositor_platform_selection_contract
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_compositor_platform_contract.sh
        $<TARGET_FILE:odysea_presentation_compositor_launcher>)

set_tests_properties(compositor_platform_selection_contract PROPERTIES TIMEOUT 10)

# Shares the gpu RESOURCE_LOCK with the other two GPU-path gates so no two of
# them hold a GPU context at once under a parallel run. ODYSEA_REQUIRE_COMPOSITOR
# is deliberately not baked in here: a machine with no compositor must skip
# honestly rather than fail. The verifying environment sets it externally so a
# skip there becomes a failure.
set_tests_properties(shell_presentation_compositor PROPERTIES
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK gpu
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

# Visual-foundation acceptance: layout integrity across every density's live
# compact breakpoint and interface-scale extremes, focus indicators with
# effects off, reduced-motion stability, effects-off usability, virtualization
# under a representative large directory, and device-pixel-ratio behavior.
# The suite runs twice on the software path — once at 1x and once with
# QT_SCALE_FACTOR=2 — because every layout and mask assertion is written in
# logical coordinates and must hold under both configurations. The latter is
# scaled-layout coverage, not a 2x framebuffer; only the real-compositor entry
# below can run the device-resolution assertions on a genuine 2x surface.
# It uses the shared GPU-gate main so that entry can turn a software fallback
# into a failure and declare the scale it launched at; every other entry
# leaves those variables unset and behaves exactly as before.
qt_add_executable(odysea_validation_tests
    tests/tst_gpu_gate_main.cpp)

target_compile_definitions(odysea_validation_tests
    PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/validation")

target_link_libraries(odysea_validation_tests
    PRIVATE
        odysea_shell_qml odysea_shell_qmlplugin
        Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::QuickTest
        odysea_warnings odysea_sanitizers)

add_test(NAME shell_visual_validation COMMAND odysea_validation_tests)

add_test(NAME shell_visual_validation_scaled_layout COMMAND odysea_validation_tests)

# The GPU half of the device-pixel gate: the protected-well border sweep
# needs a real scene-graph backend. The launcher probes for an OpenGL
# context exactly like the presentation gate and skips cleanly without one.
# The offscreen platform cannot allocate a genuine high-density framebuffer
# — under a forced scale factor it reports 2x while rasterizing at 1x and
# grabs frames without the scene at device resolution — so this entry runs
# the pixel assertions at 1x, where they are real; the suite's vacuity
# sentinel rejects any environment that would grab an empty frame. Running
# the same suite on a windowing system with a real 2x surface exercises
# the identical assertions at full density.
qt_add_executable(odysea_validation_rhi_launcher
    tests/tst_presentation_rhi_launcher.cpp)

target_compile_definitions(odysea_validation_rhi_launcher
    PRIVATE ODYSEA_PRESENTATION_BINARY="$<TARGET_FILE:odysea_validation_tests>")

target_link_libraries(odysea_validation_rhi_launcher
    PRIVATE
        Qt6::Gui
        odysea_warnings odysea_sanitizers)

add_dependencies(odysea_validation_rhi_launcher odysea_validation_tests)

add_test(NAME shell_visual_validation_rhi
    COMMAND odysea_validation_rhi_launcher
        DevicePixelScaling::test_maskGeometryStaysLogical
        DevicePixelScaling::test_frameRendersAtDeviceResolution
        DevicePixelScaling::test_perAxisRoundingReachedIsRecordedAndBounded
        DevicePixelScaling::test_wellBorderStaysByteTrueAtDeviceResolution
        DevicePixelScaling::test_ringOutsideWellStaysProcessed)

set_tests_properties(shell_visual_validation_rhi PROPERTIES
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK gpu
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt")

# The genuine-2x device-resolution gate. The offscreen platform cannot
# allocate a real high-density framebuffer — under QT_SCALE_FACTOR=2 it reports
# a ratio of two while rasterizing at 1x — so the offscreen RHI entry above
# runs the pixel assertions at 1x, and the software scaled-layout pass checks
# only logical-coordinate layout. Neither exercises the device-resolution pixel
# comparisons at a true 2x surface. This launcher runs the validation suite on
# a real compositor with OpenGL RHI forced and QT_SCALE_FACTOR=2, so a windowing
# system that can allocate a 2x surface grabs frames at genuine device
# resolution and the border, ring, and interior sweeps run at full density.
# ODYSEA_EXPECTED_FRAME_SCALE=2 makes the device-resolution test assert the
# frame is exactly twice the logical size, so a run that fell back to 1x fails
# instead of passing as if it had rendered at 2x. It reuses the compositor
# launcher, which skips (77) when no compositor or no usable GL is present and
# turns that skip into a failure when ODYSEA_REQUIRE_COMPOSITOR is set in the
# verifying environment.
#
# It does NOT run against whatever session is in the environment. The suite
# renders an activating window, so a compositor it does not own is one it can
# leave with input focus on a surface nobody is looking at, and an abnormal exit
# leaves that behind. The launcher therefore declines with a skip unless
# ODYSEA_ISOLATED_COMPOSITOR marks the compositor as started for the run and
# disposable after it — see the interlock note in the launcher source. No
# harness here starts one yet, so this entry currently declines everywhere and
# the real-compositor path is unmeasured. That is deliberate and is recorded as
# a known gap rather than papered over with a run against a session in use.
qt_add_executable(odysea_validation_compositor_launcher
    tests/tst_presentation_compositor_launcher.cpp)

target_compile_definitions(odysea_validation_compositor_launcher
    PRIVATE ODYSEA_PRESENTATION_BINARY="$<TARGET_FILE:odysea_validation_tests>")

target_link_libraries(odysea_validation_compositor_launcher
    PRIVATE
        Qt6::Gui
        odysea_warnings odysea_sanitizers)

add_dependencies(odysea_validation_compositor_launcher odysea_validation_tests)

add_test(NAME shell_visual_validation_compositor_2x
    COMMAND odysea_validation_compositor_launcher)

set_tests_properties(shell_visual_validation_compositor_2x PROPERTIES
    SKIP_RETURN_CODE 77
    RESOURCE_LOCK gpu
    ENVIRONMENT "QT_SCALE_FACTOR=2;ODYSEA_EXPECTED_FRAME_SCALE=2;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt")

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
    shell_input_parity shell_components shell_navigation_input
    shell_action_system shell_command_palette shell_dual_pane
    shell_storage_usage shell_fuzzy_find
    shell_thumbnail_render shell_entry_interactions shell_load_diagnostic
    app_theme_controller app_storage_usage_model app_fuzzy_find_model
    shell_presentation_fallback shell_visual_validation
    PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt")

# The scaled-layout pass of the validation suite: identical software
# environment plus the doubled scale factor, so logical-coordinate layout and
# mask assertions are checked under doubled scaling. The offscreen surface
# still rasterizes at 1x; the name deliberately does not present this entry as
# genuine device-resolution evidence.
set_tests_properties(
    shell_visual_validation_scaled_layout
    PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=2;LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt")

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

# Application smoke gate. The launch must come up and stay up without a fatal
# fault. It replaces the "zero bytes on both streams" criterion — which a core
# dump also satisfied — with one a core dump cannot: the gate script forces the
# offscreen software path with QT_FORCE_STDERR_LOGGING=1 and records when its
# watchdog terminates a process that survived the observation window. This
# distinguishes that harness-owned signal from an early exit — including an
# early status 124 — or a crash, and a stderr carrying a platform-plugin, RHI,
# scene-graph, or sanitizer fault still fails an otherwise live launch. Offscreen
# software renders at device pixel ratio 1 and runs with or without a display,
# so the gate is deterministic everywhere; a real-GPU launch is the compositor
# gate's stronger and separate job, not a second offscreen smoke run twice.
add_test(NAME application_smoke
    COMMAND bash ${CMAKE_SOURCE_DIR}/tools/check_smoke.sh $<TARGET_FILE:odysea> 5)

set_tests_properties(application_smoke PROPERTIES
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_SOURCE_DIR}/tools/lsan_suppressions.txt"
    TIMEOUT 12)

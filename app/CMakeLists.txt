# odysea: the Qt Quick application shell.
#
# The shell is GPU-rendered through Qt Quick's scene graph. It consumes the
# toolkit-agnostic odysea::core library for all filesystem work, keeping the
# performance-critical model free of framework overhead.
find_package(Qt6 6.6 REQUIRED COMPONENTS Concurrent Core Gui Qml Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.6)
qt_policy(SET QTP0004 NEW)

qt_add_executable(odysea
    src/main.cpp
    src/directory_list_model.cpp
    src/directory_list_model.hpp)

qt_add_qml_module(odysea
    URI OdySea
    VERSION 1.0
    QML_FILES
        qml/Main.qml)

target_link_libraries(odysea
    PRIVATE
        odysea::core
        Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
        odysea_warnings odysea_sanitizers)

install(TARGETS odysea RUNTIME DESTINATION bin)

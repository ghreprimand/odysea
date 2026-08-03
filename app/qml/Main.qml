// OdySea shell foundation. Navigation, selection, filtering, tabs, panes, and
// filesystem-operation requests expose matching pointer and keyboard paths.
//
// The window composes the module's reusable chrome components — toolbar,
// breadcrumbs, tab strip, action row, directory panes, and status strip —
// and owns what is genuinely shell-wide: the theme instance, the shortcut
// table, the type-ahead engine, view-mode state, and the presentation
// pipeline that processes everything beneath the popup overlay.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
// Self-import: resolves the module's compiled types (ShellTheme) for tooling
// and runtime alike.
import OdySea

ApplicationWindow {
    id: root

    required property var shellModel

    /// Where appearance preferences persist. The application injects the real
    /// location; the default keeps scenes and tests in memory only.
    property string themeStoragePath: ""
    readonly property ShellTheme shellTheme: ShellTheme {
        storagePath: root.themeStoragePath
    }

    objectName: "mainWindow"
    readonly property int rowHeight: shellTheme.rowHeight
    readonly property color backgroundColor: shellTheme.background
    readonly property color panelColor: shellTheme.panel
    readonly property color borderColor: shellTheme.border
    readonly property color primaryTextColor: shellTheme.text
    readonly property color secondaryTextColor: shellTheme.textMuted
    readonly property color accentColor: shellTheme.accent
    readonly property color selectionColor: shellTheme.selectionBed
    readonly property int typeAheadTimeoutMs: 900
    property bool gridMode: false
    property string typeAheadBuffer: ""

    width: 1100
    height: 720
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: root.shellModel.path.length > 0 ? root.shellModel.path + " — OdySea" : "OdySea"
    color: backgroundColor
    font.family: shellTheme.chromeFontFamily
    font.pixelSize: shellTheme.chromeFontPixelSize

    function formatSize(bytes) {
        if (bytes < 1024) {
            return bytes + " B";
        }
        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(1) + " KiB";
        }
        if (bytes < 1024 * 1024 * 1024) {
            return (bytes / (1024 * 1024)).toFixed(1) + " MiB";
        }
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB";
    }

    function activateRelativeTab(offset) {
        const count = root.shellModel.tabCount;
        if (count < 1) {
            return;
        }
        const nextTab = (root.shellModel.activeTab + offset + count) % count;
        root.shellModel.activateTab(nextTab);
    }

    function activeDirectoryPane() {
        return root.shellModel.activePane === 0 ? firstPaneLoader.item : secondPaneLoader.item;
    }

    function clearTypeAhead() {
        typeAheadBuffer = "";
        typeAheadTimer.stop();
    }

    function focusCurrentView() {
        Qt.callLater(function () {
            const pane = root.activeDirectoryPane();
            if (pane !== null) {
                pane.focusCurrentView();
                pane.revealCurrent();
            }
        });
    }

    function printableKeyText(event) {
        if ((event.key === Qt.Key_Space && typeAheadBuffer.length === 0) || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) !== 0 || event.text.length === 0) {
            return "";
        }
        const codePoint = event.text.codePointAt(0);
        return codePoint >= 0x20 && codePoint !== 0x7f ? event.text : "";
    }

    function handleTypeAhead(event, view) {
        if (event.key === Qt.Key_Backspace) {
            if (typeAheadBuffer.length === 0) {
                return false;
            }
            typeAheadBuffer = typeAheadBuffer.slice(0, -1);
            if (typeAheadBuffer.length > 0) {
                root.shellModel.selectByPrefix(typeAheadBuffer, false);
                typeAheadTimer.restart();
                view.revealCurrent();
            } else {
                typeAheadTimer.stop();
            }
            return true;
        }
        if (event.key === Qt.Key_Escape && typeAheadBuffer.length > 0) {
            root.clearTypeAhead();
            return true;
        }

        const text = root.printableKeyText(event);
        if (text.length === 0) {
            return false;
        }
        const previous = typeAheadBuffer;
        const repeatedSingleCharacter = previous.length === 1 && previous.toLocaleLowerCase() === text.toLocaleLowerCase();
        const cycle = previous.length === 0 || repeatedSingleCharacter;
        typeAheadBuffer = repeatedSingleCharacter ? text : previous + text;
        root.shellModel.selectByPrefix(typeAheadBuffer, cycle);
        typeAheadTimer.restart();
        view.revealCurrent();
        return true;
    }

    function switchView(useGrid) {
        gridMode = useGrid;
        root.clearTypeAhead();
        root.focusCurrentView();
    }

    function activateTabIndex(index) {
        if (index < root.shellModel.tabCount) {
            root.shellModel.activateTab(index);
            root.clearTypeAhead();
            root.focusCurrentView();
        }
    }

    Timer {
        id: typeAheadTimer

        interval: root.typeAheadTimeoutMs
        onTriggered: root.typeAheadBuffer = ""
    }

    Shortcut {
        sequence: "Alt+Left"
        enabled: root.shellModel.canGoBack
        onActivated: root.shellModel.goBack()
    }
    Shortcut {
        sequence: "Alt+Right"
        enabled: root.shellModel.canGoForward
        onActivated: root.shellModel.goForward()
    }
    Shortcut {
        sequence: "Alt+Up"
        enabled: root.shellModel.canGoUp
        onActivated: root.shellModel.goUp()
    }
    Shortcut {
        sequence: "F5"
        onActivated: root.shellModel.refresh()
    }
    Shortcut {
        sequence: "Ctrl+H"
        onActivated: root.shellModel.showHidden = !root.shellModel.showHidden
    }
    Shortcut {
        sequence: "Ctrl+1"
        enabled: root.shellModel.tabCount > 0
        onActivated: root.activateTabIndex(0)
    }
    Shortcut {
        sequence: "Ctrl+2"
        enabled: root.shellModel.tabCount > 1
        onActivated: root.activateTabIndex(1)
    }
    Shortcut {
        sequence: "Ctrl+3"
        enabled: root.shellModel.tabCount > 2
        onActivated: root.activateTabIndex(2)
    }
    Shortcut {
        sequence: "Ctrl+4"
        enabled: root.shellModel.tabCount > 3
        onActivated: root.activateTabIndex(3)
    }
    Shortcut {
        sequence: "Ctrl+5"
        enabled: root.shellModel.tabCount > 4
        onActivated: root.activateTabIndex(4)
    }
    Shortcut {
        sequence: "Ctrl+6"
        enabled: root.shellModel.tabCount > 5
        onActivated: root.activateTabIndex(5)
    }
    Shortcut {
        sequence: "Ctrl+7"
        enabled: root.shellModel.tabCount > 6
        onActivated: root.activateTabIndex(6)
    }
    Shortcut {
        sequence: "Ctrl+8"
        enabled: root.shellModel.tabCount > 7
        onActivated: root.activateTabIndex(7)
    }
    Shortcut {
        sequence: "Ctrl+9"
        enabled: root.shellModel.tabCount > 8
        onActivated: root.activateTabIndex(8)
    }
    Shortcut {
        sequence: "Ctrl+Shift+1"
        onActivated: root.switchView(false)
    }
    Shortcut {
        sequence: "Ctrl+Shift+2"
        onActivated: root.switchView(true)
    }
    Shortcut {
        sequence: "Ctrl+Shift+S"
        onActivated: root.shellModel.sortMode = (root.shellModel.sortMode + 1) % 3
    }
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: navigationToolBar.focusAddressField()
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: actionBar.focusFilterField()
    }
    Shortcut {
        sequence: "Ctrl+T"
        onActivated: root.shellModel.addTab()
    }
    Shortcut {
        sequence: "Ctrl+W"
        onActivated: root.shellModel.closeTab(root.shellModel.activeTab)
    }
    Shortcut {
        sequence: "Ctrl+Tab"
        onActivated: root.activateRelativeTab(1)
    }
    Shortcut {
        sequence: "Ctrl+Shift+Tab"
        onActivated: root.activateRelativeTab(-1)
    }
    Shortcut {
        sequence: "Ctrl+Shift+P"
        onActivated: root.shellModel.setDualPaneEnabled(root.shellModel.paneCount === 1)
    }
    Shortcut {
        sequence: "F6"
        enabled: root.shellModel.paneCount === 2
        onActivated: root.shellModel.activatePane(root.shellModel.activePane === 0 ? 1 : 0)
    }
    Shortcut {
        sequence: "Ctrl+A"
        onActivated: root.shellModel.selectAll()
    }
    Shortcut {
        sequence: "Ctrl+C"
        onActivated: root.shellModel.requestCopy()
    }
    Shortcut {
        sequence: "Ctrl+X"
        onActivated: root.shellModel.requestMove()
    }
    Shortcut {
        sequence: "F2"
        onActivated: root.shellModel.requestRename()
    }
    Shortcut {
        sequence: "Delete"
        onActivated: root.shellModel.requestTrash()
    }
    Shortcut {
        sequence: "Ctrl+,"
        onActivated: appearancePanel.open()
    }

    AppearancePanel {
        id: appearancePanel

        parent: root.contentItem
        theme: root.shellTheme
    }

    // Everything the presentation pipeline processes lives under one item:
    // chrome, panes, and status. Popups render in the window overlay above
    // the pipeline instead, which keeps modal surfaces solid and color-true.
    Item {
        id: shellContent

        anchors.fill: parent

        // Window ground: the deep-field material behind every surface.
        DeepFieldGround {
            anchors.fill: parent
            deepField: root.shellTheme.effectiveDeepField
            sheetColor: root.backgroundColor
            deepColor: root.shellTheme.backgroundDeep
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            ShellToolBar {
                id: navigationToolBar

                Layout.fillWidth: true
                shellModel: root.shellModel
                navigationController: root
                theme: root.shellTheme
                onAppearanceRequested: appearancePanel.open()
            }

            BreadcrumbBar {
                Layout.fillWidth: true
                shellModel: root.shellModel
                navigationController: root
                backgroundColor: Qt.alpha(root.panelColor, root.shellTheme.surfaceOpacity)
                borderColor: root.borderColor
                primaryTextColor: root.primaryTextColor
                accentColor: root.accentColor
                hoverColor: root.shellTheme.hover
                pressedColor: root.shellTheme.pressed
                pathFontFamily: root.shellTheme.pathFontFamily
                pathFontPixelSize: root.shellTheme.pathFontPixelSize
            }

            TabStrip {
                Layout.fillWidth: true
                shellModel: root.shellModel
                theme: root.shellTheme
            }

            ActionBar {
                id: actionBar

                Layout.fillWidth: true
                shellModel: root.shellModel
                theme: root.shellTheme
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 8
                spacing: 8

                Loader {
                    id: firstPaneLoader

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: root.shellModel.activePane === 0
                    visible: active
                    sourceComponent: paneComponent
                }

                PanePlaceholder {
                    visible: root.shellModel.paneCount === 2 && root.shellModel.activePane !== 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    shellModel: root.shellModel
                    theme: root.shellTheme
                    paneIndex: 0
                }

                PanePlaceholder {
                    visible: root.shellModel.paneCount === 2 && root.shellModel.activePane !== 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    shellModel: root.shellModel
                    theme: root.shellTheme
                    paneIndex: 1
                }

                Loader {
                    id: secondPaneLoader

                    visible: root.shellModel.paneCount === 2
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    active: root.shellModel.paneCount === 2 && root.shellModel.activePane === 1
                    sourceComponent: paneComponent
                }
            }

            StatusBar {
                Layout.fillWidth: true
                shellModel: root.shellModel
                theme: root.shellTheme
            }
        }
    }

    Component {
        id: paneComponent

        DirectoryPane {
            shellModel: root.shellModel
            navigationController: root
            theme: root.shellTheme
            gridMode: root.gridMode
            persistenceDurationMs: presentationLayer.motionDurationMs
            wellLayer: wellMaskLayer
        }
    }

    WellMaskLayer {
        id: wellMaskLayer

        objectName: "wellMaskLayer"
        anchors.fill: shellContent
    }

    PresentationLayer {
        id: presentationLayer

        objectName: "presentationLayer"
        anchors.fill: shellContent
        content: shellContent
        wellMask: wellMaskLayer
        theme: root.shellTheme
    }

    FilesystemDialogs {
        shellModel: root.shellModel
        theme: root.shellTheme
        backgroundColor: root.backgroundColor
        panelColor: root.panelColor
        borderColor: root.borderColor
        primaryTextColor: root.primaryTextColor
        secondaryTextColor: root.secondaryTextColor
        accentColor: root.accentColor
        dangerColor: root.shellTheme.danger
    }
}

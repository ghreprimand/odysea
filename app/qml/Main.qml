// OdySea shell foundation. Navigation, selection, filtering, tabs, panes, and
// filesystem-operation requests expose matching pointer and keyboard paths.
//
// The window composes the module's reusable chrome components — toolbar,
// breadcrumbs, tab strip, action row, directory panes, and status strip —
// and owns what is genuinely shell-wide: the theme instance, the action
// registry with its instantiated shortcut table, the type-ahead engine,
// view-mode state, and the presentation pipeline that processes everything
// beneath the popup overlay.
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

    /// The single action declaration site. Every menu, button, and key
    /// sequence in the shell renders from these declarations.
    readonly property ShellActions actions: ShellActions {
        shellModel: root.shellModel
        shell: root
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

    // Focus and surface entry points the registry's actions target. The
    // declarations call these instead of reaching into chrome internals.
    function focusAddressField() {
        navigationToolBar.focusAddressField();
    }

    function focusFilterField() {
        actionBar.focusFilterField();
    }

    function openAppearancePanel() {
        appearancePanel.open();
    }

    Timer {
        id: typeAheadTimer

        interval: root.typeAheadTimeoutMs
        onTriggered: root.typeAheadBuffer = ""
    }

    // The shortcut table is instantiated from the registry's declarations:
    // one Shortcut per declared sequence, firing through the registry so
    // enablement is revalidated at activation. The declarations themselves
    // live with the actions in ShellActions.
    Instantiator {
        objectName: "shortcutTable"
        model: root.actions.shortcutEntries()

        delegate: Shortcut {
            id: boundShortcut

            required property var modelData
            readonly property var action: root.actions.find(modelData.actionId)
            readonly property var actionContext: root.actions.globalContext(modelData.argument)

            sequence: modelData.sequence
            // isEnabled reads live model state inside the predicates, so
            // this binding re-evaluates with the model; triggering
            // revalidates once more before the handler runs.
            enabled: root.actions.isEnabled(action, actionContext)
            onActivated: root.actions.trigger(modelData.actionId, actionContext)
        }
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
                registry: root.actions
                theme: root.shellTheme
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
                registry: root.actions
                theme: root.shellTheme
            }

            ActionBar {
                id: actionBar

                Layout.fillWidth: true
                shellModel: root.shellModel
                registry: root.actions
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
                    registry: root.actions
                    theme: root.shellTheme
                    paneIndex: 0
                }

                PanePlaceholder {
                    visible: root.shellModel.paneCount === 2 && root.shellModel.activePane !== 1
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    shellModel: root.shellModel
                    registry: root.actions
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
            registry: root.actions
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

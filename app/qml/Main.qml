// OdySea shell foundation. Navigation, selection, filtering, tabs, panes, and
// filesystem-operation requests expose matching pointer and keyboard paths.
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
    font.family: shellTheme.fontFamily
    font.pixelSize: shellTheme.fontPixelSize

    component ShellButton: Button {
        id: control
        implicitHeight: 32
        leftPadding: 11
        rightPadding: 11

        contentItem: Text {
            text: control.text
            color: control.enabled ? root.primaryTextColor : root.shellTheme.textFaint
            font.family: root.shellTheme.fontFamily
            font.pixelSize: root.shellTheme.fontPixelSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: control.down ? root.shellTheme.pressed : (control.hovered ? root.shellTheme.hover : root.panelColor)
            border.color: control.activeFocus ? root.accentColor : root.borderColor
            radius: 5
        }
    }

    component DirectoryPane: FocusScope {
        id: pane

        function focusCurrentView() {
            if (root.gridMode) {
                directoryGrid.forceViewFocus();
            } else {
                directoryList.focusView();
            }
        }

        function revealCurrent() {
            if (root.gridMode) {
                directoryGrid.revealCurrent();
            } else {
                directoryList.revealCurrent();
            }
        }

        Rectangle {
            anchors.fill: parent
            color: root.backgroundColor
            border.color: directoryList.activeFocus || directoryGrid.activeFocus ? root.accentColor : root.borderColor
            radius: 6
        }

        DirectoryListView {
            id: directoryList

            anchors.fill: parent
            visible: !root.gridMode
            shellModel: root.shellModel
            navigationController: root
            rowHeight: root.rowHeight
            accentColor: root.accentColor
            primaryTextColor: root.primaryTextColor
            secondaryTextColor: root.secondaryTextColor
            selectionColor: root.selectionColor
            dirInkColor: root.shellTheme.dirInk
            fileInkColor: root.shellTheme.textFaint
            dangerColor: root.shellTheme.danger
            hoverColor: root.shellTheme.hover
            rubberBandColor: root.shellTheme.rubberBand
            contentFontFamily: root.shellTheme.fontFamily
            contentFontPixelSize: root.shellTheme.contentFontPixelSize
            metaFontPixelSize: root.shellTheme.metaFontPixelSize
        }

        DirectoryGridView {
            id: directoryGrid

            anchors.fill: parent
            visible: root.gridMode
            shellModel: root.shellModel
            navigationController: root
            backgroundColor: root.backgroundColor
            panelColor: root.panelColor
            borderColor: root.borderColor
            primaryTextColor: root.primaryTextColor
            secondaryTextColor: root.secondaryTextColor
            accentColor: root.accentColor
            selectionColor: root.selectionColor
            dirInkColor: root.shellTheme.dirInk
            fileInkColor: root.shellTheme.textFaint
            dangerColor: root.shellTheme.danger
            hoverColor: root.shellTheme.hover
            rubberBandColor: root.shellTheme.rubberBand
            contentFontFamily: root.shellTheme.fontFamily
            contentFontPixelSize: root.shellTheme.contentFontPixelSize
            metaFontPixelSize: root.shellTheme.metaFontPixelSize
            cellWidth: root.shellTheme.gridCellWidth
            cellHeight: root.shellTheme.gridCellHeight
        }
    }

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
        onActivated: {
            addressField.forceActiveFocus();
            addressField.selectAll();
        }
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            filterField.forceActiveFocus();
            filterField.selectAll();
        }
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

    header: ColumnLayout {
        spacing: 0

        ToolBar {
            Layout.fillWidth: true

            background: Rectangle {
                color: root.panelColor
                border.color: root.borderColor
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6

                ShellButton {
                    text: "\u2190"
                    enabled: root.shellModel.canGoBack
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Back (Alt+Left)")
                    onClicked: root.shellModel.goBack()
                }
                ShellButton {
                    text: "\u2192"
                    enabled: root.shellModel.canGoForward
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Forward (Alt+Right)")
                    onClicked: root.shellModel.goForward()
                }
                ShellButton {
                    text: "\u2191"
                    enabled: root.shellModel.canGoUp
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Up (Alt+Up)")
                    onClicked: root.shellModel.goUp()
                }
                ShellButton {
                    text: "\u21bb"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Refresh (F5)")
                    onClicked: root.shellModel.refresh()
                }

                TextField {
                    id: addressField
                    Layout.fillWidth: true
                    text: root.shellModel.path
                    color: root.primaryTextColor
                    selectByMouse: true
                    placeholderText: qsTr("Location")
                    onAccepted: {
                        root.shellModel.path = text;
                        text = root.shellModel.path;
                        focus = false;
                    }

                    background: Rectangle {
                        color: root.backgroundColor
                        border.color: addressField.activeFocus ? root.accentColor : root.borderColor
                        radius: 5
                    }
                }

                Connections {
                    target: root.shellModel

                    function onPathChanged() {
                        if (!addressField.activeFocus) {
                            addressField.text = root.shellModel.path;
                        }
                    }
                }

                ShellButton {
                    text: root.shellModel.paneCount === 2 ? qsTr("1 pane") : qsTr("2 panes")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Toggle pane workspace (Ctrl+Shift+P)")
                    onClicked: root.shellModel.setDualPaneEnabled(root.shellModel.paneCount === 1)
                }
                ShellButton {
                    objectName: "listViewButton"
                    text: qsTr("List")
                    checkable: true
                    checked: !root.gridMode
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("List view (Ctrl+Shift+1)")
                    onClicked: root.switchView(false)
                }
                ShellButton {
                    objectName: "gridViewButton"
                    text: qsTr("Grid")
                    checkable: true
                    checked: root.gridMode
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Grid view (Ctrl+Shift+2)")
                    onClicked: root.switchView(true)
                }
                ShellButton {
                    objectName: "appearanceButton"
                    text: qsTr("Appearance")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Appearance settings (Ctrl+,)")
                    onClicked: appearancePanel.open()
                }
            }
        }

        BreadcrumbBar {
            Layout.fillWidth: true
            shellModel: root.shellModel
            navigationController: root
            backgroundColor: root.panelColor
            borderColor: root.borderColor
            primaryTextColor: root.primaryTextColor
            accentColor: root.accentColor
            hoverColor: root.shellTheme.hover
            pressedColor: root.shellTheme.pressed
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: root.panelColor
            border.color: root.borderColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 7
                anchors.rightMargin: 7
                spacing: 5

                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    implicitHeight: 34
                    currentIndex: root.shellModel.activeTab

                    background: Item {}

                    Repeater {
                        model: root.shellModel.tabCount

                        TabButton {
                            id: tabButton

                            required property int index
                            objectName: "tabButton-" + index
                            implicitWidth: 140
                            text: root.shellModel.tabLabel(index)
                            onClicked: root.shellModel.activateTab(index)

                            contentItem: Text {
                                text: tabButton.text
                                color: tabButton.checked ? root.accentColor : root.primaryTextColor
                                elide: Text.ElideRight
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                color: tabButton.checked ? root.backgroundColor : root.panelColor
                                border.color: tabButton.checked ? root.accentColor : root.borderColor
                                radius: 5
                            }
                        }
                    }
                }

                ShellButton {
                    text: "+"
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("New tab (Ctrl+T)")
                    onClicked: root.shellModel.addTab()
                }
                ShellButton {
                    text: "\u00d7"
                    enabled: root.shellModel.tabCount > 1
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Close tab (Ctrl+W)")
                    onClicked: root.shellModel.closeTab(root.shellModel.activeTab)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 44
            color: root.backgroundColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                TextField {
                    id: filterField
                    objectName: "filterField"
                    Layout.preferredWidth: 260
                    placeholderText: qsTr("Filter this folder (Ctrl+F)")
                    color: root.primaryTextColor
                    selectByMouse: true
                    onTextEdited: root.shellModel.filterText = text

                    background: Rectangle {
                        color: root.panelColor
                        border.color: filterField.activeFocus ? root.accentColor : root.borderColor
                        radius: 5
                    }
                }

                ComboBox {
                    id: sortBox
                    Layout.preferredWidth: 130
                    model: [qsTr("Name"), qsTr("Size"), qsTr("Type")]
                    currentIndex: root.shellModel.sortMode
                    onActivated: index => root.shellModel.sortMode = index
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Sort order (Ctrl+Shift+S)")
                }

                CheckBox {
                    text: qsTr("Hidden")
                    checked: root.shellModel.showHidden
                    onToggled: root.shellModel.showHidden = checked
                }

                ShellButton {
                    objectName: "selectAllButton"
                    text: qsTr("Select all")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Select all entries (Ctrl+A)")
                    onClicked: root.shellModel.selectAll()
                }

                Item {
                    Layout.fillWidth: true
                }

                ShellButton {
                    objectName: "copyButton"
                    text: qsTr("Copy")
                    enabled: root.shellModel.selectedCount > 0 && !root.shellModel.operationBusy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Copy selection (Ctrl+C)")
                    onClicked: root.shellModel.requestCopy()
                }
                ShellButton {
                    objectName: "moveButton"
                    text: qsTr("Move")
                    enabled: root.shellModel.selectedCount > 0 && !root.shellModel.operationBusy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Move selection (Ctrl+X)")
                    onClicked: root.shellModel.requestMove()
                }
                ShellButton {
                    objectName: "renameButton"
                    text: qsTr("Rename")
                    enabled: root.shellModel.selectedCount === 1 && !root.shellModel.operationBusy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Rename selection (F2)")
                    onClicked: root.shellModel.requestRename()
                }
                ShellButton {
                    objectName: "trashButton"
                    text: qsTr("Trash")
                    enabled: root.shellModel.selectedCount > 0 && !root.shellModel.operationBusy
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Move selection to trash (Delete)")
                    onClicked: root.shellModel.requestTrash()
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        Loader {
            id: firstPaneLoader

            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.shellModel.activePane === 0
            visible: active
            sourceComponent: DirectoryPane {}
        }

        Rectangle {
            visible: root.shellModel.paneCount === 2 && root.shellModel.activePane !== 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.panelColor
            border.color: root.borderColor
            radius: 6

            Text {
                anchors.centerIn: parent
                text: qsTr("Pane 1\nClick or press F6 to activate")
                color: root.secondaryTextColor
                horizontalAlignment: Text.AlignHCenter
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.shellModel.activatePane(0)
            }
        }

        Rectangle {
            visible: root.shellModel.paneCount === 2 && root.shellModel.activePane !== 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.panelColor
            border.color: root.borderColor
            radius: 6

            Text {
                anchors.centerIn: parent
                text: qsTr("Pane 2\nClick or press F6 to activate")
                color: root.secondaryTextColor
                horizontalAlignment: Text.AlignHCenter
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.shellModel.activatePane(1)
            }
        }

        Loader {
            id: secondPaneLoader

            visible: root.shellModel.paneCount === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.shellModel.paneCount === 2 && root.shellModel.activePane === 1
            sourceComponent: DirectoryPane {}
        }
    }

    FilesystemDialogs {
        shellModel: root.shellModel
        backgroundColor: root.backgroundColor
        panelColor: root.panelColor
        borderColor: root.borderColor
        primaryTextColor: root.primaryTextColor
        secondaryTextColor: root.secondaryTextColor
        accentColor: root.accentColor
        dangerColor: root.shellTheme.danger
    }

    footer: Rectangle {
        implicitHeight: 30
        color: root.panelColor
        border.color: root.borderColor

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 12

            BusyIndicator {
                visible: root.shellModel.busy || root.shellModel.operationBusy
                running: visible
                implicitWidth: 20
                implicitHeight: 20
            }

            Text {
                Layout.fillWidth: true
                text: root.shellModel.operationErrorString.length > 0 ? root.shellModel.operationErrorString : (root.shellModel.errorString.length > 0 ? qsTr("Could not read folder: ") + root.shellModel.errorString : root.shellModel.statusMessage)
                color: root.shellModel.operationErrorString.length > 0 || root.shellModel.errorString.length > 0 ? root.shellTheme.danger : root.secondaryTextColor
                elide: Text.ElideRight
                font.pixelSize: root.shellTheme.metaFontPixelSize
            }

            Text {
                text: qsTr("%1 selected").arg(root.shellModel.selectedCount)
                color: root.secondaryTextColor
                font.pixelSize: root.shellTheme.metaFontPixelSize
            }

            Text {
                text: qsTr("Pane %1 of %2").arg(root.shellModel.activePane + 1).arg(root.shellModel.paneCount)
                color: root.secondaryTextColor
                font.pixelSize: root.shellTheme.metaFontPixelSize
            }
        }
    }
}

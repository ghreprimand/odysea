// OdySea shell foundation. Navigation, selection, filtering, tabs, panes, and
// filesystem-operation requests expose matching pointer and keyboard paths.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

pragma ComponentBehavior: Bound

ApplicationWindow {
    id: root

    required property var shellModel

    readonly property int rowHeight: 34
    readonly property color backgroundColor: "#16130f"
    readonly property color panelColor: "#201b16"
    readonly property color borderColor: "#3a3128"
    readonly property color primaryTextColor: "#e8e2d6"
    readonly property color secondaryTextColor: "#a99f91"
    readonly property color accentColor: "#ffb454"
    readonly property color selectionColor: "#49321f"

    width: 1100
    height: 720
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: root.shellModel.path.length > 0 ? root.shellModel.path + " — OdySea" : "OdySea"
    color: backgroundColor

    component ShellButton: Button {
        id: control
        implicitHeight: 32
        leftPadding: 11
        rightPadding: 11

        contentItem: Text {
            text: control.text
            color: control.enabled ? root.primaryTextColor : "#6f675e"
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: control.down ? "#4a392b" : (control.hovered ? "#382b22" : root.panelColor)
            border.color: control.activeFocus ? root.accentColor : root.borderColor
            radius: 5
        }
    }

    component DirectoryPane: FocusScope {
        id: pane

        Rectangle {
            anchors.fill: parent
            color: root.backgroundColor
            border.color: directoryList.activeFocus ? root.accentColor : root.borderColor
            radius: 6
        }

        ListView {
            id: directoryList
            anchors.fill: parent
            anchors.margins: 4
            model: root.shellModel
            clip: true
            focus: true
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: root.shellModel.currentIndex
            highlightMoveDuration: 60

            Keys.onPressed: event => {
                const extend = (event.modifiers & Qt.ShiftModifier) !== 0
                const preserve = (event.modifiers & Qt.ControlModifier) !== 0
                if (event.key === Qt.Key_Up) {
                    root.shellModel.moveCursor(-1, extend, preserve)
                } else if (event.key === Qt.Key_Down) {
                    root.shellModel.moveCursor(1, extend, preserve)
                } else if (event.key === Qt.Key_Home) {
                    root.shellModel.moveCursorTo(0, extend, preserve)
                } else if (event.key === Qt.Key_End) {
                    root.shellModel.moveCursorTo(directoryList.count - 1, extend, preserve)
                } else if (event.key === Qt.Key_Space) {
                    root.shellModel.toggleCurrent()
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.shellModel.activate(root.shellModel.currentIndex)
                } else if (event.key === Qt.Key_Escape) {
                    root.shellModel.clearSelection()
                } else {
                    return
                }
                event.accepted = true
                directoryList.positionViewAtIndex(root.shellModel.currentIndex,
                                                  ListView.Contain)
            }

            delegate: Item {
                id: entryRow

                required property int index
                required property string name
                required property bool isDir
                required property double size
                required property bool selected

                width: directoryList.width
                height: root.rowHeight
                z: 1

                Rectangle {
                    anchors.fill: parent
                    color: entryRow.selected ? root.selectionColor
                                             : (rowPointer.containsMouse ? "#28211b" : "transparent")
                    border.color: root.shellModel.currentIndex === entryRow.index
                                  ? root.accentColor : "transparent"
                    radius: 4
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    Text {
                        text: entryRow.isDir ? "\u25B8" : "\u2022"
                        color: entryRow.isDir ? root.accentColor : "#7a7266"
                        font.pixelSize: 14
                    }

                    Text {
                        Layout.fillWidth: true
                        text: entryRow.name
                        color: root.primaryTextColor
                        elide: Text.ElideRight
                        font.family: "monospace"
                        font.pixelSize: 14
                    }

                    Text {
                        visible: !entryRow.isDir
                        text: root.formatSize(entryRow.size)
                        color: root.secondaryTextColor
                        font.pixelSize: 12
                    }
                }

                MouseArea {
                    id: rowPointer
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true

                    onClicked: mouse => {
                        directoryList.forceActiveFocus()
                        root.shellModel.selectRow(entryRow.index, mouse.modifiers)
                        if (mouse.button === Qt.RightButton) {
                            entryMenu.popup()
                        }
                    }

                    onDoubleClicked: mouse => {
                        if (mouse.button === Qt.LeftButton) {
                            root.shellModel.activate(entryRow.index)
                        }
                    }
                }

                Menu {
                    id: entryMenu

                    MenuItem {
                        text: entryRow.isDir ? qsTr("Open folder") : qsTr("Open")
                        onTriggered: root.shellModel.activate(entryRow.index)
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Copy")
                        onTriggered: root.shellModel.requestCopy()
                    }
                    MenuItem {
                        text: qsTr("Move")
                        onTriggered: root.shellModel.requestMove()
                    }
                    MenuItem {
                        text: qsTr("Rename")
                        onTriggered: root.shellModel.requestRename()
                    }
                    MenuItem {
                        text: qsTr("Move to Trash")
                        onTriggered: root.shellModel.requestTrash()
                    }
                }
            }

            MouseArea {
                id: rubberBandPointer

                property real originX: 0
                property real originY: 0

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                z: 0

                function rowAt(viewY) {
                    if (directoryList.count === 0) {
                        return -1
                    }
                    return Math.max(0, Math.min(directoryList.count - 1,
                                                Math.floor((directoryList.contentY + viewY)
                                                           / root.rowHeight)))
                }

                onPressed: mouse => {
                    originX = mouse.x
                    originY = mouse.y
                    rubberBand.visible = false
                    root.shellModel.beginRubberBand(
                                (mouse.modifiers & Qt.ControlModifier) !== 0)
                    directoryList.forceActiveFocus()
                }

                onPositionChanged: mouse => {
                    if (!pressed) {
                        return
                    }
                    rubberBand.visible = true
                    rubberBand.x = Math.min(originX, mouse.x)
                    rubberBand.y = Math.min(originY, mouse.y)
                    rubberBand.width = Math.abs(mouse.x - originX)
                    rubberBand.height = Math.abs(mouse.y - originY)
                    root.shellModel.updateRubberBand(rowAt(originY), rowAt(mouse.y))
                }

                onReleased: {
                    rubberBand.visible = false
                    root.shellModel.endRubberBand()
                }

                onCanceled: {
                    rubberBand.visible = false
                    root.shellModel.endRubberBand()
                }
            }

            Rectangle {
                id: rubberBand
                visible: false
                z: 3
                color: "#335f87b2"
                border.color: root.accentColor
                radius: 3
            }

            ScrollBar.vertical: ScrollBar {}
        }
    }

    function formatSize(bytes) {
        if (bytes < 1024) {
            return bytes + " B"
        }
        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(1) + " KiB"
        }
        if (bytes < 1024 * 1024 * 1024) {
            return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        }
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
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
        sequence: "Ctrl+Shift+S"
        onActivated: root.shellModel.sortMode = (root.shellModel.sortMode + 1) % 3
    }
    Shortcut {
        sequence: "Ctrl+L"
        onActivated: {
            addressField.forceActiveFocus()
            addressField.selectAll()
        }
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            filterField.forceActiveFocus()
            filterField.selectAll()
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
                        root.shellModel.path = text
                        text = root.shellModel.path
                        focus = false
                    }

                    background: Rectangle {
                        color: root.backgroundColor
                        border.color: addressField.activeFocus ? root.accentColor
                                                               : root.borderColor
                        radius: 5
                    }
                }

                Connections {
                    target: root.shellModel

                    function onPathChanged() {
                        if (!addressField.activeFocus) {
                            addressField.text = root.shellModel.path
                        }
                    }
                }

                ShellButton {
                    text: root.shellModel.paneCount === 2 ? qsTr("1 pane") : qsTr("2 panes")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Toggle pane workspace (Ctrl+Shift+P)")
                    onClicked: root.shellModel.setDualPaneEnabled(root.shellModel.paneCount === 1)
                }
            }
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
                            width: Math.max(110, tabs.width / Math.max(1, root.shellModel.tabCount))
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
                    Layout.preferredWidth: 260
                    placeholderText: qsTr("Filter this folder (Ctrl+F)")
                    color: root.primaryTextColor
                    selectByMouse: true
                    onTextEdited: root.shellModel.filterText = text

                    background: Rectangle {
                        color: root.panelColor
                        border.color: filterField.activeFocus ? root.accentColor
                                                              : root.borderColor
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

                Item {
                    Layout.fillWidth: true
                }

                ShellButton {
                    text: qsTr("Copy")
                    enabled: root.shellModel.selectedCount > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Copy selection (Ctrl+C)")
                    onClicked: root.shellModel.requestCopy()
                }
                ShellButton {
                    text: qsTr("Move")
                    enabled: root.shellModel.selectedCount > 0
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Move selection (Ctrl+X)")
                    onClicked: root.shellModel.requestMove()
                }
                ShellButton {
                    text: qsTr("Rename")
                    enabled: root.shellModel.selectedCount === 1
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Rename selection (F2)")
                    onClicked: root.shellModel.requestRename()
                }
                ShellButton {
                    text: qsTr("Trash")
                    enabled: root.shellModel.selectedCount > 0
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
            visible: root.shellModel.paneCount === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            active: root.shellModel.paneCount === 2 && root.shellModel.activePane === 1
            sourceComponent: DirectoryPane {}
        }
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
                visible: root.shellModel.busy
                running: visible
                implicitWidth: 20
                implicitHeight: 20
            }

            Text {
                Layout.fillWidth: true
                text: root.shellModel.errorString.length > 0
                      ? qsTr("Could not read folder: ") + root.shellModel.errorString
                      : root.shellModel.statusMessage
                color: root.shellModel.errorString.length > 0 ? "#ff8f7a"
                                                              : root.secondaryTextColor
                elide: Text.ElideRight
                font.pixelSize: 12
            }

            Text {
                text: qsTr("%1 selected").arg(root.shellModel.selectedCount)
                color: root.secondaryTextColor
                font.pixelSize: 12
            }

            Text {
                text: qsTr("Pane %1 of %2").arg(root.shellModel.activePane + 1)
                      .arg(root.shellModel.paneCount)
                color: root.secondaryTextColor
                font.pixelSize: 12
            }
        }
    }
}

// Virtualized Miller/columns navigation. The controller owns only the live
// path chain; each vertical ListView creates delegates only for its viewport.
// Selecting a folder reveals its children to the right, while activation
// enters that column or opens a file.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: columns

    required property var columnsModel
    required property var theme

    readonly property real columnWidth: Math.max(220 * theme.uiScale, Math.min(320 * theme.uiScale, width / 3))

    function revealActive() {
        if (columnsModel.columnCount > 0) {
            columnStrip.positionViewAtIndex(columnsModel.activeColumn, ListView.Contain);
        }
    }

    function focusView() {
        columns.forceActiveFocus();
        revealActive();
    }

    function revealCurrent() {
        revealActive();
    }

    focus: visible
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Folder columns")
    Accessible.description: qsTr("Use left and right to move between folder levels, and up and down to move within a level")
    Accessible.focusable: true
    Accessible.focused: activeFocus

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier) !== 0) {
            columns.columnsModel.collapseBack();
        } else if (event.key === Qt.Key_Up) {
            columns.columnsModel.moveWithin(-1);
        } else if (event.key === Qt.Key_Down) {
            columns.columnsModel.moveWithin(1);
        } else if (event.key === Qt.Key_PageUp) {
            columns.columnsModel.moveWithin(-Math.max(1, Math.floor(columns.height / columns.theme.rowHeight)));
        } else if (event.key === Qt.Key_PageDown) {
            columns.columnsModel.moveWithin(Math.max(1, Math.floor(columns.height / columns.theme.rowHeight)));
        } else if (event.key === Qt.Key_Home) {
            columns.columnsModel.moveToRow(0);
        } else if (event.key === Qt.Key_End) {
            columns.columnsModel.moveToRow(columns.columnsModel.entryCount(columns.columnsModel.activeColumn) - 1);
        } else if (event.key === Qt.Key_Left) {
            columns.columnsModel.moveAcross(-1);
        } else if (event.key === Qt.Key_Right) {
            if (columns.columnsModel.activeColumn + 1 < columns.columnsModel.columnCount) {
                columns.columnsModel.moveAcross(1);
            } else if (columns.columnsModel.currentEntryIsDirectory()) {
                columns.columnsModel.activateCurrent();
            }
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            columns.columnsModel.activateCurrent();
        } else if (event.key === Qt.Key_Backspace) {
            columns.columnsModel.collapseBack();
        } else {
            return;
        }
        event.accepted = true;
        columns.revealActive();
    }

    Connections {
        target: columns.columnsModel

        function onActiveColumnChanged() {
            columns.revealActive();
        }

        function onColumnCountChanged() {
            columns.revealActive();
        }
    }

    ListView {
        id: columnStrip

        objectName: "millerColumnStrip"
        anchors.fill: parent
        anchors.margins: 4
        orientation: ListView.Horizontal
        model: columns.columnsModel.columns
        clip: true
        spacing: 6
        boundsBehavior: Flickable.StopAtBounds
        reuseItems: true
        cacheBuffer: 0

        delegate: Rectangle {
            id: column

            required property int index
            required property var listingModel
            required property string columnPath
            required property string columnTitle
            required property bool active
            required property int depth

            objectName: "millerColumn-" + index
            width: columns.columnWidth
            height: columnStrip.height
            color: columns.theme.panel
            border.color: active ? columns.theme.accent : columns.theme.border
            border.width: active ? 2 : 1
            radius: 5

            function revealCurrent() {
                if (listingModel !== undefined && listingModel !== null && listingModel.currentIndex >= 0) {
                    entryList.positionViewAtIndex(listingModel.currentIndex, ListView.Contain);
                }
            }

            onActiveChanged: {
                if (active) {
                    revealCurrent();
                }
            }
            Component.onCompleted: {
                if (active) {
                    revealCurrent();
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    objectName: "millerHeader-" + column.index
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(40 * columns.theme.uiScale, columns.theme.captionFontPixelSize + 18)
                    color: column.active ? columns.theme.selectionBed : columns.theme.backgroundDeep
                    border.color: columns.theme.border

                    RowLayout {
                        z: 1
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 5
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: column.columnTitle
                            color: column.active ? columns.theme.text : columns.theme.textMuted
                            font.family: columns.theme.pathFontFamily
                            font.pixelSize: columns.theme.captionFontPixelSize
                            font.bold: column.active
                            elide: Text.ElideMiddle
                            Accessible.ignored: true
                        }

                        BusyIndicator {
                            objectName: "millerBusy-" + column.index
                            Layout.preferredWidth: 20 * columns.theme.uiScale
                            Layout.preferredHeight: 20 * columns.theme.uiScale
                            running: column.listingModel !== undefined && column.listingModel !== null && column.listingModel.busy
                            visible: running
                            Accessible.name: qsTr("Loading %1").arg(column.columnTitle)
                        }

                        ShellButton {
                            objectName: "millerCollapse-" + column.index
                            visible: column.index > 0
                            theme: columns.theme
                            iconName: "back"
                            text: ""
                            Accessible.name: qsTr("Collapse %1 and return to the previous column").arg(column.columnTitle)
                            onClicked: {
                                columns.columnsModel.collapseTo(column.index - 1);
                                columns.focusView();
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onClicked: {
                            columns.columnsModel.setActiveColumn(column.index);
                            columns.focusView();
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    visible: column.listingModel !== undefined && column.listingModel !== null && column.listingModel.errorString.length > 0
                    text: column.listingModel === undefined || column.listingModel === null ? "" : column.listingModel.errorString
                    color: columns.theme.danger
                    font.family: columns.theme.captionFontFamily
                    font.pixelSize: columns.theme.captionFontPixelSize
                    wrapMode: Text.Wrap
                }

                ListView {
                    id: entryList

                    objectName: "millerEntryList-" + column.index
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: column.listingModel === undefined ? null : column.listingModel
                    clip: true
                    reuseItems: true
                    cacheBuffer: 0
                    boundsBehavior: Flickable.StopAtBounds
                    currentIndex: column.listingModel === undefined || column.listingModel === null ? -1 : column.listingModel.currentIndex

                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Contents of %1").arg(column.columnPath)
                    Accessible.description: qsTr("Folder level %1").arg(column.depth + 1)
                    Accessible.focusable: true
                    Accessible.focused: columns.activeFocus && column.active

                    Connections {
                        target: column.listingModel === undefined ? null : column.listingModel

                        function onCurrentIndexChanged() {
                            column.revealCurrent();
                        }
                    }

                    delegate: Rectangle {
                        id: row

                        required property int index
                        required property string name
                        required property bool isDir
                        required property bool isSymlink
                        required property double size
                        required property bool selected
                        required property bool recoveryEntry
                        required property string entryPath

                        readonly property string kindLabel: isSymlink ? qsTr("Symbolic link") : (isDir ? qsTr("Folder") : qsTr("File"))

                        objectName: "millerRow-" + column.index + "-" + index
                        width: entryList.width
                        height: columns.theme.rowHeight
                        color: selected ? columns.theme.selectionBed : (rowPointer.containsMouse ? columns.theme.hover : "transparent")
                        border.color: selected ? columns.theme.accent : "transparent"
                        border.width: selected ? 1 : 0
                        radius: 3

                        Accessible.role: Accessible.ListItem
                        Accessible.name: qsTr("%1: %2").arg(kindLabel).arg(name)
                        Accessible.description: recoveryEntry ? qsTr("Recovery entry") : (isDir ? qsTr("Opens a child column") : qsTr("Double-click or press Enter to open"))
                        Accessible.focusable: true
                        Accessible.focused: columns.activeFocus && column.active && column.listingModel.currentIndex === index
                        Accessible.selectable: true
                        Accessible.selected: selected

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 7

                            VectorIcon {
                                Layout.preferredWidth: 18 * columns.theme.uiScale
                                Layout.preferredHeight: 18 * columns.theme.uiScale
                                name: row.isSymlink ? "symlink" : (row.isDir ? "folder" : "file")
                                ink: row.isDir ? columns.theme.dirInk : (row.isSymlink ? columns.theme.linkInk : columns.theme.iconInk)
                                highContrast: columns.theme.highContrast
                            }

                            Text {
                                Layout.fillWidth: true
                                text: row.name
                                color: row.selected ? columns.theme.text : columns.theme.textMuted
                                font.family: columns.theme.contentFontFamily
                                font.pixelSize: columns.theme.contentFontPixelSize
                                font.bold: row.selected
                                elide: Text.ElideMiddle
                                verticalAlignment: Text.AlignVCenter
                                Accessible.ignored: true
                            }

                            VectorIcon {
                                visible: row.isDir
                                Layout.preferredWidth: 14 * columns.theme.uiScale
                                Layout.preferredHeight: 14 * columns.theme.uiScale
                                name: "forward"
                                ink: columns.theme.textFaint
                                highContrast: columns.theme.highContrast
                            }
                        }

                        MouseArea {
                            id: rowPointer

                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            hoverEnabled: true
                            onClicked: {
                                columns.columnsModel.select(column.index, row.index);
                                columns.focusView();
                            }
                            onDoubleClicked: {
                                columns.columnsModel.activate(column.index, row.index);
                                columns.focusView();
                            }
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                }
            }

            Accessible.role: Accessible.List
            Accessible.name: qsTr("Column %1, %2").arg(depth + 1).arg(columnPath)
            Accessible.description: active ? qsTr("Current folder level") : qsTr("Folder level")
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }
}

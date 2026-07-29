pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: pane

    required property var shellModel
    required property var navigationController
    required property int rowHeight
    required property color accentColor
    required property color primaryTextColor
    required property color secondaryTextColor
    required property color selectionColor

    readonly property int selectionGutterWidth: 28
    property alias contentY: directoryList.contentY
    property alias interactive: directoryList.interactive
    property alias count: directoryList.count

    function cancelFlick() {
        directoryList.cancelFlick();
    }

    function focusView() {
        directoryList.forceActiveFocus();
    }

    function positionViewAtBeginning() {
        directoryList.positionViewAtBeginning();
    }

    function revealCurrent() {
        directoryList.revealCurrent();
    }

    component RubberBandArea: MouseArea {
        id: bandPointer

        required property var listView
        required property var selectionRectangle
        required property var selectionModel
        required property int selectionRowHeight
        property real originX: 0
        property real originContentY: 0

        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        preventStealing: true

        function rowsIntersectingBand(firstContentY, secondContentY) {
            const rows = [];
            const contentTop = Math.min(firstContentY, secondContentY);
            const contentBottom = Math.max(firstContentY, secondContentY);
            const entriesBottom = listView.count * selectionRowHeight;
            if (listView.count === 0 || contentBottom <= 0 || contentTop >= entriesBottom) {
                return rows;
            }
            const firstRow = Math.max(0, Math.floor(contentTop / selectionRowHeight));
            const lastRow = Math.min(listView.count - 1, Math.ceil(contentBottom / selectionRowHeight) - 1);
            for (let row = firstRow; row <= lastRow; ++row) {
                rows.push(row);
            }
            return rows;
        }

        onPressed: mouse => {
            originX = mouse.x;
            originContentY = listView.contentY + bandPointer.y + mouse.y;
            selectionRectangle.visible = false;
            selectionModel.beginRubberBand((mouse.modifiers & Qt.ControlModifier) !== 0);
            listView.forceActiveFocus();
        }
        onPositionChanged: mouse => {
            if (!pressed) {
                return;
            }
            const pointerContentY = listView.contentY + bandPointer.y + mouse.y;
            selectionRectangle.visible = true;
            selectionRectangle.x = bandPointer.x + Math.min(originX, mouse.x);
            selectionRectangle.y = Math.min(originContentY, pointerContentY) - listView.contentY;
            selectionRectangle.width = Math.abs(mouse.x - originX);
            selectionRectangle.height = Math.abs(pointerContentY - originContentY);
            const rows = rowsIntersectingBand(originContentY, pointerContentY);
            const currentRow = rows.length === 0 ? -1 : (pointerContentY >= originContentY ? rows[rows.length - 1] : rows[0]);
            selectionModel.updateRubberBandSelection(rows, currentRow);
        }
        onReleased: {
            selectionRectangle.visible = false;
            selectionModel.endRubberBand();
        }
        onCanceled: {
            selectionRectangle.visible = false;
            selectionModel.endRubberBand();
        }
    }

    ListView {
        id: directoryList

        objectName: "directoryList"
        anchors.fill: parent
        anchors.margins: 4
        model: pane.shellModel
        clip: true
        focus: pane.visible
        boundsBehavior: Flickable.StopAtBounds
        currentIndex: pane.shellModel.currentIndex
        highlightMoveDuration: 60

        function revealCurrent() {
            if (pane.shellModel.currentIndex >= 0) {
                directoryList.positionViewAtIndex(pane.shellModel.currentIndex, ListView.Contain);
            }
        }

        function openCurrentContextMenu() {
            const row = pane.shellModel.currentIndex;
            if (row < 0) {
                return;
            }
            if (pane.shellModel.rowSelected(row)) {
                pane.shellModel.moveCursorTo(row, false, true);
            } else {
                pane.shellModel.selectRow(row, Qt.NoModifier);
            }
            keyboardContextMenu.openFor(row, pane.shellModel.rowIsDirectory(row));
        }

        Keys.onPressed: event => {
            if (pane.navigationController.handleTypeAhead(event, directoryList)) {
                event.accepted = true;
                return;
            }
            const extend = (event.modifiers & Qt.ShiftModifier) !== 0;
            const preserve = (event.modifiers & Qt.ControlModifier) !== 0;
            if (event.key === Qt.Key_Up) {
                pane.shellModel.moveCursor(-1, extend, preserve);
            } else if (event.key === Qt.Key_Down) {
                pane.shellModel.moveCursor(1, extend, preserve);
            } else if (event.key === Qt.Key_PageUp) {
                pane.shellModel.moveCursor(-Math.max(1, Math.floor(directoryList.height / pane.rowHeight)), extend, preserve);
            } else if (event.key === Qt.Key_PageDown) {
                pane.shellModel.moveCursor(Math.max(1, Math.floor(directoryList.height / pane.rowHeight)), extend, preserve);
            } else if (event.key === Qt.Key_Home) {
                pane.shellModel.moveCursorTo(0, extend, preserve);
            } else if (event.key === Qt.Key_End) {
                pane.shellModel.moveCursorTo(directoryList.count - 1, extend, preserve);
            } else if (event.key === Qt.Key_Space) {
                pane.shellModel.toggleCurrent();
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                pane.shellModel.activateCurrent();
            } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier) !== 0)) {
                directoryList.openCurrentContextMenu();
            } else if (event.key === Qt.Key_Escape) {
                pane.shellModel.clearSelection();
            } else {
                return;
            }
            pane.navigationController.clearTypeAhead();
            event.accepted = true;
            directoryList.revealCurrent();
        }

        delegate: Item {
            id: entryRow

            objectName: "entryRow-" + index
            required property int index
            required property string name
            required property bool isDir
            required property double size
            required property bool selected
            required property bool recoveryEntry
            required property string entryPath
            readonly property var entryContextMenu: entryMenu
            readonly property var dragMimeData: ({
                    "text/uri-list": pane.shellModel.selectedFileUrls().join("\r\n") + "\r\n"
                })
            readonly property int dragProposedAction: Drag.proposedAction

            width: directoryList.width - pane.selectionGutterWidth
            height: pane.rowHeight
            z: 1

            function openContextMenu() {
                entryMenu.openFor(index, isDir);
            }

            function dropSelectedEntries(action) {
                return pane.shellModel.dropSelection(entryPath, action === Qt.MoveAction, 0);
            }

            Drag.active: rowPointer.fileDragging
            Drag.dragType: Drag.Automatic
            Drag.keys: ["odysea-entry"]
            Drag.mimeData: dragMimeData
            Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
            Drag.proposedAction: (rowPointer.pressModifiers & Qt.ControlModifier) !== 0 ? Qt.CopyAction : Qt.MoveAction
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2

            Rectangle {
                anchors.fill: parent
                color: entryRow.selected ? pane.selectionColor : (rowPointer.containsMouse ? "#28211b" : "transparent")
                border.color: pane.shellModel.currentIndex === entryRow.index ? pane.accentColor : "transparent"
                radius: 4
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 10

                Text {
                    text: entryRow.isDir ? "\u25B8" : "\u2022"
                    color: entryRow.isDir ? pane.accentColor : "#7a7266"
                    font.pixelSize: 14
                }
                Text {
                    visible: entryRow.recoveryEntry
                    text: qsTr("RECOVERY")
                    color: "#ff8f7a"
                    font.bold: true
                    font.pixelSize: 10
                }
                Text {
                    Layout.fillWidth: true
                    text: entryRow.name
                    color: pane.primaryTextColor
                    elide: Text.ElideRight
                    font.family: "monospace"
                    font.pixelSize: 14
                }
                Text {
                    visible: !entryRow.isDir
                    text: pane.navigationController.formatSize(entryRow.size)
                    color: pane.secondaryTextColor
                    font.pixelSize: 12
                }
            }

            MouseArea {
                id: rowPointer

                property point pressPosition
                property int pressModifiers: Qt.NoModifier
                property bool fileDragging: false
                property bool suppressClick: false

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true

                onPressed: mouse => {
                    pressPosition = Qt.point(mouse.x, mouse.y);
                    pressModifiers = mouse.modifiers;
                    suppressClick = false;
                }
                onPositionChanged: mouse => {
                    if (!pressed || fileDragging) {
                        return;
                    }
                    const dx = mouse.x - pressPosition.x;
                    const dy = mouse.y - pressPosition.y;
                    if (Math.abs(dx) >= 12 && Math.abs(dx) > Math.abs(dy)) {
                        if (!entryRow.selected) {
                            pane.shellModel.selectRow(entryRow.index, Qt.NoModifier);
                        }
                        fileDragging = true;
                        suppressClick = true;
                        preventStealing = true;
                    }
                }
                onReleased: {
                    if (fileDragging) {
                        entryRow.Drag.drop();
                    }
                    fileDragging = false;
                    preventStealing = false;
                }
                onCanceled: {
                    fileDragging = false;
                    suppressClick = false;
                    preventStealing = false;
                }
                onClicked: mouse => {
                    if (suppressClick) {
                        suppressClick = false;
                        return;
                    }
                    pane.navigationController.clearTypeAhead();
                    directoryList.forceActiveFocus();
                    if (mouse.button === Qt.RightButton && entryRow.selected) {
                        pane.shellModel.moveCursorTo(entryRow.index, false, true);
                    } else {
                        pane.shellModel.selectRow(entryRow.index, mouse.modifiers);
                    }
                    directoryList.revealCurrent();
                    if (mouse.button === Qt.RightButton) {
                        entryRow.openContextMenu();
                    }
                }
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) {
                        pane.shellModel.activate(entryRow.index);
                    }
                }
            }

            EntryContextMenu {
                id: entryMenu

                objectName: "entryMenu-" + entryRow.index
                shellModel: pane.shellModel
                focusTarget: directoryList
            }

            DropArea {
                objectName: "entryDropTarget-" + entryRow.index
                anchors.fill: parent
                enabled: entryRow.isDir
                keys: ["odysea-entry"]
                onDropped: drop => {
                    if (entryRow.dropSelectedEntries(drop.proposedAction)) {
                        drop.acceptProposedAction();
                    }
                }
            }
        }

        RubberBandArea {
            id: blankBandPointer

            objectName: "rubberBandBlankArea"
            listView: directoryList
            selectionRectangle: rubberBand
            selectionModel: pane.shellModel
            selectionRowHeight: pane.rowHeight
            x: 0
            y: Math.max(0, Math.min(directoryList.height, directoryList.count * pane.rowHeight - directoryList.contentY))
            width: directoryList.width - verticalBar.width
            height: Math.max(0, directoryList.height - y)
            z: 0
        }

        RubberBandArea {
            id: gutterBandPointer

            objectName: "rubberBandGutter"
            listView: directoryList
            selectionRectangle: rubberBand
            selectionModel: pane.shellModel
            selectionRowHeight: pane.rowHeight
            x: directoryList.width - pane.selectionGutterWidth
            y: 0
            width: pane.selectionGutterWidth - verticalBar.width
            height: blankBandPointer.y
            z: 2
        }

        Rectangle {
            id: rubberBand

            visible: false
            z: 3
            color: "#335f87b2"
            border.color: pane.accentColor
            radius: 3
        }

        ScrollBar.vertical: ScrollBar {
            id: verticalBar

            width: 8
        }
    }

    EntryContextMenu {
        id: keyboardContextMenu

        objectName: "listKeyboardContextMenu"
        shellModel: pane.shellModel
        focusTarget: directoryList
    }
}

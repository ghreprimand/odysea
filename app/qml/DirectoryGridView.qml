pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: pane

    required property var shellModel
    required property var navigationController
    required property color backgroundColor
    required property color panelColor
    required property color borderColor
    required property color primaryTextColor
    required property color secondaryTextColor
    required property color accentColor
    required property color selectionColor

    readonly property int selectionGutterWidth: 28
    readonly property int cellWidth: 144
    readonly property int cellHeight: 154

    focus: visible

    function forceViewFocus() {
        directoryGrid.forceActiveFocus();
    }

    function revealCurrent() {
        directoryGrid.revealCurrent();
    }

    component GridBandArea: MouseArea {
        id: bandPointer

        required property var gridView
        required property var selectionRectangle
        required property var selectionModel
        property real originContentX: 0
        property real originContentY: 0

        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        preventStealing: true

        function rowsIntersectingBand(firstX, firstY, secondX, secondY) {
            const rows = [];
            if (gridView.count === 0) {
                return rows;
            }
            const left = Math.min(firstX, secondX);
            const right = Math.max(firstX, secondX);
            const top = Math.min(firstY, secondY);
            const bottom = Math.max(firstY, secondY);
            const columns = Math.max(1, Math.floor(gridView.width / pane.cellWidth));
            const firstColumn = Math.max(0, Math.floor(left / pane.cellWidth));
            const lastColumn = Math.min(columns - 1, Math.ceil(right / pane.cellWidth) - 1);
            const firstGridRow = Math.max(0, Math.floor(top / pane.cellHeight));
            const lastGridRow = Math.max(firstGridRow, Math.ceil(bottom / pane.cellHeight) - 1);
            for (let gridRow = firstGridRow; gridRow <= lastGridRow; ++gridRow) {
                for (let column = firstColumn; column <= lastColumn; ++column) {
                    const row = gridRow * columns + column;
                    if (row < gridView.count) {
                        rows.push(row);
                    }
                }
            }
            return rows;
        }

        function nearestRow(contentX, contentY) {
            if (gridView.count === 0) {
                return -1;
            }
            const columns = Math.max(1, Math.floor(gridView.width / pane.cellWidth));
            const column = Math.max(0, Math.min(columns - 1, Math.floor(contentX / pane.cellWidth)));
            const gridRow = Math.max(0, Math.floor(contentY / pane.cellHeight));
            return Math.min(gridView.count - 1, gridRow * columns + column);
        }

        onPressed: mouse => {
            originContentX = gridView.contentX + bandPointer.x - gridView.x + mouse.x;
            originContentY = gridView.contentY + bandPointer.y - gridView.y + mouse.y;
            selectionRectangle.visible = false;
            selectionModel.beginRubberBand((mouse.modifiers & Qt.ControlModifier) !== 0);
            gridView.forceActiveFocus();
        }

        onPositionChanged: mouse => {
            if (!pressed) {
                return;
            }
            const pointerContentX = gridView.contentX + bandPointer.x - gridView.x + mouse.x;
            const pointerContentY = gridView.contentY + bandPointer.y - gridView.y + mouse.y;
            selectionRectangle.visible = true;
            selectionRectangle.x = gridView.x + Math.min(originContentX, pointerContentX) - gridView.contentX;
            selectionRectangle.y = gridView.y + Math.min(originContentY, pointerContentY) - gridView.contentY;
            selectionRectangle.width = Math.abs(pointerContentX - originContentX);
            selectionRectangle.height = Math.abs(pointerContentY - originContentY);
            const rows = rowsIntersectingBand(originContentX, originContentY, pointerContentX, pointerContentY);
            const currentRow = rows.length > 0 ? nearestRow(pointerContentX, pointerContentY) : -1;
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

    GridView {
        id: directoryGrid

        objectName: "directoryGrid"
        anchors.fill: parent
        anchors.margins: 4
        anchors.rightMargin: pane.selectionGutterWidth
        model: pane.shellModel
        clip: true
        focus: true
        boundsBehavior: Flickable.StopAtBounds
        cellWidth: pane.cellWidth
        cellHeight: pane.cellHeight
        cacheBuffer: Math.max(0, height * 2)
        currentIndex: pane.shellModel.currentIndex
        highlightMoveDuration: 60

        function columnCount() {
            return Math.max(1, Math.floor(directoryGrid.width / pane.cellWidth));
        }

        function moveHorizontal(direction, extend, preserve) {
            const current = Math.max(0, pane.shellModel.currentIndex);
            const columns = directoryGrid.columnCount();
            const column = current % columns;
            const candidate = current + direction;
            if ((direction < 0 && column > 0) || (direction > 0 && column + 1 < columns && candidate < directoryGrid.count)) {
                pane.shellModel.moveCursorTo(candidate, extend, preserve);
            }
        }

        function moveVertical(direction, rows, extend, preserve) {
            const columns = directoryGrid.columnCount();
            let target = Math.max(0, pane.shellModel.currentIndex);
            for (let step = 0; step < rows; ++step) {
                const candidate = target + direction * columns;
                if (candidate < 0 || candidate >= directoryGrid.count) {
                    break;
                }
                target = candidate;
            }
            pane.shellModel.moveCursorTo(target, extend, preserve);
        }

        function revealCurrent() {
            if (pane.shellModel.currentIndex >= 0) {
                directoryGrid.positionViewAtIndex(pane.shellModel.currentIndex, GridView.Contain);
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
            if (pane.navigationController.handleTypeAhead(event, directoryGrid)) {
                event.accepted = true;
                return;
            }
            const extend = (event.modifiers & Qt.ShiftModifier) !== 0;
            const preserve = (event.modifiers & Qt.ControlModifier) !== 0;
            if (event.key === Qt.Key_Left) {
                directoryGrid.moveHorizontal(-1, extend, preserve);
            } else if (event.key === Qt.Key_Right) {
                directoryGrid.moveHorizontal(1, extend, preserve);
            } else if (event.key === Qt.Key_Up) {
                directoryGrid.moveVertical(-1, 1, extend, preserve);
            } else if (event.key === Qt.Key_Down) {
                directoryGrid.moveVertical(1, 1, extend, preserve);
            } else if (event.key === Qt.Key_PageUp) {
                directoryGrid.moveVertical(-1, Math.max(1, Math.floor(directoryGrid.height / pane.cellHeight)), extend, preserve);
            } else if (event.key === Qt.Key_PageDown) {
                directoryGrid.moveVertical(1, Math.max(1, Math.floor(directoryGrid.height / pane.cellHeight)), extend, preserve);
            } else if (event.key === Qt.Key_Home) {
                pane.shellModel.moveCursorTo(0, extend, preserve);
            } else if (event.key === Qt.Key_End) {
                pane.shellModel.moveCursorTo(directoryGrid.count - 1, extend, preserve);
            } else if (event.key === Qt.Key_Space) {
                pane.shellModel.toggleCurrent();
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                pane.shellModel.activateCurrent();
            } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier) !== 0)) {
                directoryGrid.openCurrentContextMenu();
            } else if (event.key === Qt.Key_Escape) {
                pane.shellModel.clearSelection();
            } else {
                return;
            }
            pane.navigationController.clearTypeAhead();
            event.accepted = true;
            directoryGrid.revealCurrent();
        }

        delegate: Item {
            id: entryCell

            objectName: "entryCell-" + index
            required property int index
            required property string name
            required property bool isDir
            required property bool selected
            required property bool recoveryEntry
            required property string entryPath
            required property string thumbnailSource
            required property bool thumbnailLoading
            readonly property var entryContextMenu: entryMenu
            readonly property var dragMimeData: ({
                    "text/uri-list": pane.shellModel.selectedFileUrls().join("\r\n") + "\r\n"
                })
            readonly property int dragProposedAction: Drag.proposedAction

            width: pane.cellWidth
            height: pane.cellHeight
            z: 1

            Component.onCompleted: {
                if (pane.visible) {
                    pane.shellModel.requestThumbnail(index);
                }
            }
            Component.onDestruction: pane.shellModel.releaseThumbnail(entryPath)

            function openContextMenu() {
                entryMenu.openFor(index, isDir);
            }

            function dropSelectedEntries(action) {
                return pane.shellModel.dropSelection(entryPath, action === Qt.MoveAction, 0);
            }

            Drag.active: cellPointer.fileDragging
            Drag.dragType: Drag.Automatic
            Drag.keys: ["odysea-entry"]
            Drag.mimeData: dragMimeData
            Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
            Drag.proposedAction: (cellPointer.pressModifiers & Qt.ControlModifier) !== 0 ? Qt.CopyAction : Qt.MoveAction
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2

            Connections {
                target: pane

                function onVisibleChanged() {
                    if (pane.visible) {
                        pane.shellModel.requestThumbnail(entryCell.index);
                    } else {
                        pane.shellModel.releaseThumbnail(entryCell.entryPath);
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                color: entryCell.selected ? pane.selectionColor : (cellPointer.containsMouse ? "#28211b" : "transparent")
                border.color: pane.shellModel.currentIndex === entryCell.index ? pane.accentColor : "transparent"
                radius: 6
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 104

                    Image {
                        id: thumbnail

                        anchors.fill: parent
                        source: entryCell.thumbnailSource
                        sourceSize.width: 112
                        sourceSize.height: 96
                        asynchronous: true
                        cache: false
                        fillMode: Image.PreserveAspectFit
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: thumbnail.status !== Image.Ready && !entryCell.thumbnailLoading
                        text: entryCell.isDir ? "\u25B8" : "\u2022"
                        color: entryCell.isDir ? pane.accentColor : "#7a7266"
                        font.pixelSize: 34
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        visible: entryCell.thumbnailLoading
                        running: visible
                        implicitWidth: 28
                        implicitHeight: 28
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: entryCell.name
                    color: pane.primaryTextColor
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignHCenter
                    font.family: "monospace"
                    font.pixelSize: 13
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: entryCell.recoveryEntry
                    text: qsTr("RECOVERY")
                    color: "#ff8f7a"
                    font.bold: true
                    font.pixelSize: 9
                }
            }

            MouseArea {
                id: cellPointer

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
                        if (!entryCell.selected) {
                            pane.shellModel.selectRow(entryCell.index, Qt.NoModifier);
                        }
                        fileDragging = true;
                        suppressClick = true;
                        preventStealing = true;
                    }
                }
                onReleased: {
                    if (fileDragging) {
                        entryCell.Drag.drop();
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
                    directoryGrid.forceActiveFocus();
                    if (mouse.button === Qt.RightButton && entryCell.selected) {
                        pane.shellModel.moveCursorTo(entryCell.index, false, true);
                    } else {
                        pane.shellModel.selectRow(entryCell.index, mouse.modifiers);
                    }
                    directoryGrid.revealCurrent();
                    if (mouse.button === Qt.RightButton) {
                        entryCell.openContextMenu();
                    }
                }

                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) {
                        pane.shellModel.activate(entryCell.index);
                    }
                }
            }

            EntryContextMenu {
                id: entryMenu

                objectName: "gridEntryMenu-" + entryCell.index
                shellModel: pane.shellModel
                focusTarget: directoryGrid
            }

            DropArea {
                objectName: "gridEntryDropTarget-" + entryCell.index
                anchors.fill: parent
                enabled: entryCell.isDir
                keys: ["odysea-entry"]
                onDropped: drop => {
                    if (entryCell.dropSelectedEntries(drop.proposedAction)) {
                        drop.acceptProposedAction();
                    }
                }
            }
        }

        ScrollBar.vertical: ScrollBar {
            width: 8
        }
    }

    GridBandArea {
        id: blankBandPointer

        objectName: "gridRubberBandBlankArea"
        gridView: directoryGrid
        selectionRectangle: rubberBand
        selectionModel: pane.shellModel
        x: directoryGrid.x
        y: directoryGrid.y + Math.max(0, Math.min(directoryGrid.height, Math.ceil(directoryGrid.count / Math.max(1, Math.floor(directoryGrid.width / pane.cellWidth))) * pane.cellHeight - directoryGrid.contentY))
        width: directoryGrid.width
        height: Math.max(0, directoryGrid.y + directoryGrid.height - y)
        z: 2
    }

    GridBandArea {
        id: gutterBandPointer

        objectName: "gridRubberBandGutter"
        gridView: directoryGrid
        selectionRectangle: rubberBand
        selectionModel: pane.shellModel
        x: directoryGrid.x + directoryGrid.width
        y: directoryGrid.y
        width: pane.selectionGutterWidth - 4
        height: directoryGrid.height
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

    EntryContextMenu {
        id: keyboardContextMenu

        objectName: "gridKeyboardContextMenu"
        shellModel: pane.shellModel
        focusTarget: directoryGrid
    }
}

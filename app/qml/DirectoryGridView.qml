pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: pane

    required property var shellModel
    required property var navigationController
    /// The hosting pane's shared context menu; every entry, selection,
    /// and blank-canvas invocation in this view parameterizes it.
    required property var actionMenu
    required property color backgroundColor
    required property color panelColor
    required property color borderColor
    required property color primaryTextColor
    required property color secondaryTextColor
    required property color accentColor
    required property color selectionColor

    // Optional roles with the shell's former fixed values as defaults, so the
    // view renders sensibly when a scene does not bind a theme.
    property color dirInkColor: accentColor
    property color fileInkColor: "#7a7266"
    property color linkInkColor: "#7096b8"
    property color iconInkColor: secondaryTextColor
    property color dangerColor: "#ff8f7a"
    property color hoverColor: "#28211b"
    property color rubberBandColor: "#335f87b2"
    property string entryFontFamily: "monospace"
    property int entryFontPixelSize: 13
    property string captionFontFamily: "monospace"
    property int captionFontPixelSize: 12
    property bool highContrast: false
    property int cellWidth: 144
    property int cellHeight: 154
    /// How long the current-item ring persists when the cursor moves away.
    /// Zero renders instantly; the shell binds this to the presentation
    /// layer's motion token so reduced motion disables the decay.
    property int persistenceDurationMs: 0
    /// Optional protected-content mask layer. When bound, every loaded
    /// thumbnail registers its pixels as color-true wells the presentation
    /// pipeline must not process.
    property WellMaskLayer wellLayer: null

    readonly property int selectionGutterWidth: 28

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

        Accessible.role: Accessible.List
        Accessible.name: pane.navigationController.activeShellModel === pane.shellModel ? qsTr("Active directory grid") : qsTr("Inactive directory grid")
        Accessible.focusable: true
        Accessible.focused: activeFocus

        // Registered wells mirror scrolled positions only when told the
        // viewport moved.
        onContentXChanged: {
            if (pane.wellLayer !== null) {
                pane.wellLayer.bump();
            }
        }
        onContentYChanged: {
            if (pane.wellLayer !== null) {
                pane.wellLayer.bump();
            }
        }

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
            const registry = pane.actionMenu.registry;
            const row = pane.shellModel.currentIndex;
            if (row < 0) {
                pane.actionMenu.openFor(registry.canvasContext(pane.shellModel.path), directoryGrid, Qt.point(directoryGrid.width / 2, directoryGrid.height / 2), directoryGrid);
                return;
            }
            if (pane.shellModel.rowSelected(row)) {
                pane.shellModel.moveCursorTo(row, false, true);
            } else {
                pane.shellModel.selectRow(row, Qt.NoModifier);
            }
            directoryGrid.revealCurrent();
            const context = registry.entryContext(row, pane.shellModel.rowIsDirectory(row), Math.max(1, pane.shellModel.selectedCount));
            const delegate = directoryGrid.itemAtIndex(row);
            if (delegate !== null) {
                pane.actionMenu.openFor(context, delegate, Qt.point(delegate.width / 2, delegate.height / 2), directoryGrid);
            } else {
                pane.actionMenu.openFor(context, directoryGrid, Qt.point(directoryGrid.width / 2, directoryGrid.height / 2), directoryGrid);
            }
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
            required property bool isSymlink
            required property bool selected
            required property bool recoveryEntry
            required property string entryPath
            required property string thumbnailSource
            required property bool thumbnailLoading
            readonly property var entryContextMenu: pane.actionMenu
            readonly property var dragMimeData: ({
                    "text/uri-list": pane.shellModel.selectedFileUrls.length > 0 ? pane.shellModel.selectedFileUrls.join("\r\n") + "\r\n" : ""
                })
            readonly property int dragProposedAction: Drag.proposedAction
            readonly property bool dragActive: Drag.active
            readonly property string accessibleKind: entryCell.isSymlink ? qsTr("Symbolic link") : (entryCell.isDir ? qsTr("Folder") : qsTr("File"))
            signal transferDragStarted(int action)

            width: pane.cellWidth
            height: pane.cellHeight
            z: 1

            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("%1: %2").arg(entryCell.accessibleKind).arg(entryCell.name)
            Accessible.description: entryCell.recoveryEntry ? qsTr("Recovery entry") : ""
            Accessible.focusable: true
            Accessible.focused: directoryGrid.activeFocus && pane.shellModel.currentIndex === entryCell.index
            Accessible.selectable: true
            Accessible.selected: entryCell.selected

            Component.onCompleted: {
                if (pane.visible) {
                    pane.shellModel.requestThumbnail(index);
                }
            }
            Component.onDestruction: pane.shellModel.releaseThumbnail(entryPath)

            function openContextMenu(position) {
                const registry = pane.actionMenu.registry;
                pane.actionMenu.openFor(registry.entryContext(entryCell.index, entryCell.isDir, Math.max(1, pane.shellModel.selectedCount)), entryCell, position, directoryGrid);
            }

            function dragActionForModifiers(modifiers) {
                if ((modifiers & Qt.ShiftModifier) !== 0) {
                    return Qt.MoveAction;
                }
                return (modifiers & Qt.ControlModifier) !== 0 ? Qt.CopyAction : Qt.MoveAction;
            }

            function dropSelectedEntries(action) {
                return pane.shellModel.dropSelection(entryPath, action === Qt.MoveAction, 0);
            }

            Drag.active: cellPointer.fileDragging
            Drag.dragType: Drag.Automatic
            Drag.keys: ["odysea-entry"]
            Drag.mimeData: dragMimeData
            Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
            Drag.proposedAction: dragActionForModifiers(cellPointer.pressModifiers)
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
                color: entryCell.selected ? pane.selectionColor : (cellPointer.containsMouse ? pane.hoverColor : "transparent")
                radius: 6
            }

            // Current-item ring with persistence decay: leaving a cell fades
            // the ring over the shared motion token, so a moving cursor
            // leaves a brief trail. A zero duration renders instantly.
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                color: "transparent"
                border.color: pane.accentColor
                radius: 6
                opacity: pane.shellModel.currentIndex === entryCell.index ? 1 : 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: pane.persistenceDurationMs
                    }
                }
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

                        objectName: "entryThumbnail-" + entryCell.index
                        anchors.fill: parent
                        source: entryCell.thumbnailSource
                        sourceSize.width: 112
                        sourceSize.height: 96
                        asynchronous: true
                        cache: false
                        fillMode: Image.PreserveAspectFit

                        // A loaded thumbnail is color-true content: register
                        // its pixels as a protected well so the presentation
                        // pipeline never blooms or bands them. The grid is
                        // the clipping viewport — cache-buffer delegates
                        // stay realized beyond it, and their mirrors must
                        // not escape onto surrounding chrome.
                        onStatusChanged: {
                            if (pane.wellLayer === null) {
                                return;
                            }
                            if (status === Image.Ready) {
                                pane.wellLayer.registerWell(thumbnail, directoryGrid);
                            } else {
                                pane.wellLayer.unregisterWell(thumbnail);
                            }
                        }
                        Component.onDestruction: {
                            if (pane.wellLayer !== null) {
                                pane.wellLayer.unregisterWell(thumbnail);
                            }
                        }
                    }

                    VectorIcon {
                        objectName: "entryCellIcon-" + entryCell.index
                        anchors.centerIn: parent
                        visible: thumbnail.status !== Image.Ready && !entryCell.thumbnailLoading
                        width: Math.max(34, pane.entryFontPixelSize * 2.4)
                        height: width
                        name: entryCell.isSymlink ? "symlink" : (entryCell.isDir ? "folder" : "file")
                        ink: entryCell.isSymlink ? pane.linkInkColor : (entryCell.isDir ? pane.dirInkColor : pane.fileInkColor)
                        highContrast: pane.highContrast
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
                    color: entryCell.isDir ? pane.dirInkColor : pane.primaryTextColor
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignHCenter
                    font.family: pane.entryFontFamily
                    font.pixelSize: pane.entryFontPixelSize
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: entryCell.recoveryEntry
                    text: qsTr("RECOVERY")
                    color: pane.dangerColor
                    font.bold: true
                    font.family: pane.captionFontFamily
                    font.pixelSize: Math.max(9, pane.captionFontPixelSize - 3)
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
                    if (mouse.button === Qt.RightButton) {
                        pane.navigationController.clearTypeAhead();
                        directoryGrid.forceActiveFocus();
                        if (entryCell.selected) {
                            pane.shellModel.moveCursorTo(entryCell.index, false, true);
                        } else {
                            pane.shellModel.selectRow(entryCell.index, mouse.modifiers);
                        }
                        directoryGrid.revealCurrent();
                        entryCell.openContextMenu(pressPosition);
                    }
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
                        entryCell.transferDragStarted(entryCell.dragProposedAction);
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
                    if (mouse.button === Qt.RightButton) {
                        return;
                    }
                    if (suppressClick) {
                        suppressClick = false;
                        return;
                    }
                    pane.navigationController.clearTypeAhead();
                    directoryGrid.forceActiveFocus();
                    pane.shellModel.selectRow(entryCell.index, mouse.modifiers);
                    directoryGrid.revealCurrent();
                }

                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) {
                        pane.shellModel.activate(entryCell.index);
                    }
                }
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

    // Blank-canvas context menu, pointer path. Mirrors the blank
    // rubber-band region's geometry instead of restating it; right button
    // only, so band selection below is untouched.
    MouseArea {
        id: blankContextPointer

        objectName: "gridBlankContextArea"
        acceptedButtons: Qt.RightButton
        x: blankBandPointer.x
        y: blankBandPointer.y
        width: blankBandPointer.width
        height: blankBandPointer.height
        z: 3
        onPressed: mouse => {
            pane.navigationController.clearTypeAhead();
            directoryGrid.forceActiveFocus();
            pane.actionMenu.openFor(pane.actionMenu.registry.canvasContext(pane.shellModel.path), blankContextPointer, Qt.point(mouse.x, mouse.y), directoryGrid);
        }
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
        color: pane.rubberBandColor
        border.color: pane.accentColor
        radius: 3
    }
}

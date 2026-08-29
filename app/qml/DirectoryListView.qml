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
    required property int rowHeight
    required property color accentColor
    required property color primaryTextColor
    required property color secondaryTextColor
    required property color selectionColor

    // Optional roles with the shell's former fixed values as defaults, so the
    // view renders sensibly when a scene does not bind a theme.
    property color dirInkColor: accentColor
    property color metaInkColor: secondaryTextColor
    property color fileInkColor: "#7a7266"
    property color linkInkColor: "#7096b8"
    property color iconInkColor: secondaryTextColor
    property color dangerColor: "#ff8f7a"
    property color hoverColor: "#28211b"
    property color rubberBandColor: "#335f87b2"
    property string entryFontFamily: "monospace"
    property int entryFontPixelSize: 14
    property string captionFontFamily: "monospace"
    property int captionFontPixelSize: 12
    property bool highContrast: false
    /// How long the current-row ring persists when the cursor moves away.
    /// Zero renders instantly; the shell binds this to the presentation
    /// layer's motion token so reduced motion disables the decay.
    property int persistenceDurationMs: 0
    /// One current selected row can emit. Other selected rows retain their
    /// bed only, so batch selection never scales the phosphor source.
    property bool glowEnabled: false

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

        Accessible.role: Accessible.List
        Accessible.name: pane.navigationController.activeShellModel === pane.shellModel ? qsTr("Active directory list") : qsTr("Inactive directory list")
        Accessible.focusable: true
        Accessible.focused: activeFocus

        function revealCurrent() {
            if (pane.shellModel.currentIndex >= 0) {
                directoryList.positionViewAtIndex(pane.shellModel.currentIndex, ListView.Contain);
            }
        }

        function openCurrentContextMenu() {
            const registry = pane.actionMenu.registry;
            const row = pane.shellModel.currentIndex;
            if (row < 0) {
                pane.actionMenu.openFor(registry.canvasContext(pane.shellModel.path), directoryList, Qt.point(pane.selectionGutterWidth, 8), directoryList);
                return;
            }
            if (pane.shellModel.rowSelected(row)) {
                pane.shellModel.moveCursorTo(row, false, true);
            } else {
                pane.shellModel.selectRow(row, Qt.NoModifier);
            }
            directoryList.revealCurrent();
            const context = registry.entryContext(row, pane.shellModel.rowIsDirectory(row), Math.max(1, pane.shellModel.selectedCount));
            const delegate = directoryList.itemAtIndex(row);
            if (delegate !== null) {
                pane.actionMenu.openFor(context, delegate, Qt.point(pane.selectionGutterWidth, delegate.height), directoryList);
            } else {
                const rowY = Math.max(0, Math.min(directoryList.height, row * pane.rowHeight - directoryList.contentY));
                pane.actionMenu.openFor(context, directoryList, Qt.point(pane.selectionGutterWidth, rowY), directoryList);
            }
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
            required property bool isSymlink
            required property double size
            required property bool selected
            required property bool recoveryEntry
            required property string entryPath
            readonly property var entryContextMenu: pane.actionMenu
            readonly property var dragMimeData: ({
                    "text/uri-list": pane.shellModel.selectedFileUrls.length > 0 ? pane.shellModel.selectedFileUrls.join("\r\n") + "\r\n" : ""
                })
            readonly property int dragProposedAction: Drag.proposedAction
            readonly property bool dragActive: Drag.active
            readonly property string accessibleKind: entryRow.isSymlink ? qsTr("Symbolic link") : (entryRow.isDir ? qsTr("Folder") : qsTr("File"))
            signal transferDragStarted(int action)

            width: directoryList.width - pane.selectionGutterWidth
            height: pane.rowHeight
            z: 1

            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("%1: %2").arg(entryRow.accessibleKind).arg(entryRow.name)
            Accessible.description: entryRow.recoveryEntry ? qsTr("Recovery entry") : ""
            Accessible.focusable: true
            Accessible.focused: directoryList.activeFocus && pane.shellModel.currentIndex === entryRow.index
            Accessible.selectable: true
            Accessible.selected: entryRow.selected

            function openContextMenu(position) {
                const registry = pane.actionMenu.registry;
                pane.actionMenu.openFor(registry.entryContext(entryRow.index, entryRow.isDir, Math.max(1, pane.shellModel.selectedCount)), entryRow, position, directoryList);
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

            Drag.active: rowPointer.fileDragging
            Drag.dragType: Drag.Automatic
            Drag.keys: ["odysea-entry"]
            Drag.mimeData: dragMimeData
            Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
            Drag.proposedAction: dragActionForModifiers(rowPointer.pressModifiers)
            Drag.hotSpot.x: width / 2
            Drag.hotSpot.y: height / 2

            Rectangle {
                anchors.fill: parent
                color: entryRow.selected ? pane.selectionColor : (rowPointer.containsMouse ? pane.hoverColor : "transparent")
                radius: 4
            }

            // Current-row ring with persistence decay: leaving a row fades
            // the ring over the shared motion token, so a moving cursor
            // leaves a brief trail. A zero duration renders instantly.
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: pane.accentColor
                radius: 4
                opacity: pane.shellModel.currentIndex === entryRow.index ? 1 : 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: pane.persistenceDurationMs
                    }
                }
            }

            GlowFrame {
                objectName: "entryGlow-" + entryRow.index
                anchors.fill: parent
                accentColor: pane.accentColor
                focusedSurface: directoryList.activeFocus && pane.shellModel.currentIndex === entryRow.index
                selected: entryRow.selected && pane.shellModel.currentIndex === entryRow.index
                glowEnabled: pane.glowEnabled
                // The existing current-row ring owns persistence. This
                // emitter changes immediately, avoiding a second motion path
                // and avoiding a lingering bright-pass source during a fade.
                visible: pane.shellModel.currentIndex === entryRow.index
                radius: 4
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 10

                EntryIcon {
                    objectName: "entryIcon-" + entryRow.index
                    implicitWidth: pane.entryFontPixelSize + 4
                    implicitHeight: implicitWidth
                    directory: entryRow.isDir
                    symbolicLink: entryRow.isSymlink
                    directoryInk: pane.dirInkColor
                    fileInk: pane.fileInkColor
                    symbolicLinkInk: pane.linkInkColor
                    highContrast: pane.highContrast
                }
                Text {
                    visible: entryRow.recoveryEntry
                    text: qsTr("RECOVERY")
                    color: pane.dangerColor
                    font.bold: true
                    font.family: pane.captionFontFamily
                    font.pixelSize: Math.max(10, pane.captionFontPixelSize - 2)
                }
                Text {
                    Layout.fillWidth: true
                    text: entryRow.name
                    color: entryRow.isDir ? pane.dirInkColor : pane.primaryTextColor
                    elide: Text.ElideRight
                    font.family: pane.entryFontFamily
                    font.pixelSize: pane.entryFontPixelSize
                }
                Text {
                    visible: !entryRow.isDir
                    text: pane.navigationController.formatSize(entryRow.size)
                    color: pane.metaInkColor
                    font.family: pane.captionFontFamily
                    font.pixelSize: pane.captionFontPixelSize
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
                    if (mouse.button === Qt.RightButton) {
                        pane.navigationController.clearTypeAhead();
                        directoryList.forceActiveFocus();
                        if (entryRow.selected) {
                            pane.shellModel.moveCursorTo(entryRow.index, false, true);
                        } else {
                            pane.shellModel.selectRow(entryRow.index, mouse.modifiers);
                        }
                        directoryList.revealCurrent();
                        entryRow.openContextMenu(pressPosition);
                    }
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
                        entryRow.transferDragStarted(entryRow.dragProposedAction);
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
                    if (mouse.button === Qt.RightButton) {
                        return;
                    }
                    if (suppressClick) {
                        suppressClick = false;
                        return;
                    }
                    pane.navigationController.clearTypeAhead();
                    directoryList.forceActiveFocus();
                    pane.shellModel.selectRow(entryRow.index, mouse.modifiers);
                    directoryList.revealCurrent();
                }
                onDoubleClicked: mouse => {
                    if (mouse.button === Qt.LeftButton) {
                        pane.shellModel.activate(entryRow.index);
                    }
                }
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

        // Blank-canvas context menu, pointer path. The area mirrors the
        // blank rubber-band region's geometry instead of restating it, and
        // accepts only the right button so left-press band selection below
        // is untouched.
        MouseArea {
            id: blankContextPointer

            objectName: "blankContextArea"
            acceptedButtons: Qt.RightButton
            x: blankBandPointer.x
            y: blankBandPointer.y
            width: blankBandPointer.width
            height: blankBandPointer.height
            z: 1
            onPressed: mouse => {
                pane.navigationController.clearTypeAhead();
                directoryList.forceActiveFocus();
                pane.actionMenu.openFor(pane.actionMenu.registry.canvasContext(pane.shellModel.path), blankContextPointer, Qt.point(mouse.x, mouse.y), directoryList);
            }
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
            color: pane.rubberBandColor
            border.color: pane.accentColor
            radius: 3
        }

        ScrollBar.vertical: ScrollBar {
            id: verticalBar

            width: 8
        }
    }
}

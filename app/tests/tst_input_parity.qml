pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import "../qml"

TestCase {
    id: testCase

    name: "InputParity"

    property var shellWindow

    ListModel {
        id: fakeModel

        property string path: "/sample"
        property string errorString: ""
        property string filterText: ""
        property string statusMessage: ""
        property bool busy: false
        property bool showHidden: false
        property bool canGoBack: false
        property bool canGoForward: false
        property bool canGoUp: true
        property int sortMode: 0
        property int currentIndex: 0
        property int selectedCount: 0
        property int tabCount: 1
        property int activeTab: 0
        property int paneCount: 1
        property int activePane: 0
        property bool operationBusy: false
        property string operationErrorString: ""
        property int selectRowCalls: 0
        property int selectedRow: -1
        property int selectedModifiers: Qt.NoModifier
        property int activateCalls: 0
        property int activatedRow: -1
        property int moveCursorCalls: 0
        property int moveCursorDelta: 0
        property int toggleCurrentCalls: 0
        property int clearSelectionCalls: 0
        property int beginRubberBandCalls: 0
        property bool rubberBandAdditive: false
        property int updateRubberBandCalls: 0
        property var rubberBandRows: []
        property int rubberBandCurrentRow: -1
        property int endRubberBandCalls: 0
        property int activateTabCalls: 0
        property int activatedTab: -1
        property int selectAllCalls: 0
        property int requestCopyCalls: 0
        property int requestMoveCalls: 0
        property int requestRenameCalls: 0
        property int requestTrashCalls: 0
        property int performCopyCalls: 0
        property int performMoveCalls: 0
        property int performRenameCalls: 0
        property int performTrashCalls: 0
        property string performedDestination: ""
        property string performedName: ""
        property int performedConflictMode: -1

        signal filesystemOperationRequested(string operation, var paths)

        function activate(row) {
            activateCalls += 1;
            activatedRow = row;
        }
        function activatePane(pane) {
        }
        function activateTab(tab) {
            activateTabCalls += 1;
            activatedTab = tab;
            activeTab = tab;
        }
        function addTab() {
        }
        function beginRubberBand(additive) {
            beginRubberBandCalls += 1;
            rubberBandAdditive = additive;
        }
        function clearSelection() {
            clearSelectionCalls += 1;
        }
        function closeTab(tab) {
        }
        function endRubberBand() {
            endRubberBandCalls += 1;
        }
        function goBack() {
        }
        function goForward() {
        }
        function goUp() {
        }
        function moveCursor(delta, extendSelection, preserveSelection) {
            moveCursorCalls += 1;
            moveCursorDelta = delta;
            currentIndex = Math.max(0, Math.min(count - 1, currentIndex + delta));
        }
        function moveCursorTo(row, extendSelection, preserveSelection) {
        }
        function refresh() {
        }
        function requestCopy() {
            requestCopyCalls += 1;
            filesystemOperationRequested("copy", ["/sample/sample-0.txt"]);
        }
        function requestMove() {
            requestMoveCalls += 1;
            filesystemOperationRequested("move", ["/sample/sample-0.txt"]);
        }
        function requestRename() {
            requestRenameCalls += 1;
            filesystemOperationRequested("rename", ["/sample/sample-0.txt"]);
        }
        function requestTrash() {
            requestTrashCalls += 1;
            filesystemOperationRequested("trash", ["/sample/sample-0.txt"]);
        }
        function performCopy(destination, conflictMode) {
            performCopyCalls += 1;
            performedDestination = destination;
            performedConflictMode = conflictMode;
        }
        function performMove(destination, conflictMode) {
            performMoveCalls += 1;
            performedDestination = destination;
            performedConflictMode = conflictMode;
        }
        function performRename(name, conflictMode) {
            performRenameCalls += 1;
            performedName = name;
            performedConflictMode = conflictMode;
        }
        function performTrash() {
            performTrashCalls += 1;
        }
        function selectAll() {
            selectAllCalls += 1;
        }
        function setDualPaneEnabled(enabled) {
        }
        function tabLabel(tab) {
            return "Sample " + tab;
        }
        function toggleCurrent() {
            toggleCurrentCalls += 1;
        }
        function updateRubberBandSelection(rows, currentRow) {
            updateRubberBandCalls += 1;
            rubberBandRows = rows.slice();
            rubberBandCurrentRow = currentRow;
        }

        function selectRow(row, modifiers) {
            selectRowCalls += 1;
            selectedRow = row;
            selectedModifiers = modifiers;
            currentIndex = row;
            selectedCount = 1;
            setProperty(row, "selected", true);
        }

        function resetTelemetry() {
            selectRowCalls = 0;
            selectedRow = -1;
            selectedModifiers = Qt.NoModifier;
            activateCalls = 0;
            activatedRow = -1;
            moveCursorCalls = 0;
            moveCursorDelta = 0;
            toggleCurrentCalls = 0;
            clearSelectionCalls = 0;
            beginRubberBandCalls = 0;
            rubberBandAdditive = false;
            updateRubberBandCalls = 0;
            rubberBandRows = [];
            rubberBandCurrentRow = -1;
            endRubberBandCalls = 0;
            activateTabCalls = 0;
            activatedTab = -1;
            selectAllCalls = 0;
            requestCopyCalls = 0;
            requestMoveCalls = 0;
            requestRenameCalls = 0;
            requestTrashCalls = 0;
            performCopyCalls = 0;
            performMoveCalls = 0;
            performRenameCalls = 0;
            performTrashCalls = 0;
            performedDestination = "";
            performedName = "";
            performedConflictMode = -1;
        }
    }

    Component {
        id: shellComponent

        Main {
            shellModel: fakeModel
        }
    }

    function initTestCase() {
        shellWindow = shellComponent.createObject(null);
        verify(shellWindow !== null);
        tryVerify(function () {
            return shellWindow.visible;
        });
        waitForRendering(shellWindow.contentItem);
    }

    function init() {
        fakeModel.tabCount = 1;
        fakeModel.activeTab = 0;
        fakeModel.operationErrorString = "";
        populateRows(4);
        fakeModel.resetTelemetry();
    }

    function cleanupTestCase() {
        shellWindow.destroy();
    }

    function populateRows(count) {
        let list = child("directoryList");
        list.interactive = false;
        list.cancelFlick();
        list.contentY = 0;
        fakeModel.clear();
        for (let index = 0; index < count; ++index) {
            fakeModel.append({
                "name": "sample-" + index + ".txt",
                "isDir": false,
                "size": 12 + index,
                "selected": false,
                "recoveryEntry": false
            });
        }
        fakeModel.currentIndex = count > 0 ? 0 : -1;
        fakeModel.selectedCount = 0;

        tryCompare(list, "count", count);
        list.positionViewAtBeginning();
        list.interactive = true;
        waitForRendering(shellWindow.contentItem);
    }

    function child(objectName) {
        let item = findChild(shellWindow.contentItem, objectName);
        if (item === null) {
            item = findChild(shellWindow.header, objectName);
        }
        if (item === null) {
            item = findChild(shellWindow, objectName);
        }
        tryVerify(function () {
            return item !== null;
        });
        return item;
    }

    function rowAt(index) {
        let row = child("entryRow-" + index);
        tryVerify(function () {
            return row.width > 0 && row.height > 0;
        });
        return row;
    }

    function clickRow(index, button, modifiers) {
        let row = rowAt(index);
        mouseClick(row, row.width / 2, row.height / 2, button, modifiers);
    }

    function verifyBandDrag(area, expectRows) {
        tryVerify(function () {
            return area.width > 4 && area.height > 80;
        });
        mouseDrag(area, area.width / 2, 10, 0, 60, Qt.LeftButton, Qt.NoModifier, 10);
        tryCompare(fakeModel, "beginRubberBandCalls", 1);
        verify(fakeModel.updateRubberBandCalls > 0);
        compare(fakeModel.endRubberBandCalls, 1);
        if (expectRows) {
            verify(fakeModel.rubberBandRows.length > 0);
            verify(fakeModel.rubberBandCurrentRow >= 0);
        } else {
            compare(fakeModel.rubberBandRows.length, 0);
            compare(fakeModel.rubberBandCurrentRow, -1);
        }
    }

    function prepareScrollableRows(contentY) {
        populateRows(60);
        let list = child("directoryList");
        list.interactive = false;
        list.cancelFlick();
        list.contentY = contentY;
        wait(40);
        list.cancelFlick();
        list.contentY = contentY;
        list.interactive = true;
        tryCompare(list, "contentY", contentY);
        waitForRendering(shellWindow.contentItem);
        fakeModel.resetTelemetry();
        return list;
    }

    function test_blankAreaRubberBand() {
        verifyBandDrag(child("rubberBandBlankArea"), false);
    }

    function test_ctrlClickReachesSelectionModel() {
        clickRow(1, Qt.LeftButton, Qt.ControlModifier);
        tryCompare(fakeModel, "selectRowCalls", 1);
        compare(fakeModel.selectedRow, 1);
        verify((fakeModel.selectedModifiers & Qt.ControlModifier) !== 0);
    }

    function test_copyDialogKeyboardAndMouseParity() {
        clickRow(0, Qt.LeftButton, Qt.NoModifier);
        let list = child("directoryList");
        list.forceActiveFocus();

        keyClick(Qt.Key_C, Qt.ControlModifier);
        tryCompare(fakeModel, "requestCopyCalls", 1);
        let transferDialog = child("transferDialog");
        tryCompare(transferDialog, "opened", true);
        let destinationField = child("operationDestinationField");
        destinationField.text = "/destination/keyboard";
        destinationField.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryCompare(fakeModel, "performCopyCalls", 1);
        compare(fakeModel.performedDestination, "/destination/keyboard");
        tryCompare(transferDialog, "opened", false);

        mouseClick(child("copyButton"));
        tryCompare(fakeModel, "requestCopyCalls", 2);
        tryCompare(transferDialog, "opened", true);
        destinationField.text = "/destination/pointer";
        mouseClick(child("transferConfirmButton"));
        tryCompare(fakeModel, "performCopyCalls", 2);
        compare(fakeModel.performedDestination, "/destination/pointer");
        tryCompare(transferDialog, "opened", false);
    }

    function test_doubleClickReachesActivationModel() {
        let row = rowAt(2);
        mouseDoubleClickSequence(row, row.width / 2, row.height / 2, Qt.LeftButton, Qt.NoModifier);
        tryCompare(fakeModel, "activateCalls", 1);
        compare(fakeModel.activatedRow, 2);
    }

    function test_filledViewportRubberBand() {
        populateRows(40);
        fakeModel.resetTelemetry();
        let blankArea = child("rubberBandBlankArea");
        tryCompare(blankArea, "height", 0);
        verifyBandDrag(child("rubberBandGutter"), true);
    }

    function test_bandAnchorSurvivesContentMovement() {
        let list = prepareScrollableRows(300);
        let gutter = child("rubberBandGutter");
        let pointerX = gutter.width / 2;

        mousePress(gutter, pointerX, 100, Qt.LeftButton, Qt.NoModifier);
        list.contentY = 368;
        tryCompare(list, "contentY", 368);
        mouseMove(gutter, pointerX, 150, 10, Qt.LeftButton, Qt.NoModifier);
        mouseRelease(gutter, pointerX, 150, Qt.LeftButton, Qt.NoModifier);

        tryCompare(fakeModel, "beginRubberBandCalls", 1);
        verify(fakeModel.updateRubberBandCalls > 0);
        compare(fakeModel.rubberBandRows[0], Math.floor(400 / 34));
        compare(fakeModel.rubberBandRows[fakeModel.rubberBandRows.length - 1], Math.floor(517 / 34));
        compare(fakeModel.rubberBandCurrentRow, Math.floor(517 / 34));
        compare(fakeModel.endRubberBandCalls, 1);
    }

    function test_scrolledDownwardFilledViewportRubberBand() {
        let list = prepareScrollableRows(300);
        let gutter = child("rubberBandGutter");

        mouseDrag(gutter, gutter.width / 2, 30, 0, 140, Qt.LeftButton, Qt.NoModifier, 10);

        tryCompare(fakeModel, "beginRubberBandCalls", 1);
        verify(fakeModel.updateRubberBandCalls > 2);
        compare(fakeModel.rubberBandRows[0], Math.floor(330 / 34));
        compare(fakeModel.rubberBandRows[fakeModel.rubberBandRows.length - 1], Math.floor(469 / 34));
        compare(fakeModel.rubberBandCurrentRow, Math.floor(469 / 34));
        compare(fakeModel.endRubberBandCalls, 1);
        compare(list.contentY, 300);
    }

    function test_scrolledUpwardFilledViewportRubberBand() {
        let list = prepareScrollableRows(300);
        let gutter = child("rubberBandGutter");

        mouseDrag(gutter, gutter.width / 2, 200, 0, -140, Qt.LeftButton, Qt.NoModifier, 10);

        tryCompare(fakeModel, "beginRubberBandCalls", 1);
        verify(fakeModel.updateRubberBandCalls > 2);
        compare(fakeModel.rubberBandRows[0], Math.floor(360 / 34));
        compare(fakeModel.rubberBandRows[fakeModel.rubberBandRows.length - 1], Math.floor(499 / 34));
        compare(fakeModel.rubberBandCurrentRow, Math.floor(360 / 34));
        compare(fakeModel.endRubberBandCalls, 1);
        compare(list.contentY, 300);
    }

    function test_keyboardSelectionPaths() {
        let list = child("directoryList");
        list.forceActiveFocus();
        tryVerify(function () {
            return list.activeFocus;
        });
        keyClick(Qt.Key_Down);
        keyClick(Qt.Key_Space);
        keyClick(Qt.Key_Escape);
        tryCompare(fakeModel, "moveCursorCalls", 1);
        compare(fakeModel.moveCursorDelta, 1);
        compare(fakeModel.toggleCurrentCalls, 1);
        compare(fakeModel.clearSelectionCalls, 1);
    }

    function test_moveDialogKeyboardAndMouseParity() {
        clickRow(0, Qt.LeftButton, Qt.NoModifier);
        let list = child("directoryList");
        list.forceActiveFocus();

        keyClick(Qt.Key_X, Qt.ControlModifier);
        tryCompare(fakeModel, "requestMoveCalls", 1);
        let transferDialog = child("transferDialog");
        tryCompare(transferDialog, "opened", true);
        let destinationField = child("operationDestinationField");
        destinationField.text = "/destination/move-keyboard";
        destinationField.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryCompare(fakeModel, "performMoveCalls", 1);

        mouseClick(child("moveButton"));
        tryCompare(fakeModel, "requestMoveCalls", 2);
        tryCompare(transferDialog, "opened", true);
        destinationField.text = "/destination/move-pointer";
        mouseClick(child("transferConfirmButton"));
        tryCompare(fakeModel, "performMoveCalls", 2);
        compare(fakeModel.performedDestination, "/destination/move-pointer");
    }

    function test_plainClickReachesSelectionModel() {
        clickRow(0, Qt.LeftButton, Qt.NoModifier);
        tryCompare(fakeModel, "selectRowCalls", 1);
        compare(fakeModel.selectedRow, 0);
        compare(fakeModel.selectedModifiers, Qt.NoModifier);
    }

    function test_scrolledRowClickPathsRemainIndependent() {
        prepareScrollableRows(300);

        clickRow(10, Qt.LeftButton, Qt.NoModifier);
        tryCompare(fakeModel, "selectRowCalls", 1);
        compare(fakeModel.selectedRow, 10);
        compare(fakeModel.beginRubberBandCalls, 0);

        let doubleRow = rowAt(11);
        mouseDoubleClickSequence(doubleRow, doubleRow.width / 2, doubleRow.height / 2, Qt.LeftButton, Qt.NoModifier);
        tryCompare(fakeModel, "activateCalls", 1);
        compare(fakeModel.activatedRow, 11);
        compare(fakeModel.beginRubberBandCalls, 0);

        clickRow(12, Qt.RightButton, Qt.NoModifier);
        tryCompare(fakeModel, "selectedRow", 12);
        compare(fakeModel.beginRubberBandCalls, 0);
        keyClick(Qt.Key_Escape);
    }

    function test_scrolledRowWheelRemainsIndependent() {
        let list = prepareScrollableRows(300);
        let row = rowAt(10);
        const initialContentY = list.contentY;

        mouseWheel(row, row.width / 2, row.height / 2, 0, -120, Qt.NoButton, Qt.NoModifier, 10);

        tryVerify(function () {
            return list.contentY !== initialContentY;
        });
        list.interactive = false;
        list.cancelFlick();

        list.contentY = 300;
        list.interactive = true;
        tryCompare(list, "contentY", 300);
        let gutter = child("rubberBandGutter");
        mouseWheel(gutter, gutter.width / 2, gutter.height / 2, 0, -120, Qt.NoButton, Qt.NoModifier, 10);
        tryVerify(function () {
            return list.contentY !== 300;
        });
        list.interactive = false;
        list.cancelFlick();
        list.interactive = true;

        compare(fakeModel.beginRubberBandCalls, 0);
        compare(fakeModel.selectRowCalls, 0);
    }

    function test_rightClickReachesSelectionModel() {
        clickRow(1, Qt.RightButton, Qt.NoModifier);
        tryCompare(fakeModel, "selectRowCalls", 1);
        compare(fakeModel.selectedRow, 1);
        keyClick(Qt.Key_Escape);
    }

    function test_renameDialogKeyboardAndMouseParity() {
        clickRow(0, Qt.LeftButton, Qt.NoModifier);
        let list = child("directoryList");
        list.forceActiveFocus();

        keyClick(Qt.Key_F2);
        tryCompare(fakeModel, "requestRenameCalls", 1);
        let renameDialog = child("renameDialog");
        tryCompare(renameDialog, "opened", true);
        let renameField = child("renameField");
        renameField.text = "keyboard-name.txt";
        renameField.forceActiveFocus();
        keyClick(Qt.Key_Return);
        tryCompare(fakeModel, "performRenameCalls", 1);
        compare(fakeModel.performedName, "keyboard-name.txt");

        mouseClick(child("renameButton"));
        tryCompare(fakeModel, "requestRenameCalls", 2);
        tryCompare(renameDialog, "opened", true);
        renameField.text = "pointer-name.txt";
        mouseClick(child("renameConfirmButton"));
        tryCompare(fakeModel, "performRenameCalls", 2);
        compare(fakeModel.performedName, "pointer-name.txt");
    }

    function test_shiftClickReachesSelectionModel() {
        clickRow(2, Qt.LeftButton, Qt.ShiftModifier);
        tryCompare(fakeModel, "selectRowCalls", 1);
        compare(fakeModel.selectedRow, 2);
        verify((fakeModel.selectedModifiers & Qt.ShiftModifier) !== 0);
    }

    function test_trashConfirmationKeyboardAndMouseParity() {
        clickRow(0, Qt.LeftButton, Qt.NoModifier);
        let list = child("directoryList");
        list.forceActiveFocus();

        keyClick(Qt.Key_Delete);
        tryCompare(fakeModel, "requestTrashCalls", 1);
        let trashDialog = child("trashDialog");
        tryCompare(trashDialog, "opened", true);
        let confirmButton = child("trashConfirmButton");
        tryVerify(function () {
            return confirmButton.activeFocus;
        });
        keyClick(Qt.Key_Space);
        tryCompare(fakeModel, "performTrashCalls", 1);

        mouseClick(child("trashButton"));
        tryCompare(fakeModel, "requestTrashCalls", 2);
        tryCompare(trashDialog, "opened", true);
        mouseClick(confirmButton);
        tryCompare(fakeModel, "performTrashCalls", 2);
    }

    function test_contextOperationActionsRemainReachable() {
        let entryMenu = rowAt(0).entryContextMenu;
        verify(entryMenu !== null);

        clickRow(0, Qt.RightButton, Qt.NoModifier);
        tryCompare(entryMenu, "opened", true);
        mouseClick(entryMenu.itemAt(2));
        tryCompare(fakeModel, "requestCopyCalls", 1);
        mouseClick(child("transferCancelButton"));

        clickRow(0, Qt.RightButton, Qt.NoModifier);
        tryCompare(entryMenu, "opened", true);
        mouseClick(entryMenu.itemAt(3));
        tryCompare(fakeModel, "requestMoveCalls", 1);
        mouseClick(child("transferCancelButton"));

        clickRow(0, Qt.RightButton, Qt.NoModifier);
        tryCompare(entryMenu, "opened", true);
        mouseClick(entryMenu.itemAt(4));
        tryCompare(fakeModel, "requestRenameCalls", 1);
        mouseClick(child("renameCancelButton"));

        clickRow(0, Qt.RightButton, Qt.NoModifier);
        tryCompare(entryMenu, "opened", true);
        mouseClick(entryMenu.itemAt(5));
        tryCompare(fakeModel, "requestTrashCalls", 1);
        mouseClick(child("trashCancelButton"));
    }

    function test_operationErrorFeedback() {
        fakeModel.operationErrorString = "Synthetic operation failure";
        let errorDialog = child("operationErrorDialog");
        tryCompare(errorDialog, "opened", true);
        errorDialog.close();
    }

    function test_tabSwitchingKeyboardAndMouseParity() {
        fakeModel.tabCount = 3;
        fakeModel.activeTab = 1;
        waitForRendering(shellWindow.contentItem);
        fakeModel.resetTelemetry();

        keyClick(Qt.Key_Tab, Qt.ControlModifier);
        tryCompare(fakeModel, "activateTabCalls", 1);
        compare(fakeModel.activatedTab, 2);

        keyClick(Qt.Key_Tab, Qt.ControlModifier | Qt.ShiftModifier);
        tryCompare(fakeModel, "activateTabCalls", 2);
        compare(fakeModel.activatedTab, 1);

        let thirdTab = child("tabButton-2");
        mouseClick(thirdTab, thirdTab.width / 2, thirdTab.height / 2, Qt.LeftButton, Qt.NoModifier);
        tryCompare(fakeModel, "activateTabCalls", 3);
        compare(fakeModel.activatedTab, 2);
    }

    function test_selectAllKeyboardAndMouseParity() {
        let list = child("directoryList");
        list.forceActiveFocus();
        tryVerify(function () {
            return list.activeFocus;
        });

        keyClick(Qt.Key_A, Qt.ControlModifier);
        tryCompare(fakeModel, "selectAllCalls", 1);

        let selectAllButton = child("selectAllButton");
        mouseClick(selectAllButton, selectAllButton.width / 2, selectAllButton.height / 2, Qt.LeftButton, Qt.NoModifier);
        tryCompare(fakeModel, "selectAllCalls", 2);
    }
}

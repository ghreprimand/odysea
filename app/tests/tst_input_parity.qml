pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea

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
        property var selectedFileUrls: collectSelectedFileUrls()
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
        property int navigateToPathCalls: 0
        property string navigatedPath: ""
        property int dropSelectionCalls: 0
        property string dropDestination: ""
        property bool dropMove: false
        property int dropConflictMode: -1
        property bool dropAccepted: true
        property int moveCursorCalls: 0
        property int moveCursorDelta: 0
        property int moveCursorToCalls: 0
        property int movedToRow: -1
        property bool movedWithExtension: false
        property bool movedPreservingSelection: false
        property int selectionAnchor: 0
        property int prefixSearchCalls: 0
        property string searchedPrefix: ""
        property bool searchedWithCycling: false
        property var scriptedPrefixRows: []
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
        property int requestThumbnailCalls: 0
        property int releaseThumbnailCalls: 0
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
        function activateCurrent() {
            activate(currentIndex);
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
            clearSelectedRows();
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
        function breadcrumbSegments() {
            const segments = [
                {
                    "label": "/",
                    "path": "/",
                    "url": "file:///"
                }
            ];
            if (path !== "/") {
                const names = path.split("/").filter(function (name) {
                    return name.length > 0;
                });
                let accumulated = "";
                for (let index = 0; index < names.length; ++index) {
                    accumulated += "/" + names[index];
                    segments.push({
                        "label": names[index],
                        "path": accumulated,
                        "url": "file://" + accumulated
                    });
                }
            }
            return segments;
        }
        function navigateToPath(destination) {
            navigateToPathCalls += 1;
            navigatedPath = destination;
            path = destination;
        }
        function collectSelectedFileUrls() {
            const urls = [];
            for (let row = 0; row < count; ++row) {
                if (get(row).selected) {
                    urls.push("file://" + get(row).entryPath);
                }
            }
            return urls;
        }
        function rowSelected(row) {
            return row >= 0 && row < count && get(row).selected;
        }
        function rowIsDirectory(row) {
            return row >= 0 && row < count && get(row).isDir;
        }
        function canDropSelection(destination) {
            return selectedCount > 0 && destination.length > 0;
        }
        function dropSelection(destination, move, conflictMode) {
            dropSelectionCalls += 1;
            dropDestination = destination;
            dropMove = move;
            dropConflictMode = conflictMode;
            return dropAccepted;
        }
        function moveCursor(delta, extendSelection, preserveSelection) {
            moveCursorCalls += 1;
            moveCursorDelta = delta;
            moveCursorTo(Math.max(0, Math.min(count - 1, currentIndex + delta)), extendSelection, preserveSelection);
        }
        function moveCursorTo(row, extendSelection, preserveSelection) {
            if (row < 0 || row >= count) {
                return;
            }
            moveCursorToCalls += 1;
            movedToRow = row;
            movedWithExtension = extendSelection;
            movedPreservingSelection = preserveSelection;
            if (extendSelection) {
                clearSelectedRows();
                const first = Math.min(selectionAnchor, row);
                const last = Math.max(selectionAnchor, row);
                for (let selectedRow = first; selectedRow <= last; ++selectedRow) {
                    setProperty(selectedRow, "selected", true);
                }
                selectedCount = last - first + 1;
            } else if (!preserveSelection) {
                selectOnly(row);
                selectionAnchor = row;
            }
            currentIndex = row;
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
        function requestThumbnail(row) {
            requestThumbnailCalls += 1;
        }
        function releaseThumbnail(entryPath) {
            releaseThumbnailCalls += 1;
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
            for (let row = 0; row < count; ++row) {
                setProperty(row, "selected", true);
            }
            selectedCount = count;
        }
        function setDualPaneEnabled(enabled) {
        }
        function tabLabel(tab) {
            return "Sample " + tab;
        }
        function toggleCurrent() {
            toggleCurrentCalls += 1;
            if (currentIndex < 0 || currentIndex >= count) {
                return;
            }
            const selectedNow = !get(currentIndex).selected;
            setProperty(currentIndex, "selected", selectedNow);
            selectedCount += selectedNow ? 1 : -1;
            selectionAnchor = currentIndex;
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
            if ((modifiers & Qt.ShiftModifier) !== 0) {
                moveCursorTo(row, true, false);
                return;
            }
            if ((modifiers & Qt.ControlModifier) !== 0) {
                const selectedNow = !get(row).selected;
                setProperty(row, "selected", selectedNow);
                selectedCount += selectedNow ? 1 : -1;
                selectionAnchor = row;
                currentIndex = row;
                return;
            }
            selectOnly(row);
            selectionAnchor = row;
            currentIndex = row;
        }

        function clearSelectedRows() {
            for (let row = 0; row < count; ++row) {
                setProperty(row, "selected", false);
            }
            selectedCount = 0;
        }

        function selectByPrefix(prefix, cycle) {
            prefixSearchCalls += 1;
            searchedPrefix = prefix;
            searchedWithCycling = cycle;
            if (scriptedPrefixRows.length === 0) {
                return false;
            }
            const row = scriptedPrefixRows[0];
            scriptedPrefixRows = scriptedPrefixRows.slice(1);
            if (row < 0) {
                return false;
            }
            moveCursorTo(row, false, false);
            return true;
        }

        function selectOnly(row) {
            clearSelectedRows();
            setProperty(row, "selected", true);
            selectedCount = 1;
        }

        function resetTelemetry() {
            selectRowCalls = 0;
            selectedRow = -1;
            selectedModifiers = Qt.NoModifier;
            activateCalls = 0;
            activatedRow = -1;
            navigateToPathCalls = 0;
            navigatedPath = "";
            dropSelectionCalls = 0;
            dropDestination = "";
            dropMove = false;
            dropConflictMode = -1;
            dropAccepted = true;
            moveCursorCalls = 0;
            moveCursorDelta = 0;
            moveCursorToCalls = 0;
            movedToRow = -1;
            movedWithExtension = false;
            movedPreservingSelection = false;
            prefixSearchCalls = 0;
            searchedPrefix = "";
            searchedWithCycling = false;
            scriptedPrefixRows = [];
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
            requestThumbnailCalls = 0;
            releaseThumbnailCalls = 0;
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
        fakeModel.path = "/sample";
        fakeModel.tabCount = 1;
        fakeModel.activeTab = 0;
        fakeModel.operationErrorString = "";
        shellWindow.gridMode = false;
        shellWindow.clearTypeAhead();
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
                "recoveryEntry": false,
                "entryPath": "/sample/sample-" + index + ".txt",
                "thumbnailSource": "",
                "thumbnailLoading": false
            });
        }
        fakeModel.currentIndex = count > 0 ? 0 : -1;
        fakeModel.selectedCount = 0;
        fakeModel.selectionAnchor = count > 0 ? 0 : -1;

        tryCompare(list, "count", count);
        tryCompare(child("directoryGrid"), "count", count);
        list.positionViewAtBeginning();
        list.interactive = true;
        waitForRendering(shellWindow.contentItem);
    }

    function populateNamedRows(names) {
        let list = child("directoryList");
        list.interactive = false;
        list.cancelFlick();
        list.contentY = 0;
        fakeModel.clear();
        for (let index = 0; index < names.length; ++index) {
            fakeModel.append({
                "name": names[index],
                "isDir": false,
                "size": 12 + index,
                "selected": false,
                "recoveryEntry": false,
                "entryPath": "/sample/" + names[index],
                "thumbnailSource": "",
                "thumbnailLoading": false
            });
        }
        fakeModel.currentIndex = names.length > 0 ? 0 : -1;
        fakeModel.selectedCount = 0;
        fakeModel.selectionAnchor = names.length > 0 ? 0 : -1;
        tryCompare(list, "count", names.length);
        tryCompare(child("directoryGrid"), "count", names.length);
        list.positionViewAtBeginning();
        list.interactive = true;
        waitForRendering(shellWindow.contentItem);
    }

    function child(objectName) {
        let item = findChild(shellWindow.contentItem, objectName);
        if (item === null) {
            item = findChild(shellWindow, objectName);
        }
        tryVerify(function () {
            return item !== null;
        });
        return item;
    }

    function shortcutForSequence(sequence) {
        for (let index = 0; index < shellWindow.contentData.length; ++index) {
            const candidate = shellWindow.contentData[index];
            if (candidate !== null && candidate.sequence !== undefined && candidate.sequence.toString() === sequence) {
                return candidate;
            }
        }
        return null;
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

    function cellAt(index) {
        let cell = child("entryCell-" + index);
        tryVerify(function () {
            return cell.width > 0 && cell.height > 0;
        });
        return cell;
    }

    function clickCell(index, button, modifiers) {
        let cell = cellAt(index);
        mouseClick(cell, cell.width / 2, cell.height / 2, button, modifiers);
    }

    function selectedRows() {
        const rows = [];
        for (let row = 0; row < fakeModel.count; ++row) {
            if (fakeModel.get(row).selected) {
                rows.push(row);
            }
        }
        return rows;
    }

    function realizedGridCellCount(rowCount) {
        let realized = 0;
        for (let index = 0; index < rowCount; ++index) {
            if (findChild(shellWindow.contentItem, "entryCell-" + index) !== null) {
                ++realized;
            }
        }
        return realized;
    }

    function verifyBandDrag(area, expectRows) {
        tryVerify(function () {
            return area.width > 4 && area.height > 80;
        });
        mouseDrag(area, area.width / 2, 10, 0, 60, Qt.LeftButton, Qt.NoModifier, 10);
        tryCompare(fakeModel, "beginRubberBandCalls", 1);
        verify(fakeModel.updateRubberBandCalls > 0);
        compare(fakeModel.endRubberBandCalls, 1);
        compare(fakeModel.dropSelectionCalls, 0);
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

    function test_presentationLayerWiredIntoTheShell() {
        const layer = child("presentationLayer");
        verify(layer !== null);
        // The layer consumes the same live theme object the shell renders
        // with — not a copy.
        verify(layer.theme === shellWindow.shellTheme);
        // This suite forces the software scene graph: the pipeline must
        // stand down silently while every interaction above keeps passing.
        verify(layer.softwareBackend);
        verify(!layer.active);
    }

    function test_appearancePanelKeyboardAndMouseParity() {
        const panel = child("appearancePanel");
        verify(!panel.opened);

        // Keyboard path.
        keySequence("Ctrl+,");
        tryVerify(function () {
            return panel.opened;
        });
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !panel.opened;
        });

        // Mouse path.
        mouseClick(child("appearanceButton"));
        tryVerify(function () {
            return panel.opened;
        });
        const closeButton = findChild(panel.contentItem, "closeAppearanceButton");
        verify(closeButton !== null);
        mouseClick(closeButton);
        tryVerify(function () {
            return !panel.opened;
        });
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

    function test_enterAndDoubleClickShareActivationPath() {
        let list = child("directoryList");
        fakeModel.selectRow(1, Qt.NoModifier);
        list.forceActiveFocus();
        fakeModel.resetTelemetry();
        keyClick(Qt.Key_Return);
        tryCompare(fakeModel, "activateCalls", 1);
        compare(fakeModel.activatedRow, 1);

        let row = rowAt(1);
        mouseDoubleClickSequence(row, row.width / 2, row.height / 2, Qt.LeftButton, Qt.NoModifier);
        tryCompare(fakeModel, "activateCalls", 2);
        compare(fakeModel.activatedRow, 1);
    }

    function test_breadcrumbPointerAndKeyboardNavigation() {
        let rootCrumb = child("breadcrumb-0");
        let sampleCrumb = child("breadcrumb-1");
        compare(rootCrumb.segmentPath, "/");
        compare(rootCrumb.segmentUrl, "file:///");
        compare(sampleCrumb.segmentPath, "/sample");
        compare(sampleCrumb.segmentUrl, "file:///sample");

        rootCrumb.forceActiveFocus();
        keyClick(Qt.Key_Right);
        tryVerify(function () {
            return sampleCrumb.activeFocus;
        });
        keyClick(Qt.Key_Return);
        tryCompare(fakeModel, "navigateToPathCalls", 1);
        compare(fakeModel.navigatedPath, "/sample");
        tryVerify(function () {
            return child("directoryList").activeFocus;
        });

        mouseClick(rootCrumb);
        tryCompare(fakeModel, "navigateToPathCalls", 2);
        compare(fakeModel.navigatedPath, "/");
        tryVerify(function () {
            return child("directoryList").activeFocus;
        });
    }

    function test_contextMenuKeyboardPathsPreserveSelectionAndRestoreFocus() {
        let list = child("directoryList");
        fakeModel.selectRow(2, Qt.NoModifier);
        fakeModel.selectRow(1, Qt.ControlModifier);
        compare(selectedRows().join(","), "1,2");
        list.forceActiveFocus();
        fakeModel.resetTelemetry();

        keyClick(Qt.Key_Menu);
        let menu = child("listKeyboardContextMenu");
        tryCompare(menu, "opened", true);
        compare(selectedRows().join(","), "1,2");
        compare(fakeModel.movedToRow, 1);
        compare(fakeModel.movedPreservingSelection, true);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return list.activeFocus;
        });

        keyClick(Qt.Key_F10, Qt.ShiftModifier);
        tryCompare(menu, "opened", true);
        compare(selectedRows().join(","), "1,2");
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return list.activeFocus;
        });
    }

    function test_dragMimeUrlsModifiersAndDirectoryTargets() {
        fakeModel.selectRow(0, Qt.NoModifier);
        fakeModel.selectRow(1, Qt.ControlModifier);
        fakeModel.setProperty(3, "isDir", true);
        fakeModel.setProperty(3, "entryPath", "/sample/folder");
        waitForRendering(shellWindow.contentItem);

        let row = rowAt(0);
        compare(row.dragMimeData["text/uri-list"], "file:///sample/sample-0.txt\r\nfile:///sample/sample-1.txt\r\n");
        compare(row.dragProposedAction, Qt.MoveAction);
        mousePress(row, row.width / 2, row.height / 2, Qt.LeftButton, Qt.ControlModifier);
        compare(row.dragProposedAction, Qt.CopyAction);
        mouseRelease(row, row.width / 2, row.height / 2, Qt.LeftButton, Qt.ControlModifier);

        compare(child("entryDropTarget-0").enabled, false);
        compare(child("entryDropTarget-3").enabled, true);
        verify(child("breadcrumbDropTarget-0").enabled);
        verify(rowAt(3).dropSelectedEntries(Qt.CopyAction));
        compare(fakeModel.dropSelectionCalls, 1);
        compare(fakeModel.dropDestination, "/sample/folder");
        compare(fakeModel.dropMove, false);
        compare(fakeModel.dropConflictMode, 0);
        verify(child("breadcrumb-0").dropSelectedEntries(Qt.MoveAction));
        compare(fakeModel.dropSelectionCalls, 2);
        compare(fakeModel.dropDestination, "/");
        compare(fakeModel.dropMove, true);
        compare(fakeModel.dropConflictMode, 0);
        fakeModel.dropAccepted = false;
        verify(!rowAt(3).dropSelectedEntries(Qt.MoveAction));
        compare(fakeModel.dropSelectionCalls, 3);
    }

    function test_verticalRowScrollDoesNotStartTransfer() {
        let list = prepareScrollableRows(300);
        let row = rowAt(10);
        const initialContentY = list.contentY;
        mouseDrag(row, row.width / 2, row.height / 2, 0, -100, Qt.LeftButton, Qt.NoModifier, 10);
        tryVerify(function () {
            return list.contentY !== initialContentY;
        });
        compare(fakeModel.dropSelectionCalls, 0);
        compare(fakeModel.beginRubberBandCalls, 0);
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

    function test_listNavigationUpdatesFocusSelectionAndReveal() {
        populateRows(60);
        fakeModel.selectRow(5, Qt.NoModifier);
        let list = child("directoryList");
        list.forceActiveFocus();
        fakeModel.resetTelemetry();

        keyClick(Qt.Key_Down, Qt.ShiftModifier);
        compare(fakeModel.currentIndex, 6);
        compare(selectedRows().join(","), "5,6");

        keyClick(Qt.Key_Down, Qt.ControlModifier);
        compare(fakeModel.currentIndex, 7);
        compare(selectedRows().join(","), "5,6");

        const pageRows = Math.max(1, Math.floor(list.height / shellWindow.rowHeight));
        keyClick(Qt.Key_PageDown);
        const pageTarget = Math.min(fakeModel.count - 1, 7 + pageRows);
        compare(fakeModel.currentIndex, pageTarget);
        compare(selectedRows().join(","), String(pageTarget));
        verify(list.contentY > 0);
        keyClick(Qt.Key_PageUp, Qt.ControlModifier);
        compare(fakeModel.currentIndex, 7);
        compare(selectedRows().join(","), String(pageTarget));

        keyClick(Qt.Key_Home);
        compare(fakeModel.currentIndex, 0);
        compare(selectedRows().join(","), "0");
        keyClick(Qt.Key_End, Qt.ControlModifier);
        compare(fakeModel.currentIndex, fakeModel.count - 1);
        compare(selectedRows().join(","), "0");
        tryVerify(function () {
            return list.contentY > 0;
        });

        keyClick(Qt.Key_Return);
        compare(fakeModel.activatedRow, fakeModel.count - 1);
    }

    function test_typeAheadCyclesEditsWrapsTimesOutAndReveals() {
        populateNamedRows(["alpha.txt", "beta.txt", "bravo.txt", "berry.txt"]);
        let list = child("directoryList");
        list.forceActiveFocus();
        fakeModel.scriptedPrefixRows = [1, 2, 2, 2, 1, 79];

        keyClick(Qt.Key_B);
        compare(shellWindow.typeAheadBuffer.toLocaleLowerCase(), "b");
        compare(fakeModel.searchedPrefix.toLocaleLowerCase(), "b");
        compare(fakeModel.searchedWithCycling, true);
        compare(fakeModel.currentIndex, 1);
        compare(selectedRows().join(","), "1");

        keyClick(Qt.Key_B);
        compare(shellWindow.typeAheadBuffer.toLocaleLowerCase(), "b");
        compare(fakeModel.searchedPrefix.toLocaleLowerCase(), "b");
        compare(fakeModel.searchedWithCycling, true);
        compare(fakeModel.currentIndex, 2);
        keyClick(Qt.Key_R);
        compare(shellWindow.typeAheadBuffer.toLocaleLowerCase(), "br");
        compare(fakeModel.searchedPrefix.toLocaleLowerCase(), "br");
        compare(fakeModel.searchedWithCycling, false);
        compare(fakeModel.currentIndex, 2);
        keyClick(Qt.Key_Backspace);
        compare(shellWindow.typeAheadBuffer.toLocaleLowerCase(), "b");
        compare(fakeModel.searchedPrefix.toLocaleLowerCase(), "b");
        compare(fakeModel.searchedWithCycling, false);
        keyClick(Qt.Key_Escape);
        compare(shellWindow.typeAheadBuffer, "");

        fakeModel.selectRow(3, Qt.NoModifier);
        keyClick(Qt.Key_B);
        compare(fakeModel.searchedPrefix.toLocaleLowerCase(), "b");
        compare(fakeModel.searchedWithCycling, true);
        compare(fakeModel.currentIndex, 1);
        wait(shellWindow.typeAheadTimeoutMs + 100);
        compare(shellWindow.typeAheadBuffer, "");

        const names = [];
        for (let index = 0; index < 79; ++index) {
            names.push("sample-" + index + ".txt");
        }
        names.push("zebra.txt");
        populateNamedRows(names);
        list = child("directoryList");
        list.forceActiveFocus();
        keyClick(Qt.Key_Z);
        compare(fakeModel.searchedPrefix.toLocaleLowerCase(), "z");
        compare(fakeModel.searchedWithCycling, true);
        compare(fakeModel.currentIndex, 79);
        compare(selectedRows().join(","), "79");
        tryVerify(function () {
            return list.contentY > 0;
        });

        keyClick(Qt.Key_Escape);
        const currentBeforeEditing = fakeModel.currentIndex;
        const filter = child("filterField");
        filter.text = "";
        filter.forceActiveFocus();
        keyClick(Qt.Key_A);
        compare(shellWindow.typeAheadBuffer, "");
        compare(fakeModel.currentIndex, currentBeforeEditing);
        compare(filter.text.toLocaleLowerCase(), "a");

        fakeModel.requestRename();
        const renameDialog = child("renameDialog");
        tryCompare(renameDialog, "opened", true);
        const renameField = child("renameField");
        renameField.text = "";
        renameField.forceActiveFocus();
        keyClick(Qt.Key_B);
        compare(shellWindow.typeAheadBuffer, "");
        compare(fakeModel.currentIndex, currentBeforeEditing);
        compare(renameField.text.toLocaleLowerCase(), "b");
        renameDialog.close();
    }

    function test_listNavigationClearsTypeAhead() {
        let list = child("directoryList");
        list.forceActiveFocus();
        fakeModel.scriptedPrefixRows = [1];

        keyClick(Qt.Key_S);
        compare(shellWindow.typeAheadBuffer.toLocaleLowerCase(), "s");
        compare(fakeModel.currentIndex, 1);

        keyClick(Qt.Key_Down);
        compare(fakeModel.currentIndex, 2);
        compare(shellWindow.typeAheadBuffer, "");
    }

    function test_gridNavigationClearsTypeAhead() {
        mouseClick(child("gridViewButton"));
        let grid = child("directoryGrid");
        tryVerify(function () {
            return grid.activeFocus;
        });
        fakeModel.scriptedPrefixRows = [0];

        keyClick(Qt.Key_S);
        compare(shellWindow.typeAheadBuffer.toLocaleLowerCase(), "s");

        keyClick(Qt.Key_Right);
        compare(fakeModel.currentIndex, 1);
        compare(shellWindow.typeAheadBuffer, "");
    }

    function test_unboundModifiedKeyDoesNotStartTypeAhead() {
        let list = child("directoryList");
        list.forceActiveFocus();
        const currentBeforeShortcut = fakeModel.currentIndex;

        keyClick(Qt.Key_K, Qt.ControlModifier);

        compare(shellWindow.typeAheadBuffer, "");
        compare(fakeModel.currentIndex, currentBeforeShortcut);
        compare(fakeModel.prefixSearchCalls, 0);
        compare(fakeModel.moveCursorCalls, 0);
        compare(fakeModel.moveCursorToCalls, 0);
    }

    function test_gridDelegatesAreVirtualizedAndRequestThumbnails() {
        populateRows(200);
        fakeModel.resetTelemetry();
        let list = child("directoryList");
        list.forceActiveFocus();
        keyClick(Qt.Key_2, Qt.ControlModifier | Qt.ShiftModifier);
        let grid = child("directoryGrid");
        tryCompare(grid, "visible", true);
        verify(grid.count > 0);
        tryVerify(function () {
            return fakeModel.requestThumbnailCalls > 0;
        });
        verify(fakeModel.requestThumbnailCalls < fakeModel.count);
        const realized = realizedGridCellCount(fakeModel.count);
        verify(realized > 0);
        verify(realized < fakeModel.count);
        verify(cellAt(0) !== null);
    }

    function test_gridPointerActivationAndSelection() {
        mouseClick(child("gridViewButton"));
        let grid = child("directoryGrid");
        tryCompare(grid, "visible", true);
        verify(grid.count > 0);

        clickCell(1, Qt.LeftButton, Qt.ControlModifier);
        tryCompare(fakeModel, "selectedRow", 1);
        verify((fakeModel.selectedModifiers & Qt.ControlModifier) !== 0);

        let cell = cellAt(2);
        mouseDoubleClickSequence(cell, cell.width / 2, cell.height / 2, Qt.LeftButton, Qt.NoModifier);
        tryCompare(fakeModel, "activatedRow", 2);

        clickCell(3, Qt.RightButton, Qt.NoModifier);
        tryCompare(fakeModel, "selectedRow", 3);
        keyClick(Qt.Key_Escape);
    }

    function test_gridRubberBandUsesTwoDimensionalRows() {
        populateRows(40);
        mouseClick(child("gridViewButton"));
        let grid = child("directoryGrid");
        tryCompare(grid, "visible", true);
        verify(grid.count > 0);
        fakeModel.resetTelemetry();

        let gutter = child("gridRubberBandGutter");
        tryVerify(function () {
            return gutter.width > 4 && gutter.height > 200;
        });
        mouseDrag(gutter, gutter.width / 2, 20, -220, 190, Qt.LeftButton, Qt.NoModifier, 10);
        tryCompare(fakeModel, "beginRubberBandCalls", 1);
        verify(fakeModel.updateRubberBandCalls > 0);
        compare(fakeModel.endRubberBandCalls, 1);
        verify(fakeModel.rubberBandRows.length > 2);
        let containsGap = false;
        for (let index = 1; index < fakeModel.rubberBandRows.length; ++index) {
            if (fakeModel.rubberBandRows[index] - fakeModel.rubberBandRows[index - 1] > 1) {
                containsGap = true;
            }
        }
        verify(containsGap);
    }

    function test_gridCellDragScrollsWithoutSelecting() {
        populateRows(60);
        mouseClick(child("gridViewButton"));
        let grid = child("directoryGrid");
        tryCompare(grid, "visible", true);
        verify(grid.count > 0);
        grid.interactive = false;
        grid.contentY = 154;
        grid.interactive = true;
        tryCompare(grid, "contentY", 154);
        fakeModel.resetTelemetry();

        let cell = cellAt(14);
        const initialContentY = grid.contentY;
        mouseDrag(cell, cell.width / 2, cell.height / 2, 0, -100, Qt.LeftButton, Qt.NoModifier, 10);
        tryVerify(function () {
            return grid.contentY !== initialContentY;
        });
        compare(fakeModel.selectRowCalls, 0);
        compare(fakeModel.beginRubberBandCalls, 0);
        compare(fakeModel.dropSelectionCalls, 0);
    }

    function test_gridKeyboardNavigationAfterShortcutSwitch() {
        let list = child("directoryList");
        list.forceActiveFocus();
        keyClick(Qt.Key_2, Qt.ControlModifier | Qt.ShiftModifier);
        let grid = child("directoryGrid");
        tryCompare(grid, "visible", true);
        verify(grid.count > 0);
        tryVerify(function () {
            return grid.activeFocus;
        });
        fakeModel.resetTelemetry();
        keyClick(Qt.Key_Right);
        tryCompare(fakeModel, "moveCursorToCalls", 1);
        compare(fakeModel.movedToRow, 1);
        compare(fakeModel.currentIndex, 1);
        compare(selectedRows().join(","), "1");
    }

    function test_gridKeyboardContextMenuRestoresViewFocus() {
        mouseClick(child("gridViewButton"));
        let grid = child("directoryGrid");
        tryVerify(function () {
            return grid.activeFocus;
        });
        fakeModel.selectRow(2, Qt.NoModifier);
        fakeModel.resetTelemetry();
        keyClick(Qt.Key_Menu);
        let menu = child("gridKeyboardContextMenu");
        tryCompare(menu, "opened", true);
        compare(fakeModel.movedToRow, 2);
        compare(fakeModel.movedPreservingSelection, true);
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return grid.activeFocus;
        });
    }

    function test_gridNavigationUsesColumnsAndPreservesSelection() {
        populateRows(60);
        mouseClick(child("gridViewButton"));
        let grid = child("directoryGrid");
        tryVerify(function () {
            return grid.activeFocus;
        });
        const columns = Math.max(1, Math.floor(grid.width / 144));
        verify(columns > 1);

        fakeModel.selectRow(columns + 1, Qt.NoModifier);
        keyClick(Qt.Key_Up);
        compare(fakeModel.currentIndex, 1);
        compare(selectedRows().join(","), "1");
        keyClick(Qt.Key_Down);
        compare(fakeModel.currentIndex, columns + 1);
        keyClick(Qt.Key_Left);
        compare(fakeModel.currentIndex, columns);
        keyClick(Qt.Key_Right);
        compare(fakeModel.currentIndex, columns + 1);

        fakeModel.selectRow(columns - 1, Qt.NoModifier);
        keyClick(Qt.Key_Right);
        compare(fakeModel.currentIndex, columns - 1);

        const lastGridRow = Math.floor((fakeModel.count - 1) / columns);
        const highColumnInPreviousRow = (lastGridRow - 1) * columns + columns - 1;
        fakeModel.selectRow(highColumnInPreviousRow, Qt.NoModifier);
        keyClick(Qt.Key_Down);
        compare(fakeModel.currentIndex, highColumnInPreviousRow);

        fakeModel.selectRow(1, Qt.NoModifier);
        keyClick(Qt.Key_Down, Qt.ShiftModifier);
        compare(fakeModel.currentIndex, columns + 1);
        compare(fakeModel.selectedCount, columns + 1);
        keyClick(Qt.Key_Down, Qt.ControlModifier);
        compare(fakeModel.currentIndex, columns * 2 + 1);
        compare(fakeModel.selectedCount, columns + 1);

        const pageRows = Math.max(1, Math.floor(grid.height / 154));
        const beforePage = fakeModel.currentIndex;
        keyClick(Qt.Key_PageDown);
        const pageTarget = Math.min(fakeModel.count - 1, beforePage + pageRows * columns);
        compare(fakeModel.currentIndex, pageTarget);
        compare(selectedRows().join(","), String(pageTarget));
        keyClick(Qt.Key_PageUp, Qt.ControlModifier);
        compare(fakeModel.currentIndex, beforePage);
        compare(selectedRows().join(","), String(pageTarget));

        keyClick(Qt.Key_End);
        compare(fakeModel.currentIndex, fakeModel.count - 1);
        compare(selectedRows().join(","), String(fakeModel.count - 1));
        tryVerify(function () {
            return grid.contentY > 0;
        });
        keyClick(Qt.Key_Return);
        compare(fakeModel.activatedRow, fakeModel.count - 1);
    }

    function test_tabAndViewShortcutsDoNotConflictAndButtonsKeepFocusUsable() {
        fakeModel.tabCount = 3;
        let list = child("directoryList");
        list.forceActiveFocus();

        keyClick(Qt.Key_2, Qt.ControlModifier);
        compare(fakeModel.activeTab, 1);
        compare(shellWindow.gridMode, false);

        keyClick(Qt.Key_2, Qt.ControlModifier | Qt.ShiftModifier);
        let grid = child("directoryGrid");
        tryCompare(grid, "visible", true);
        tryVerify(function () {
            return grid.activeFocus;
        });
        let listButton = child("listViewButton");
        let gridButton = child("gridViewButton");
        verify(listButton.enabled);
        verify(gridButton.enabled);
        compare(listButton.checkable, true);
        compare(gridButton.checkable, true);
        compare(listButton.checked, false);
        compare(gridButton.checked, true);

        mouseClick(listButton);
        tryCompare(list, "visible", true);
        tryVerify(function () {
            return list.activeFocus;
        });
        verify(listButton.enabled);
        verify(gridButton.enabled);
        compare(listButton.checked, true);
        compare(gridButton.checked, false);
    }

    function test_numberedTabShortcutBeyondOpenTabsIsDisabled() {
        fakeModel.tabCount = 3;
        fakeModel.activeTab = 1;
        waitForRendering(shellWindow.contentItem);
        let fourthTabShortcut = shortcutForSequence("Ctrl+4");
        verify(fourthTabShortcut !== null);
        tryCompare(fourthTabShortcut, "enabled", false);
        let list = child("directoryList");
        list.forceActiveFocus();
        fakeModel.resetTelemetry();

        keyClick(Qt.Key_4, Qt.ControlModifier);

        compare(fakeModel.activateTabCalls, 0);
        compare(fakeModel.activeTab, 1);
    }

    function test_viewSwitchingKeyboardAndMousePreservesSelection() {
        populateRows(60);
        let list = child("directoryList");
        list.interactive = false;
        list.contentY = 204;
        list.interactive = true;
        tryCompare(list, "contentY", 204);
        clickRow(10, Qt.LeftButton, Qt.NoModifier);
        list.forceActiveFocus();
        keyClick(Qt.Key_2, Qt.ControlModifier | Qt.ShiftModifier);
        let grid = child("directoryGrid");
        tryCompare(grid, "visible", true);
        verify(grid.count > 0);
        compare(fakeModel.currentIndex, 10);
        compare(fakeModel.selectedRow, 10);
        grid.interactive = false;
        grid.contentY = 154;
        grid.interactive = true;
        tryCompare(grid, "contentY", 154);

        mouseClick(child("listViewButton"));
        tryCompare(list, "visible", true);
        compare(fakeModel.currentIndex, 10);
        compare(fakeModel.selectedRow, 10);
        compare(list.contentY, 204);

        mouseClick(child("gridViewButton"));
        tryCompare(grid, "visible", true);
        compare(fakeModel.currentIndex, 10);
        compare(fakeModel.selectedRow, 10);
        compare(grid.contentY, 154);

        const columns = Math.max(1, Math.floor(grid.width / 144));
        keyClick(Qt.Key_Down, Qt.ShiftModifier);
        compare(fakeModel.currentIndex, 10 + columns);
        compare(fakeModel.selectedCount, columns + 1);

        mouseClick(child("listViewButton"));
        tryVerify(function () {
            return list.activeFocus;
        });
        keyClick(Qt.Key_Down, Qt.ShiftModifier);
        compare(fakeModel.currentIndex, 11 + columns);
        compare(fakeModel.selectedCount, columns + 2);
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

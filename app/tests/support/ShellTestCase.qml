pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea

TestCase {
    id: testCase

    name: "InputParity"

    property var shellWindow
    property alias fakeModel: fakeModelObject

    ListModel {
        id: fakeModelObject

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
        property int navigateFromInputCalls: 0
        property string navigationInput: ""
        property bool navigationInputAccepted: true
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
        function navigateFromInput(input) {
            navigateFromInputCalls += 1;
            navigationInput = input;
            if (navigationInputAccepted) {
                path = input;
            }
            return navigationInputAccepted;
        }
        function navigationCompletion(input) {
            if (input.endsWith("/sa")) {
                return {
                    "completed": input + "mple/",
                    "suffix": "mple/",
                    "candidates": ["sample"]
                };
            }
            return {
                "completed": input,
                "suffix": "",
                "candidates": []
            };
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
            navigateFromInputCalls = 0;
            navigationInput = "";
            navigationInputAccepted = true;
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
            shellModel: fakeModelObject
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
        fakeModelObject.path = "/sample";
        fakeModelObject.tabCount = 1;
        fakeModelObject.activeTab = 0;
        fakeModelObject.operationErrorString = "";
        shellWindow.gridMode = false;
        shellWindow.clearTypeAhead();
        const pathNavigator = findChild(shellWindow.contentItem, "pathNavigator");
        pathNavigator.editing = false;
        pathNavigator.retainedDraft = false;
        pathNavigator.draftText = fakeModelObject.path;
        const locationsPopup = findChild(shellWindow, "locationsPopup");
        if (locationsPopup !== null) {
            locationsPopup.close();
        }
        populateRows(4);
        fakeModelObject.resetTelemetry();
    }

    function cleanupTestCase() {
        shellWindow.destroy();
    }

    function populateRows(count) {
        let list = child("directoryList");
        list.interactive = false;
        list.cancelFlick();
        list.contentY = 0;
        fakeModelObject.clear();
        for (let index = 0; index < count; ++index) {
            fakeModelObject.append({
                "name": "sample-" + index + ".txt",
                "isDir": false,
                "isSymlink": false,
                "size": 12 + index,
                "selected": false,
                "recoveryEntry": false,
                "entryPath": "/sample/sample-" + index + ".txt",
                "thumbnailSource": "",
                "thumbnailLoading": false
            });
        }
        fakeModelObject.currentIndex = count > 0 ? 0 : -1;
        fakeModelObject.selectedCount = 0;
        fakeModelObject.selectionAnchor = count > 0 ? 0 : -1;

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
        fakeModelObject.clear();
        for (let index = 0; index < names.length; ++index) {
            fakeModelObject.append({
                "name": names[index],
                "isDir": false,
                "isSymlink": false,
                "size": 12 + index,
                "selected": false,
                "recoveryEntry": false,
                "entryPath": "/sample/" + names[index],
                "thumbnailSource": "",
                "thumbnailLoading": false
            });
        }
        fakeModelObject.currentIndex = names.length > 0 ? 0 : -1;
        fakeModelObject.selectedCount = 0;
        fakeModelObject.selectionAnchor = names.length > 0 ? 0 : -1;
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
        const table = findChild(shellWindow, "shortcutTable");
        if (table === null) {
            return null;
        }
        for (let index = 0; index < table.count; ++index) {
            const candidate = table.objectAt(index);
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
        for (let row = 0; row < fakeModelObject.count; ++row) {
            if (fakeModelObject.get(row).selected) {
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
        tryCompare(fakeModelObject, "beginRubberBandCalls", 1);
        verify(fakeModelObject.updateRubberBandCalls > 0);
        compare(fakeModelObject.endRubberBandCalls, 1);
        compare(fakeModelObject.dropSelectionCalls, 0);
        if (expectRows) {
            verify(fakeModelObject.rubberBandRows.length > 0);
            verify(fakeModelObject.rubberBandCurrentRow >= 0);
        } else {
            compare(fakeModelObject.rubberBandRows.length, 0);
            compare(fakeModelObject.rubberBandCurrentRow, -1);
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
        fakeModelObject.resetTelemetry();
        return list;
    }
}

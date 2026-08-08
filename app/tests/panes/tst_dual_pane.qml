// End-to-end dual-pane parity: two independent model stand-ins drive the real
// Main scene, registry shortcut table, pointer controls, and bounded splitter.
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea

TestCase {
    id: testCase

    name: "DualPane"
    when: windowShown

    property var shellWindow: null

    component PaneModel: ListModel {
        property string path: "/synthetic"
        property string errorString: ""
        property string filterText: ""
        property string statusMessage: ""
        property bool busy: false
        property bool showHidden: false
        property bool canGoBack: true
        property bool canGoForward: false
        property bool canGoUp: true
        property int sortMode: 0
        property int currentIndex: -1
        property int selectedCount: 0
        property var selectedFileUrls: []
        property int tabCount: 1
        property int activeTab: 0
        property int paneCount: 1
        property int activePane: 0
        property bool operationBusy: false
        property string operationErrorString: ""
        property var calls: []
        property int dropCalls: 0
        property string dropDestination: ""
        property bool dropMove: false

        signal filesystemOperationRequested(string operation, var paths)

        function record(call) {
            const next = calls;
            next.push(call);
            calls = next;
        }
        function refresh() {
            record("refresh");
        }
        function goBack() {
            record("goBack");
            path += "/back";
        }
        function goForward() {
            record("goForward");
        }
        function goUp() {
            record("goUp");
        }
        function activate(row) {
            record("activate:" + row);
        }
        function activateCurrent() {
            activate(currentIndex);
        }
        function navigateToPath(destination) {
            path = destination;
            record("navigateToPath:" + destination);
        }
        function navigateFromInput(input) {
            navigateToPath(input);
            return true;
        }
        function navigationCompletion(input) {
            return {
                "completed": input,
                "suffix": "",
                "candidates": []
            };
        }
        function breadcrumbSegments() {
            return [
                {
                    "label": path,
                    "path": path,
                    "url": "file://" + path
                }
            ];
        }
        function rowSelected(row) {
            return false;
        }
        function rowIsDirectory(row) {
            return false;
        }
        function canDropSelection(destination, move) {
            return selectedCount > 0 && destination.length > 0 && !operationBusy;
        }
        function dropSelection(destination, move, conflictMode) {
            dropCalls += 1;
            dropDestination = destination;
            dropMove = move;
            record("drop:" + destination + ":" + move + ":" + conflictMode);
            return true;
        }
        function requestThumbnail(row) {
        }
        function releaseThumbnail(entryPath) {
        }
        function selectRow(row, modifiers) {
        }
        function moveCursor(delta, extendSelection, preserveSelection) {
        }
        function moveCursorTo(row, extendSelection, preserveSelection) {
        }
        function selectByPrefix(prefix, cycle) {
            return false;
        }
        function toggleCurrent() {
        }
        function selectAll() {
            record("selectAll");
        }
        function clearSelection() {
            selectedCount = 0;
        }
        function beginRubberBand(additive) {
        }
        function updateRubberBandSelection(rows, currentRow) {
        }
        function endRubberBand() {
        }
        function tabLabel(index) {
            return "tab " + index;
        }
        function addTab() {
            tabCount += 1;
        }
        function closeTab(index) {
        }
        function activateTab(index) {
            activeTab = index;
        }
        function requestCopy() {
            record("requestCopy");
        }
        function requestMove() {
            record("requestMove");
        }
        function requestRename() {
            record("requestRename");
        }
        function requestTrash() {
            record("requestTrash");
        }
        function performCopy(destination, conflictMode) {
            record("performCopy:" + destination + ":" + conflictMode);
        }
        function performMove(destination, conflictMode) {
            record("performMove:" + destination + ":" + conflictMode);
        }
        function performRename(name, conflictMode) {
            record("performRename:" + name + ":" + conflictMode);
        }
        function performTrash() {
            record("performTrash");
        }
        function resetTelemetry() {
            calls = [];
            dropCalls = 0;
            dropDestination = "";
            dropMove = false;
        }
    }

    PaneModel {
        id: leftModel
        path: "/synthetic/left"
    }

    PaneModel {
        id: rightModel
        path: "/synthetic/right"
    }

    Component {
        id: shellComponent

        Main {
            shellModel: leftModel
            secondaryShellModel: rightModel
        }
    }

    function child(name) {
        const item = findChild(shellWindow, name);
        verify(item !== null, "missing " + name);
        return item;
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
        leftModel.path = "/synthetic/left";
        rightModel.path = "/synthetic/right";
        leftModel.sortMode = 0;
        rightModel.sortMode = 2;
        leftModel.filterText = "left-only";
        rightModel.filterText = "right-only";
        leftModel.selectedCount = 1;
        rightModel.selectedCount = 1;
        leftModel.operationBusy = false;
        rightModel.operationBusy = false;
        leftModel.resetTelemetry();
        rightModel.resetTelemetry();
        shellWindow.shellTheme.dualPaneEnabled = true;
        shellWindow.shellTheme.splitRatio = 0.5;
        shellWindow.activePaneIndex = 0;
        wait(0);
        waitForRendering(shellWindow.contentItem);
    }

    function cleanupTestCase() {
        shellWindow.destroy();
    }

    function test_bothPanesStayLiveAndActionsFollowActivation() {
        compare(shellWindow.paneCount, 2);
        verify(child("firstPane").visible);
        verify(child("secondPane").visible);
        compare(shellWindow.activeShellModel, leftModel);

        keySequence("F5");
        tryVerify(function () {
            return leftModel.calls.join(",") === "refresh";
        });
        compare(rightModel.calls.length, 0);

        keySequence("F6");
        tryCompare(shellWindow, "activePaneIndex", 1);
        compare(shellWindow.activeShellModel, rightModel);
        keySequence("F5");
        tryVerify(function () {
            return rightModel.calls.join(",") === "refresh";
        });

        // Pointer activation returns to the first pane without collapsing the
        // independent location, selection, filter, sort, or history state.
        mouseClick(child("paneHeader-0"));
        tryCompare(shellWindow, "activePaneIndex", 0);
        compare(leftModel.path, "/synthetic/left");
        compare(rightModel.path, "/synthetic/right");
        compare(leftModel.filterText, "left-only");
        compare(rightModel.filterText, "right-only");
        compare(leftModel.sortMode, 0);
        compare(rightModel.sortMode, 2);
        compare(leftModel.selectedCount, 1);
        compare(rightModel.selectedCount, 1);

        keySequence("Alt+Left");
        tryCompare(leftModel, "path", "/synthetic/left/back");
        compare(rightModel.path, "/synthetic/right");
    }

    function test_accessibleViewNamesExposeActivePane() {
        const leftList = findChild(child("directoryPane-0"), "directoryList");
        const rightList = findChild(child("directoryPane-1"), "directoryList");
        verify(leftList !== null);
        verify(rightList !== null);
        leftList.forceActiveFocus();
        tryVerify(function () {
            return leftList.activeFocus;
        });
        compare(leftList.Accessible.role, Accessible.List);
        compare(rightList.Accessible.role, Accessible.List);
        verify(leftList.Accessible.name.length > 0);
        verify(rightList.Accessible.name.length > 0);
        verify(leftList.Accessible.name !== rightList.Accessible.name);
        const activeName = leftList.Accessible.name;
        const inactiveName = rightList.Accessible.name;

        keySequence("F6");
        tryCompare(shellWindow, "activePaneIndex", 1);
        tryVerify(function () {
            return rightList.activeFocus;
        });
        compare(rightList.Accessible.name, activeName);
        compare(leftList.Accessible.name, inactiveName);
    }

    function test_toggleHasKeyboardAndPointerPaths() {
        keySequence("F3");
        tryCompare(shellWindow, "paneCount", 1);
        verify(!child("secondPaneLoader").active);

        mouseClick(child("paneToggleButton"));
        tryCompare(shellWindow, "paneCount", 2);
        verify(child("secondPane").visible);
    }

    function test_resizeHasKeyboardAndPointerPathsAndBounds() {
        const layout = child("paneSplitter").parent;
        const initial = layout.splitRatio;
        keySequence("Ctrl+Alt+Right");
        tryVerify(function () {
            return layout.splitRatio > initial;
        });
        compare(shellWindow.shellTheme.splitRatio, layout.splitRatio);

        const handle = child("paneSplitterHandle");
        const beforePointer = layout.splitRatio;
        mousePress(handle, handle.width / 2, handle.height / 2, Qt.LeftButton);
        mouseMove(handle, handle.width / 2 - 90, handle.height / 2);
        mouseRelease(handle, handle.width / 2 - 90, handle.height / 2, Qt.LeftButton);
        tryVerify(function () {
            return layout.splitRatio < beforePointer;
        });
        compare(shellWindow.shellTheme.splitRatio, layout.splitRatio);

        for (let step = 0; step < 30; ++step) {
            shellWindow.actions.trigger("pane.resizeLeft", shellWindow.actions.globalContext(undefined));
        }
        verify(child("firstPane").width >= layout.minimumPaneWidth - 1);
        verify(child("secondPane").width >= layout.minimumPaneWidth - 1);
    }

    function test_copyAndMoveBothDirectionsHaveKeyboardAndPointerPaths() {
        // Left to right, keyboard copy and pointer move.
        keySequence("Ctrl+Shift+C");
        tryCompare(leftModel, "dropCalls", 1);
        compare(leftModel.dropDestination, rightModel.path);
        compare(leftModel.dropMove, false);
        mouseClick(child("paneMoveToOther-0"));
        tryCompare(leftModel, "dropCalls", 2);
        compare(leftModel.dropMove, true);

        // Left to right, pointer copy and keyboard move.
        const leftCopyButton = child("paneCopyToOther-0");
        compare(leftCopyButton.actionId, "pane.copyToOther");
        mouseClick(leftCopyButton);
        tryCompare(leftModel, "dropCalls", 3);
        compare(leftModel.calls[leftModel.calls.length - 1], "drop:/synthetic/right:false:0");
        compare(leftModel.dropMove, false);
        keySequence("Ctrl+Shift+M");
        tryCompare(leftModel, "dropCalls", 4);
        compare(leftModel.dropMove, true);

        // Right to left repeats both operations through both input paths.
        mouseClick(child("paneHeader-1"));
        tryCompare(shellWindow, "activePaneIndex", 1);
        waitForRendering(shellWindow.contentItem);
        keySequence("Ctrl+Shift+C");
        tryCompare(rightModel, "dropCalls", 1);
        compare(rightModel.dropDestination, leftModel.path);
        compare(rightModel.dropMove, false);
        mouseClick(child("paneMoveToOther-1"));
        tryCompare(rightModel, "dropCalls", 2);
        compare(rightModel.dropMove, true);
        waitForRendering(shellWindow.contentItem);
        const rightCopyButton = child("paneCopyToOther-1");
        compare(rightCopyButton.actionId, "pane.copyToOther");
        mouseClick(rightCopyButton);
        tryCompare(rightModel, "dropCalls", 3);
        compare(rightModel.calls[rightModel.calls.length - 1], "drop:" + leftModel.path + ":false:0");
        compare(rightModel.dropMove, false);
        keySequence("Ctrl+Shift+M");
        tryCompare(rightModel, "dropCalls", 4);
        compare(rightModel.dropMove, true);
    }
}

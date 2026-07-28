import QtQuick
import QtTest
import "../qml"

pragma ComponentBehavior: Bound

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
        property int firstRubberBandRow: -1
        property int lastRubberBandRow: -1
        property int endRubberBandCalls: 0

        function activate(row) {
            activateCalls += 1
            activatedRow = row
        }
        function activatePane(pane) {}
        function activateTab(tab) {}
        function addTab() {}
        function beginRubberBand(additive) {
            beginRubberBandCalls += 1
            rubberBandAdditive = additive
        }
        function clearSelection() {
            clearSelectionCalls += 1
        }
        function closeTab(tab) {}
        function endRubberBand() {
            endRubberBandCalls += 1
        }
        function goBack() {}
        function goForward() {}
        function goUp() {}
        function moveCursor(delta, extendSelection, preserveSelection) {
            moveCursorCalls += 1
            moveCursorDelta = delta
            currentIndex = Math.max(0, Math.min(count - 1, currentIndex + delta))
        }
        function moveCursorTo(row, extendSelection, preserveSelection) {}
        function refresh() {}
        function requestCopy() {}
        function requestMove() {}
        function requestRename() {}
        function requestTrash() {}
        function selectAll() {}
        function setDualPaneEnabled(enabled) {}
        function tabLabel(tab) {
            return "Sample"
        }
        function toggleCurrent() {
            toggleCurrentCalls += 1
        }
        function updateRubberBand(firstRow, lastRow) {
            updateRubberBandCalls += 1
            firstRubberBandRow = firstRow
            lastRubberBandRow = lastRow
        }

        function selectRow(row, modifiers) {
            selectRowCalls += 1
            selectedRow = row
            selectedModifiers = modifiers
            currentIndex = row
            selectedCount = 1
            setProperty(row, "selected", true)
        }

        function resetTelemetry() {
            selectRowCalls = 0
            selectedRow = -1
            selectedModifiers = Qt.NoModifier
            activateCalls = 0
            activatedRow = -1
            moveCursorCalls = 0
            moveCursorDelta = 0
            toggleCurrentCalls = 0
            clearSelectionCalls = 0
            beginRubberBandCalls = 0
            rubberBandAdditive = false
            updateRubberBandCalls = 0
            firstRubberBandRow = -1
            lastRubberBandRow = -1
            endRubberBandCalls = 0
        }
    }

    Component {
        id: shellComponent

        Main {
            shellModel: fakeModel
        }
    }

    function initTestCase() {
        shellWindow = shellComponent.createObject(null)
        verify(shellWindow !== null)
        tryVerify(function() {
            return shellWindow.visible
        })
        waitForRendering(shellWindow.contentItem)
    }

    function init() {
        populateRows(4)
        fakeModel.resetTelemetry()
    }

    function cleanupTestCase() {
        shellWindow.destroy()
    }

    function populateRows(count) {
        fakeModel.clear()
        for (let index = 0; index < count; ++index) {
            fakeModel.append({
                "name": "sample-" + index + ".txt",
                "isDir": false,
                "size": 12 + index,
                "selected": false
            })
        }
        fakeModel.currentIndex = count > 0 ? 0 : -1
        fakeModel.selectedCount = 0

        let list = child("directoryList")
        tryCompare(list, "count", count)
        list.positionViewAtBeginning()
        waitForRendering(shellWindow.contentItem)
    }

    function child(objectName) {
        let item = findChild(shellWindow.contentItem, objectName)
        tryVerify(function() {
            return item !== null
        })
        return item
    }

    function rowAt(index) {
        let row = child("entryRow-" + index)
        tryVerify(function() {
            return row.width > 0 && row.height > 0
        })
        return row
    }

    function clickRow(index, button, modifiers) {
        let row = rowAt(index)
        mouseClick(row, row.width / 2, row.height / 2, button, modifiers)
    }

    function verifyBandDrag(area) {
        tryVerify(function() {
            return area.width > 4 && area.height > 80
        })
        mouseDrag(area, area.width / 2, 10, 0, 60, Qt.LeftButton, Qt.NoModifier, 10)
        tryCompare(fakeModel, "beginRubberBandCalls", 1)
        verify(fakeModel.updateRubberBandCalls > 0)
        compare(fakeModel.endRubberBandCalls, 1)
        verify(fakeModel.firstRubberBandRow >= 0)
        verify(fakeModel.lastRubberBandRow >= fakeModel.firstRubberBandRow)
    }

    function test_blankAreaRubberBand() {
        verifyBandDrag(child("rubberBandBlankArea"))
    }

    function test_ctrlClickReachesSelectionModel() {
        clickRow(1, Qt.LeftButton, Qt.ControlModifier)
        tryCompare(fakeModel, "selectRowCalls", 1)
        compare(fakeModel.selectedRow, 1)
        verify((fakeModel.selectedModifiers & Qt.ControlModifier) !== 0)
    }

    function test_doubleClickReachesActivationModel() {
        let row = rowAt(2)
        mouseDoubleClickSequence(row, row.width / 2, row.height / 2,
                                 Qt.LeftButton, Qt.NoModifier)
        tryCompare(fakeModel, "activateCalls", 1)
        compare(fakeModel.activatedRow, 2)
    }

    function test_filledViewportRubberBand() {
        populateRows(40)
        fakeModel.resetTelemetry()
        let blankArea = child("rubberBandBlankArea")
        tryCompare(blankArea, "height", 0)
        verifyBandDrag(child("rubberBandGutter"))
    }

    function test_keyboardSelectionPaths() {
        let list = child("directoryList")
        list.forceActiveFocus()
        tryVerify(function() {
            return list.activeFocus
        })
        keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Space)
        keyClick(Qt.Key_Escape)
        tryCompare(fakeModel, "moveCursorCalls", 1)
        compare(fakeModel.moveCursorDelta, 1)
        compare(fakeModel.toggleCurrentCalls, 1)
        compare(fakeModel.clearSelectionCalls, 1)
    }

    function test_plainClickReachesSelectionModel() {
        clickRow(0, Qt.LeftButton, Qt.NoModifier)
        tryCompare(fakeModel, "selectRowCalls", 1)
        compare(fakeModel.selectedRow, 0)
        compare(fakeModel.selectedModifiers, Qt.NoModifier)
    }

    function test_rightClickReachesSelectionModel() {
        clickRow(1, Qt.RightButton, Qt.NoModifier)
        tryCompare(fakeModel, "selectRowCalls", 1)
        compare(fakeModel.selectedRow, 1)
        keyClick(Qt.Key_Escape)
    }

    function test_shiftClickReachesSelectionModel() {
        clickRow(2, Qt.LeftButton, Qt.ShiftModifier)
        tryCompare(fakeModel, "selectRowCalls", 1)
        compare(fakeModel.selectedRow, 2)
        verify((fakeModel.selectedModifiers & Qt.ShiftModifier) !== 0)
    }
}

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

        function activate(row) {}
        function activatePane(pane) {}
        function activateTab(tab) {}
        function addTab() {}
        function beginRubberBand(additive) {}
        function clearSelection() {}
        function closeTab(tab) {}
        function endRubberBand() {}
        function goBack() {}
        function goForward() {}
        function goUp() {}
        function moveCursor(delta, extendSelection, preserveSelection) {}
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
        function toggleCurrent() {}
        function updateRubberBand(firstRow, lastRow) {}

        function selectRow(row, modifiers) {
            selectRowCalls += 1
            selectedRow = row
            currentIndex = row
            selectedCount = 1
            setProperty(row, "selected", true)
        }
    }

    Component {
        id: shellComponent

        Main {
            shellModel: fakeModel
        }
    }

    function initTestCase() {
        fakeModel.append({
            "name": "sample.txt",
            "isDir": false,
            "size": 12,
            "selected": false
        })
        shellWindow = shellComponent.createObject(null)
        verify(shellWindow !== null)
        tryVerify(function() {
            return shellWindow.visible
        })
        waitForRendering(shellWindow.contentItem)
    }

    function cleanupTestCase() {
        shellWindow.destroy()
    }

    function test_rowLeftClickReachesSelectionModel() {
        let row = findChild(shellWindow.contentItem, "entryRow-0")
        tryVerify(function() {
            return row !== null && row.width > 0 && row.height > 0
        })

        mouseClick(row, row.width / 2, row.height / 2, Qt.LeftButton, Qt.NoModifier)

        tryCompare(fakeModel, "selectRowCalls", 1)
        compare(fakeModel.selectedRow, 0)
    }
}

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtTest
import OdySea

Item {
    id: harness

    width: 1100
    height: 760

    ShellTheme {
        id: theme
    }

    ListModel {
        id: usageModel

        property string rootPath: "/synthetic/usage"
        property bool busy: false
        property bool cancelling: false
        property bool cancelled: false
        property string errorString: ""
        property int currentIndex: 0
        property bool canGoUp: true
        property double entriesVisited: 24
        property double apparentBytes: 12288
        property double allocatedBytes: 16384
        property double fileCount: 7
        property double directoryCount: 3
        property double unreadableDirectories: 0
        property double deduplicatedEntries: 2
        property double skippedBoundaries: 1
        property int startCalls: 0
        property int cancelCalls: 0
        property int selectCalls: 0
        property int activateCalls: 0
        property int upCalls: 0
        property string startedPath: ""
        property int activatedRow: -1

        function resetFixture() {
            clear();
            append({
                "name": "Archive",
                "entryPath": "/synthetic/usage/Archive",
                "isDirectory": true,
                "kindLabel": "folder",
                "apparentBytes": 8192,
                "allocatedBytes": 12288,
                "apparentText": "8.0 KiB",
                "allocatedText": "12.0 KiB",
                "fileCount": 5,
                "directoryCount": 2,
                "deduplicatedEntries": 2,
                "finished": true,
                "selected": true
            });
            append({
                "name": "Notes.txt",
                "entryPath": "/synthetic/usage/Notes.txt",
                "isDirectory": false,
                "kindLabel": "file",
                "apparentBytes": 4096,
                "allocatedBytes": 4096,
                "apparentText": "4.0 KiB",
                "allocatedText": "4.0 KiB",
                "fileCount": 1,
                "directoryCount": 0,
                "deduplicatedEntries": 0,
                "finished": true,
                "selected": false
            });
            rootPath = "/synthetic/usage";
            busy = false;
            cancelling = false;
            cancelled = false;
            errorString = "";
            currentIndex = 0;
            startCalls = 0;
            cancelCalls = 0;
            selectCalls = 0;
            activateCalls = 0;
            upCalls = 0;
            startedPath = "";
            activatedRow = -1;
        }

        function start(path) {
            startCalls += 1;
            startedPath = path;
            rootPath = path;
            busy = true;
            cancelled = false;
        }

        function cancel() {
            if (!busy) {
                return;
            }
            cancelCalls += 1;
            busy = false;
            cancelling = false;
            cancelled = true;
        }

        function selectRow(row) {
            if (row < 0 || row >= count) {
                return;
            }
            selectCalls += 1;
            setProperty(currentIndex, "selected", false);
            currentIndex = row;
            setProperty(currentIndex, "selected", true);
        }

        function moveCursor(delta) {
            selectRow(Math.max(0, Math.min(count - 1, currentIndex + delta)));
        }

        function activate(row) {
            activateCalls += 1;
            activatedRow = row;
        }

        function activateCurrent() {
            activate(currentIndex);
        }

        function goUp() {
            upCalls += 1;
        }

        function formatBytes(bytes) {
            return bytes + " B";
        }
    }

    Component {
        id: panelFactory

        StorageUsagePanel {
            parent: harness
            usageModel: usageModel
            theme: theme
        }
    }

    TestCase {
        id: testCase

        name: "StorageUsageView"
        when: windowShown

        property var panel

        function init() {
            usageModel.resetFixture();
            panel = createTemporaryObject(panelFactory, harness);
            verify(panel !== null);
        }

        function cleanup() {
            if (panel !== null) {
                panel.close();
                panel.destroy();
                panel = null;
            }
        }

        function openPanel() {
            panel.openFor("/synthetic/usage");
            tryVerify(function () {
                return panel.opened;
            });
            wait(30);
        }

        function control(name) {
            const found = findChild(panel.contentItem, name);
            verify(found !== null, "missing " + name);
            return found;
        }

        function test_mapAndAccessibleListShareDataByDefault() {
            openPanel();
            compare(usageModel.startCalls, 1);
            compare(usageModel.startedPath, "/synthetic/usage");

            const mapSurface = control("storageUsageMapSurface");
            const listSurface = control("storageUsageListSurface");
            verify(mapSurface.visible);
            verify(listSurface.visible);
            compare(mapSurface.Accessible.role, Accessible.List);
            compare(listSurface.Accessible.role, Accessible.List);
            verify(mapSurface.Accessible.name.length > 0);
            verify(listSurface.Accessible.name.length > 0);

            const mapEntry = control("storageMapSlice-0");
            const listEntry = control("storageListRow-0");
            compare(mapEntry.name, listEntry.name);
            compare(mapEntry.allocatedBytes, listEntry.allocatedBytes);
            compare(mapEntry.apparentBytes, listEntry.apparentBytes);
            compare(listEntry.Accessible.role, Accessible.ListItem);
            verify(listEntry.Accessible.name.indexOf("Archive") >= 0);
            verify(listEntry.Accessible.name.indexOf("folder") >= 0);
            compare(listEntry.Accessible.selectable, true);
            compare(listEntry.Accessible.selected, true);

            verify(control("storageUsageSummary").text.indexOf("24") >= 0);
            verify(control("storageUsageAccounting").text.indexOf("2 repeated") >= 0);
        }

        function test_mapSelectionAndActivationHavePointerAndKeyboardPaths() {
            openPanel();
            const secondSlice = control("storageMapSlice-1");
            mouseClick(secondSlice);
            compare(usageModel.currentIndex, 1);
            compare(usageModel.selectCalls, 1);

            const mapView = control("storageUsageMap");
            mapView.forceActiveFocus();
            keyClick(Qt.Key_Left);
            compare(usageModel.currentIndex, 0);
            keyClick(Qt.Key_Return);
            compare(usageModel.activatedRow, 0);

            usageModel.activateCalls = 0;
            mouseDoubleClickSequence(secondSlice, secondSlice.width / 2, secondSlice.height / 2, Qt.LeftButton, Qt.NoModifier);
            compare(usageModel.activateCalls, 1);
            compare(usageModel.activatedRow, 1);
        }

        function test_listSelectionAndActivationHavePointerAndKeyboardPaths() {
            openPanel();
            const secondRow = control("storageListRow-1");
            mouseClick(secondRow);
            compare(usageModel.currentIndex, 1);

            const listView = control("storageUsageList");
            listView.forceActiveFocus();
            keyClick(Qt.Key_Up);
            compare(usageModel.currentIndex, 0);
            keyClick(Qt.Key_Enter);
            compare(usageModel.activatedRow, 0);

            usageModel.activateCalls = 0;
            mouseDoubleClickSequence(secondRow, secondRow.width / 2, secondRow.height / 2, Qt.LeftButton, Qt.NoModifier);
            compare(usageModel.activatedRow, 1);
        }

        function test_upCancelAndCloseHavePointerAndKeyboardPaths() {
            openPanel();
            mouseClick(control("storageUsageUpButton"));
            compare(usageModel.upCalls, 1);
            keyClick(Qt.Key_Up, Qt.AltModifier);
            compare(usageModel.upCalls, 2);

            mouseClick(control("storageUsageCancelButton"));
            compare(usageModel.cancelCalls, 1);
            verify(usageModel.cancelled);

            usageModel.busy = true;
            keyClick(Qt.Key_Escape);
            compare(usageModel.cancelCalls, 2);
            verify(panel.opened);

            usageModel.busy = false;
            keyClick(Qt.Key_Escape);
            tryVerify(function () {
                return !panel.opened;
            });

            panel.openFor("/synthetic/usage");
            tryVerify(function () {
                return panel.opened;
            });
            mouseClick(control("storageUsageCloseButton"));
            tryVerify(function () {
                return !panel.opened;
            });
            compare(usageModel.cancelCalls, 3, "closing a busy view must cancel its scan");
        }
    }
}

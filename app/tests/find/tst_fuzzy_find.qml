pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea

Item {
    id: harness

    width: 800
    height: 600

    property alias overlay: overlay

    ListModel {
        id: fakeFinder

        property string query: ""
        property bool busy: false
        property bool ranking: false
        property string errorString: ""
        property int currentIndex: -1
        property int candidatesIndexed: 3
        property int entriesVisited: 3
        property int startCalls: 0
        property int cancelCalls: 0
        signal resultActivated(string path, bool isDirectory)

        function start(path, showHidden) {
            startCalls += 1;
        }

        function cancel() {
            cancelCalls += 1;
        }

        function moveCursor(delta) {
            if (count === 0) {
                return;
            }
            currentIndex = Math.max(0, Math.min(count - 1, currentIndex + delta));
        }

        function selectRow(row) {
            currentIndex = row;
        }

        function activate(row) {
            if (row >= 0 && row < count) {
                const result = get(row);
                resultActivated(result.entryPath, result.isDirectory);
            }
        }

        function activateCurrent() {
            activate(currentIndex);
        }
    }

    QtObject {
        id: fakeShellModel

        property string lastPath: ""
        property bool lastDirectory: false
        property int navigateCalls: 0

        function navigateToEntry(path, isDirectory) {
            lastPath = path;
            lastDirectory = isDirectory;
            navigateCalls += 1;
        }
    }

    ShellTheme {
        id: theme
    }

    FuzzyFindOverlay {
        id: overlay

        parent: harness
        finderModel: fakeFinder
        shellModel: fakeShellModel
        theme: theme
    }

    TestCase {
        name: "FuzzyFindOverlay"
        when: windowShown

        function init() {
            if (overlay.opened) {
                overlay.close();
                wait(0);
            }
            fakeFinder.clear();
            fakeFinder.append({
                "name": "docs",
                "entryPath": "/synthetic/docs",
                "relativePath": "docs",
                "isDirectory": true,
                "selected": false
            });
            fakeFinder.append({
                "name": "guide.txt",
                "entryPath": "/synthetic/docs/guide.txt",
                "relativePath": "docs/guide.txt",
                "isDirectory": false,
                "selected": true
            });
            fakeFinder.currentIndex = 1;
            fakeFinder.startCalls = 0;
            fakeFinder.cancelCalls = 0;
            fakeShellModel.navigateCalls = 0;
            fakeShellModel.lastPath = "";
            fakeShellModel.lastDirectory = false;
        }

        function openOverlay() {
            overlay.openFor("/synthetic", false);
            tryCompare(overlay, "opened", true);
            const field = findChild(overlay, "fuzzyFindField");
            verify(field !== null);
            tryVerify(() => field.activeFocus);
            return field;
        }

        function test_openTypingAndEscapeSharePaletteFocusSemantics() {
            const focusBefore = harness;
            focusBefore.forceActiveFocus();
            openOverlay();
            compare(fakeFinder.startCalls, 1);
            keyClick(Qt.Key_G);
            keyClick(Qt.Key_U);
            keyClick(Qt.Key_I);
            keyClick(Qt.Key_D);
            keyClick(Qt.Key_E);
            compare(fakeFinder.query, "guide");
            keyClick(Qt.Key_Escape);
            tryCompare(overlay, "opened", false);
            compare(fakeFinder.cancelCalls, 1);
            tryVerify(() => focusBefore.activeFocus);
        }

        function test_keyboardActivationNavigatesToFileMatch() {
            openOverlay();
            fakeFinder.currentIndex = 0;
            keyClick(Qt.Key_Down);
            compare(fakeFinder.currentIndex, 1);
            keyClick(Qt.Key_Return);
            tryCompare(overlay, "opened", false);
            compare(fakeShellModel.navigateCalls, 1);
            compare(fakeShellModel.lastPath, "/synthetic/docs/guide.txt");
            compare(fakeShellModel.lastDirectory, false);
        }

        function test_pointerActivationNavigatesToDirectoryMatch() {
            openOverlay();
            const results = findChild(overlay, "fuzzyFindResults");
            verify(results !== null);
            tryCompare(results, "count", 2);
            results.forceLayout();
            const row = results.itemAtIndex(0);
            verify(row !== null);
            mouseClick(row, row.width / 2, row.height / 2, Qt.LeftButton);
            tryCompare(overlay, "opened", false);
            compare(fakeShellModel.navigateCalls, 1);
            compare(fakeShellModel.lastPath, "/synthetic/docs");
            compare(fakeShellModel.lastDirectory, true);
        }

        function test_resultsExposeListAndListItemAccessibility() {
            openOverlay();
            const results = findChild(overlay, "fuzzyFindResults");
            verify(results !== null);
            tryCompare(results, "count", 2);
            results.forceLayout();
            const row = results.itemAtIndex(1);
            verify(row !== null);
            compare(results.Accessible.role, Accessible.List);
            compare(row.Accessible.role, Accessible.ListItem);
            compare(row.Accessible.name, "guide.txt");
            compare(row.Accessible.description, "docs/guide.txt");
            verify(row.Accessible.selected);
        }
    }
}

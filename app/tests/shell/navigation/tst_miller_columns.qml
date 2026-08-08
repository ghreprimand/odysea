pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea

Item {
    id: harness

    width: 760
    height: 260

    readonly property string sourceRoot: {
        const url = Qt.resolvedUrl("../../../qml").toString();
        return url.startsWith("file://") ? decodeURIComponent(url.slice(7)) : "";
    }

    ShellTheme {
        id: theme
    }

    MillerColumnsModel {
        id: columnsModel
    }

    MillerColumnsView {
        id: view

        anchors.fill: parent
        columnsModel: columnsModel
        theme: theme
    }

    TestCase {
        name: "MillerColumnsNavigation"
        when: windowShown

        function waitForColumn(column) {
            tryVerify(function () {
                return columnsModel.columnModel(column) !== null;
            });
            tryVerify(function () {
                return !columnsModel.columnBusy(column);
            });
        }

        function rowForName(column, name) {
            for (let row = 0; row < columnsModel.entryCount(column); ++row) {
                if (columnsModel.entryName(column, row) === name) {
                    return row;
                }
            }
            return -1;
        }

        function showRow(column, row) {
            columnsModel.setActiveColumn(column);
            columnsModel.moveToRow(row);
            view.revealCurrent();
            wait(0);
            let item = null;
            tryVerify(function () {
                item = findChild(view, "millerRow-" + column + "-" + row);
                return item !== null;
            });
            return item;
        }

        function init() {
            verify(harness.sourceRoot.length > 0);
            columnsModel.rootPath = "";
            columnsModel.rootPath = harness.sourceRoot;
            waitForColumn(0);
            view.forceActiveFocus();
            wait(0);
        }

        function test_keyboardMovesWithinAcrossActivatesAndCollapses() {
            keyClick(Qt.Key_Home);
            compare(columnsModel.columnCurrentIndex(0), 0);
            keyClick(Qt.Key_Down);
            compare(columnsModel.columnCurrentIndex(0), 1);

            const shaders = rowForName(0, "shaders");
            verify(shaders >= 0);
            columnsModel.moveToRow(shaders);
            compare(columnsModel.columnCount, 2);
            keyClick(Qt.Key_Right);
            compare(columnsModel.activeColumn, 1);
            waitForColumn(1);
            keyClick(Qt.Key_Left);
            compare(columnsModel.activeColumn, 0);
            keyClick(Qt.Key_Return);
            compare(columnsModel.activeColumn, 1);
            keyClick(Qt.Key_Backspace);
            compare(columnsModel.columnCount, 1);
            compare(columnsModel.activeColumn, 0);
        }

        function test_pointerSelectsOpensMovesAndCollapses() {
            const shaders = rowForName(0, "shaders");
            verify(shaders >= 0);
            const row = showRow(0, shaders);
            mouseClick(row);
            compare(columnsModel.columnCount, 2);
            compare(columnsModel.activeColumn, 0);

            let childHeader = null;
            tryVerify(function () {
                childHeader = findChild(view, "millerHeader-1");
                return childHeader !== null;
            });
            mouseClick(childHeader);
            compare(columnsModel.activeColumn, 1);

            const collapse = findChild(view, "millerCollapse-1");
            verify(collapse !== null);
            mouseClick(collapse);
            compare(columnsModel.columnCount, 1);

            const directory = showRow(0, shaders);
            mouseDoubleClickSequence(directory, directory.width / 2, directory.height / 2, Qt.LeftButton, Qt.NoModifier);
            compare(columnsModel.activeColumn, 1);
        }

        function test_accessibilityAndVerticalVirtualization() {
            const list = findChild(view, "millerEntryList-0");
            verify(list !== null);
            compare(list.Accessible.role, Accessible.List);
            verify(list.Accessible.name.indexOf(harness.sourceRoot) >= 0);

            const first = showRow(0, 0);
            compare(first.Accessible.role, Accessible.ListItem);
            verify(first.Accessible.name.indexOf(columnsModel.entryName(0, 0)) >= 0);
            verify(first.Accessible.name.indexOf(columnsModel.entryIsDirectory(0, 0) ? "Folder" : "File") >= 0);
            compare(first.Accessible.selected, true);
            compare(first.Accessible.focused, true);

            const finalRow = columnsModel.entryCount(0) - 1;
            verify(finalRow > 10);
            verify(findChild(view, "millerRow-0-" + finalRow) === null, "a viewport-sized column must not instantiate its final row");
        }
    }
}

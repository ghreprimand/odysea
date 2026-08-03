pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea
import "../../support" as Support

Support.ShellTestCase {
    id: testCase

    name: "ViewNavigationParity"

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

    function test_entryVectorIconsFollowSemanticKinds() {
        fakeModel.setProperty(0, "isDir", true);
        fakeModel.setProperty(1, "isSymlink", true);

        compare(child("entryIcon-0").name, "folder");
        compare(child("entryIcon-1").name, "symlink");
        compare(child("entryIcon-2").name, "file");

        mouseClick(child("gridViewButton"));
        tryCompare(child("directoryGrid"), "visible", true);
        compare(child("entryCellIcon-0").name, "folder");
        compare(child("entryCellIcon-1").name, "symlink");
        compare(child("entryCellIcon-2").name, "file");
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
        const currentCell = cellAt(2);
        compare(menu.anchorItem, currentCell);
        compare(menu.parent, currentCell);
        compare(menu.anchorPosition.x, currentCell.width / 2);
        compare(menu.anchorPosition.y, currentCell.height / 2);
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
}

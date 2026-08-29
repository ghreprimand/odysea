pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea
import "../../support" as Support

Support.ShellTestCase {
    id: testCase

    name: "InputParity"

    SignalSpy {
        id: dragStartedSpy

        signalName: "transferDragStarted"
    }

    function test_blankAreaRubberBand() {
        verifyBandDrag(child("rubberBandBlankArea"), false);
    }

    function test_commandPaletteKeyboardAndMouseParity() {
        const palette = child("commandPalette");
        verify(!palette.opened);

        // Keyboard path: the declared sequence opens it and moves focus
        // to the filter field; Escape dismisses and hands focus back to
        // where the palette was summoned from.
        const list = child("directoryList");
        list.forceActiveFocus();
        tryVerify(function () {
            return list.activeFocus;
        });
        keySequence("Ctrl+Shift+P");
        tryVerify(function () {
            return palette.opened;
        });
        const field = findChild(palette.contentItem, "paletteFilterField");
        verify(field !== null);
        tryVerify(function () {
            return field.activeFocus;
        });
        keyClick(Qt.Key_Escape);
        tryVerify(function () {
            return !palette.opened;
        });
        tryVerify(function () {
            return list.activeFocus;
        });

        // Mouse path: the toolbar button opens it, a press outside
        // dismisses it, and focus returns to the summoning surface —
        // the button itself, not merely anywhere in the window.
        const paletteButton = child("paletteButton");
        mouseClick(paletteButton);
        tryVerify(function () {
            return palette.opened;
        });
        mouseClick(shellWindow.contentItem, 5, shellWindow.height - 5);
        tryVerify(function () {
            return !palette.opened;
        });
        tryVerify(function () {
            return paletteButton.activeFocus;
        });
    }

    function test_dualPaneToggleMovedToFunctionKey() {
        compare(shellWindow.paneCount, 1);
        keySequence("F3");
        tryCompare(shellWindow, "paneCount", 2);
        verify(child("secondPane").visible);

        // The pointer path is unchanged: the toolbar toggle still routes
        // through the same declaration.
        mouseClick(child("paneToggleButton"));
        tryCompare(shellWindow, "paneCount", 1);
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

    function test_undoKeyboardAndPointerParity() {
        fakeModel.canUndo = true;
        fakeModel.undoDisabledReason = "";
        const list = child("directoryList");
        list.forceActiveFocus();

        keySequence("Ctrl+Z");
        tryCompare(fakeModel, "performUndoCalls", 1);

        mouseClick(child("undoButton"));
        tryCompare(fakeModel, "performUndoCalls", 2);
    }

    function test_pathOrientationAndDirectEntryKeyboardAndPointerParity() {
        const navigator = child("pathNavigator");
        const editor = child("pathEditorRow");
        verify(!navigator.editing);
        verify(!editor.visible);

        keySequence("Ctrl+L");
        tryVerify(function () {
            return navigator.editing;
        });
        const field = child("pathEntryField");
        tryCompare(field, "selectedText", fakeModel.path);
        navigator.draftText = "/sample/sa";
        field.forceActiveFocus();
        keyClick(Qt.Key_Tab);
        compare(navigator.draftText, "/sample/sample/");
        keyClick(Qt.Key_Return);
        tryCompare(fakeModel, "navigateFromInputCalls", 1);
        compare(fakeModel.navigationInput, "/sample/sample/");
        verify(!navigator.editing);

        mouseClick(child("editLocationButton"));
        verify(navigator.editing);
        navigator.draftText = "/sample/sa";
        mouseClick(child("pathCompletionButton"));
        compare(navigator.draftText, "/sample/sample/");
        mouseClick(child("commitPathButton"));
        tryCompare(fakeModel, "navigateFromInputCalls", 2);
        verify(!navigator.editing);

        mouseClick(child("editLocationButton"));
        navigator.draftText = "/sample/retained";
        keyClick(Qt.Key_Escape);
        verify(!navigator.editing);
        verify(navigator.retainedDraft);
        compare(navigator.draftText, "/sample/retained");
        mouseClick(child("editLocationButton"));
        compare(navigator.draftText, "/sample/retained");
        mouseClick(child("hidePathEditorButton"));
        verify(!navigator.editing);
    }

    function test_placesPanelKeyboardAndPointerSummon() {
        const navigator = child("pathNavigator");
        keySequence("Ctrl+Shift+L");
        const popup = child("locationsPopup");
        tryCompare(popup, "opened", true);
        keyClick(Qt.Key_Escape);
        tryCompare(popup, "opened", false);
        wait(50);

        mouseClick(child("locationsButton"));
        tryCompare(popup, "opened", true);
        popup.close();
        tryCompare(popup, "opened", false);
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
        let menu = child("paneActionMenu");
        tryCompare(menu, "opened", true);
        const currentRow = rowAt(1);
        compare(menu.anchorItem, currentRow);
        compare(menu.parent, currentRow);
        compare(menu.anchorPosition.x, 28);
        compare(menu.anchorPosition.y, currentRow.height);
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
        mousePress(row, row.width / 2, row.height / 2, Qt.LeftButton, Qt.ShiftModifier);
        compare(row.dragProposedAction, Qt.MoveAction);
        mouseRelease(row, row.width / 2, row.height / 2, Qt.LeftButton, Qt.ShiftModifier);

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

    function test_horizontalPointerGestureStartsAndEndsARealDrag() {
        fakeModel.selectRow(0, Qt.NoModifier);
        const row = rowAt(0);
        const centerX = row.width / 2;
        const centerY = row.height / 2;
        fakeModel.resetTelemetry();
        dragStartedSpy.target = row;
        dragStartedSpy.clear();

        mousePress(row, centerX, centerY, Qt.LeftButton, Qt.ShiftModifier);
        mouseMove(row, centerX + 20, centerY, 10, Qt.LeftButton, Qt.ShiftModifier);
        compare(dragStartedSpy.count, 1);
        compare(dragStartedSpy.signalArguments[0][0], Qt.MoveAction);
        compare(row.dragProposedAction, Qt.MoveAction);
        mouseRelease(row, centerX + 20, centerY, Qt.LeftButton, Qt.ShiftModifier);
        tryCompare(row, "dragActive", false);
        compare(fakeModel.dropSelectionCalls, 0);
        compare(fakeModel.beginRubberBandCalls, 0);
        dragStartedSpy.target = null;
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

    function test_gridPointerActivationAndSelection() {
        mouseClick(child("gridViewButton"));
        let grid = child("directoryGrid");
        tryCompare(grid, "visible", true);
        verify(grid.count > 0);
        compare(cellAt(0).entryContextMenu, cellAt(1).entryContextMenu);

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

    function test_rightClickSelectsAndOpensOnPress() {
        const row = rowAt(1);
        const menu = row.entryContextMenu;

        mousePress(row, row.width / 2, row.height / 2, Qt.RightButton, Qt.NoModifier);
        tryCompare(fakeModel, "selectRowCalls", 1);
        compare(fakeModel.selectedRow, 1);
        tryCompare(menu, "opened", true);
        compare(menu.anchorItem, row);
        compare(menu.parent, row);
        mouseRelease(row, row.width / 2, row.height / 2, Qt.RightButton, Qt.NoModifier);
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

    function menuItemByName(menu, objectName) {
        // The menu builds its items from the registry when it opens; their
        // geometry settles on the following frame, so a click synthesized
        // in the opening frame would map against pre-layout positions.
        waitForRendering(menu.contentItem);
        for (let i = 0; i < menu.count; ++i) {
            const item = menu.itemAt(i);
            if (item !== null && item.objectName === objectName) {
                return item;
            }
        }
        return null;
    }

    function test_contextOperationActionsRemainReachable() {
        let entryMenu = rowAt(0).entryContextMenu;
        verify(entryMenu !== null);
        compare(entryMenu, rowAt(1).entryContextMenu);

        clickRow(0, Qt.RightButton, Qt.NoModifier);
        tryCompare(entryMenu, "opened", true);
        mouseClick(menuItemByName(entryMenu, "menuAction-selection.copy"));
        tryCompare(fakeModel, "requestCopyCalls", 1);
        mouseClick(child("transferCancelButton"));

        clickRow(0, Qt.RightButton, Qt.NoModifier);
        tryCompare(entryMenu, "opened", true);
        mouseClick(menuItemByName(entryMenu, "menuAction-selection.move"));
        tryCompare(fakeModel, "requestMoveCalls", 1);
        mouseClick(child("transferCancelButton"));

        clickRow(0, Qt.RightButton, Qt.NoModifier);
        tryCompare(entryMenu, "opened", true);
        mouseClick(menuItemByName(entryMenu, "menuAction-selection.rename"));
        tryCompare(fakeModel, "requestRenameCalls", 1);
        mouseClick(child("renameCancelButton"));

        clickRow(0, Qt.RightButton, Qt.NoModifier);
        tryCompare(entryMenu, "opened", true);
        // The destructive action renders last, after the separator, and
        // its label states the target count.
        const trashItem = menuItemByName(entryMenu, "menuAction-selection.trash");
        compare(entryMenu.itemAt(entryMenu.count - 1), trashItem);
        compare(trashItem.text, "Move 1 entry to Trash");
        mouseClick(trashItem);
        tryCompare(fakeModel, "requestTrashCalls", 1);
        mouseClick(child("trashCancelButton"));
    }

    function test_operationErrorFeedback() {
        fakeModel.operationErrorString = "Synthetic operation failure";
        let errorDialog = child("operationErrorDialog");
        tryCompare(errorDialog, "opened", true);
        errorDialog.close();
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

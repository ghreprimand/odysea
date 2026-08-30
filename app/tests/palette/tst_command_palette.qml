// The command palette, driven standalone against recording stand-ins.
// The invariants that make the palette a registry surface rather than a
// second command list live here: enumeration comes from the registry, a
// declaration added at runtime is reachable with no palette-side change,
// disabled actions stay listed with their declared reason, the shortcut
// column reads the declaration, keyboard navigation skips disabled rows,
// activation routes through trigger-time revalidation, and focus returns
// to its origin on every dismissal path.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtTest
import OdySea

Item {
    id: harness

    width: 800
    height: 600

    ShellTheme {
        id: theme
    }

    component RecordingModel: QtObject {
        property var calls: []
        property int selectedCount: 0
        property bool operationBusy: false
        property bool operationInterruptible: false
        property bool operationPaused: false
        property bool operationProgressKnown: false
        property real operationProgress: 0
        property string operationEntry: ""
        property string operationEstimate: ""
        function pauseOperation() {
        }
        function resumeOperation() {
        }
        function cancelOperation() {
        }
        property bool canUndo: false
        property string undoDisabledReason: "No filesystem operation is available to undo."
        property bool showHidden: false
        property bool canGoBack: false
        property bool canGoForward: false
        property bool canGoUp: true
        property int sortMode: 0
        property int tabCount: 1
        property int activeTab: 0
        property int paneCount: 1
        property int activePane: 0
        property string path: "/synthetic/fixture"

        function record(name) {
            const seen = calls;
            seen.push(name);
            calls = seen;
        }

        function goBack() {
            record("goBack");
        }
        function goForward() {
            record("goForward");
        }
        function goUp() {
            record("goUp");
        }
        function refresh() {
            record("refresh");
        }
        function performUndo() {
            record("performUndo");
        }
        function activate(row) {
            record("activate:" + row);
        }
        function navigateToPath(path) {
            record("navigateToPath:" + path);
        }
        function addTab() {
            record("addTab");
        }
        function closeTab(index) {
            record("closeTab:" + index);
        }
        function activateTab(index) {
            record("activateTab:" + index);
        }
        function setDualPaneEnabled(enabled) {
            record("setDualPaneEnabled:" + enabled);
        }
        function activatePane(index) {
            record("activatePane:" + index);
        }
        function selectAll() {
            record("selectAll");
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
    }

    component RecordingShell: QtObject {
        property var calls: []
        property bool gridMode: false
        property bool columnsMode: false
        property int paneCount: 1
        property int activePaneIndex: 0
        property bool transferAllowed: false

        function record(name) {
            const seen = calls;
            seen.push(name);
            calls = seen;
        }

        function switchView(useGrid) {
            record("switchView:" + useGrid);
            columnsMode = false;
            gridMode = useGrid;
        }
        function switchColumnsView() {
            record("switchColumnsView");
            gridMode = false;
            columnsMode = true;
        }
        function activateTabIndex(index) {
            record("activateTabIndex:" + index);
        }
        function activateRelativeTab(offset) {
            record("activateRelativeTab:" + offset);
        }
        function focusAddressField() {
            record("focusAddressField");
        }
        function focusFilterField() {
            record("focusFilterField");
        }
        function openAppearancePanel() {
            record("openAppearancePanel");
        }
        function openCommandPalette() {
            record("openCommandPalette");
        }

        function openTreeSearch() {
            record("openTreeSearch");
        }
        function activatePane(index) {
            record("activatePane:" + index);
            activePaneIndex = index;
        }
        function setDualPaneEnabled(enabled) {
            record("setDualPaneEnabled:" + enabled);
            paneCount = enabled ? 2 : 1;
        }
        function switchPane() {
            record("switchPane");
            activePaneIndex = activePaneIndex === 0 ? 1 : 0;
        }
        function adjustSplitRatio(delta) {
            record("adjustSplitRatio:" + delta);
        }
        function canTransferToOppositePane(move) {
            return transferAllowed;
        }
        function transferToOppositePane(move) {
            record("transferToOppositePane:" + move);
            return transferAllowed;
        }
    }

    // The palette enumerates every declaration, including the path
    // declarations, so it needs the navigation surface they read. The
    // palette exercises never mutate it; the recorder is here so an
    // accidental mutation through a listed row is visible.
    component RecordingNavigationSettings: QtObject {
        property var calls: []
        property var places: [
            {
                "label": "Filesystem",
                "path": "/"
            }
        ]
        property var recentDestinations: ["/synthetic/recent"]

        function record(name) {
            const seen = calls;
            seen.push(name);
            calls = seen;
        }

        function addPlace(place) {
            record("addPlace:" + place.path);
            return true;
        }
        function removePlace(index) {
            record("removePlace:" + index);
        }
        function movePlace(from, to) {
            record("movePlace:" + from + ":" + to);
        }
        function clearRecentDestinations() {
            record("clearRecentDestinations");
        }
    }

    RecordingModel {
        id: fakeModel
    }

    RecordingShell {
        id: fakeShell
    }

    RecordingNavigationSettings {
        id: fakeNavigationSettings
    }

    ShellActions {
        id: shellActions

        shellModel: fakeModel
        shell: fakeShell
        navigationSettings: fakeNavigationSettings
    }

    Component {
        id: extraActionComponent

        ShellAction {
            actionId: "synthetic.extra"
            label: "Synthetic runtime addition"
            perform: () => fakeModel.record("syntheticExtra")
        }
    }

    CommandPalette {
        id: palette

        parent: harness
        registry: shellActions
        theme: theme
    }

    // A focusable origin the palette must hand focus back to.
    Item {
        id: focusOrigin

        width: 40
        height: 40
        activeFocusOnTab: true
    }

    ShellToolBar {
        id: toolBar

        width: harness.width
        shellModel: fakeModel
        registry: shellActions
        theme: theme
    }

    TestCase {
        id: testCase

        name: "CommandPalette"
        when: windowShown

        property var addedAction: null

        function init() {
            fakeModel.calls = [];
            fakeModel.selectedCount = 0;
            fakeModel.operationBusy = false;
            fakeModel.canUndo = false;
            fakeModel.undoDisabledReason = "No filesystem operation is available to undo.";
            fakeModel.tabCount = 1;
            fakeModel.paneCount = 1;
            fakeShell.calls = [];
            fakeShell.gridMode = false;
            fakeShell.columnsMode = false;
            fakeShell.paneCount = 1;
            fakeShell.activePaneIndex = 0;
            fakeShell.transferAllowed = false;
            if (palette.opened) {
                palette.close();
                tryCompare(palette, "visible", false);
            }
        }

        function cleanup() {
            if (testCase.addedAction !== null) {
                // The runtime-added declaration must not leak into later
                // cases; the list property rebuild drops the reference.
                const kept = [];
                for (let i = 0; i < shellActions.actions.length; ++i) {
                    if (shellActions.actions[i].actionId !== "synthetic.extra") {
                        kept.push(shellActions.actions[i]);
                    }
                }
                shellActions.actions = kept;
                testCase.addedAction.destroy();
                testCase.addedAction = null;
            }
        }

        function filterField() {
            const field = findChild(palette.contentItem, "paletteFilterField");
            verify(field !== null);
            return field;
        }

        function rowByName(objectName) {
            waitForRendering(palette.contentItem);
            return findChild(palette.contentItem, objectName);
        }

        function openPalette() {
            palette.openFor(null);
            tryCompare(palette, "opened", true);
            waitForRendering(palette.contentItem);
        }

        function test_enumeratesEveryReachableDeclaration() {
            openPalette();
            const ids = palette.entries.map(entry => entry.actionId);
            for (let i = 0; i < shellActions.actions.length; ++i) {
                const action = shellActions.actions[i];
                const listed = ids.indexOf(action.actionId) !== -1;
                if (action.enabledFor === null || action.surfaces.indexOf("global") !== -1) {
                    verify(listed, action.actionId + " should be listed in the palette");
                } else {
                    // A target-dependent predicate over non-global
                    // surfaces can never be satisfied by the palette's
                    // global context; the row would be permanently dead.
                    verify(!listed, action.actionId + " is target-scoped and should be omitted");
                }
            }
            verify(ids.indexOf("palette.open") !== -1);
            verify(ids.indexOf("selection.trash") !== -1);
            palette.close();
            tryCompare(palette, "visible", false);
        }

        function test_everyListedRowIsEnabledOrStatesAReason() {
            // The palette's honesty invariant: no row may sit disabled
            // without telling the user why. Asserted over the whole
            // enumeration rather than any fixed row count, so a future
            // declaration that breaks the invariant fails here.
            openPalette();
            verify(palette.entries.length > 0);
            for (let i = 0; i < palette.entries.length; ++i) {
                const entry = palette.entries[i];
                verify(entry.enabled || entry.reason.length > 0, entry.actionId + " is listed disabled without a stated reason");
            }
            palette.close();
            tryCompare(palette, "visible", false);
        }

        function test_targetScopedDeclarationsReachableFromTheirOwnContext() {
            // The omission is per-context, not a blocklist: the same
            // registry lists a target-scoped declaration when the
            // supplied context carries its target.
            const globalIds = shellActions.paletteEntries("", null).map(entry => entry.actionId);
            verify(globalIds.indexOf("entry.open") === -1);
            verify(globalIds.indexOf("place.remove") === -1);
            verify(globalIds.indexOf("tab.activate") === -1);
            verify(globalIds.indexOf("pane.activate") === -1);

            const entryIds = shellActions.paletteEntries("", shellActions.entryContext(0, false, 1)).map(entry => entry.actionId);
            verify(entryIds.indexOf("entry.open") !== -1);

            const placeIds = shellActions.paletteEntries("", shellActions.placeContext("/", "Filesystem")).map(entry => entry.actionId);
            verify(placeIds.indexOf("place.remove") !== -1);
        }

        function test_widenedGlobalContextInvokesLocationAndTabActions() {
            // The palette opens over a global context carrying the current
            // location and the first-tab ordinal, so the two global-surface
            // declarations whose predicates read those fields are not just
            // listed but enabled and invocable — through the one trigger path
            // every row uses. A palette that lists a command it can never run
            // is the defect this pins against.

            fakeNavigationSettings.calls = [];

            // Without the fields, both are honestly listed-disabled with a
            // reason: the widening, not the listing, is what makes them run.
            const bare = shellActions.paletteEntries("", shellActions.globalContext(undefined, undefined));
            const addBare = bare.filter(entry => entry.actionId === "place.addCurrent");
            const tabBare = bare.filter(entry => entry.actionId === "tab.activateByOrdinal");
            compare(addBare.length, 1);
            compare(addBare[0].enabled, false);
            verify(addBare[0].reason.length > 0);
            compare(tabBare.length, 1);
            compare(tabBare[0].enabled, false);
            verify(tabBare[0].reason.length > 0);

            // Widened context: the current location is not already a Place,
            // and at least one tab exists, so both resolve enabled.
            const context = shellActions.globalContext(0, "/synthetic/fixture");
            palette.openFor(context);
            tryCompare(palette, "opened", true);
            waitForRendering(palette.contentItem);

            filterField().text = "Add current location";
            const addRow = rowByName("paletteEntry-place.addCurrent");
            verify(addRow !== null);
            compare(addRow.actionEnabled, true);
            mouseClick(addRow);
            compare(fakeNavigationSettings.calls.join(","), "addPlace:/synthetic/fixture");
            tryCompare(palette, "visible", false);

            palette.openFor(context);
            tryCompare(palette, "opened", true);
            waitForRendering(palette.contentItem);

            filterField().text = "Switch to tab";
            const tabRow = rowByName("paletteEntry-tab.activateByOrdinal");
            verify(tabRow !== null);
            compare(tabRow.actionEnabled, true);
            mouseClick(tabRow);
            compare(fakeShell.calls.join(","), "activateTabIndex:0");
            tryCompare(palette, "visible", false);
        }

        function test_runtimeDeclarationBecomesReachableWithoutPaletteChange() {
            testCase.addedAction = extraActionComponent.createObject(shellActions);
            shellActions.actions.push(testCase.addedAction);

            openPalette();
            filterField().text = "Synthetic runtime";
            tryVerify(function () {
                return palette.entries.length === 1;
            });
            const row = rowByName("paletteEntry-synthetic.extra");
            verify(row !== null);

            mouseClick(row);
            compare(fakeModel.calls.join(","), "syntheticExtra");
            tryCompare(palette, "visible", false);
        }

        function test_disabledActionsListedWithDeclaredReason() {
            compare(fakeModel.selectedCount, 0);
            openPalette();
            filterField().text = "Copy selection";
            const row = rowByName("paletteEntry-selection.copy");
            verify(row !== null);
            verify(row.visible);
            compare(row.actionEnabled, false);
            compare(row.reason, "Nothing is selected");
            const reasonText = rowByName("paletteReason-selection.copy");
            verify(reasonText !== null);
            verify(reasonText.visible);
            compare(reasonText.text, "Nothing is selected");
            palette.close();
            tryCompare(palette, "visible", false);
        }

        function test_undoRowUsesTheJournalReasonAndSharedHandler() {
            fakeModel.undoDisabledReason = "The copied tree exceeds the reversible-entry limit.";
            openPalette();
            filterField().text = "Undo";
            const row = rowByName("paletteEntry-edit.undo");
            verify(row !== null);
            compare(row.actionEnabled, false);
            compare(row.reason, "The copied tree exceeds the reversible-entry limit.");

            fakeModel.canUndo = true;
            fakeModel.undoDisabledReason = "";
            tryCompare(row, "actionEnabled", true);
            mouseClick(row);
            compare(fakeModel.calls.join(","), "performUndo");
            tryCompare(palette, "visible", false);
        }

        function test_disabledRowReenablesLiveWhileOpen() {
            openPalette();
            filterField().text = "Copy selection";
            const row = rowByName("paletteEntry-selection.copy");
            verify(row !== null);
            compare(row.actionEnabled, false);
            fakeModel.selectedCount = 1;
            tryCompare(row, "actionEnabled", true);
            const reasonText = rowByName("paletteReason-selection.copy");
            tryCompare(reasonText, "visible", false);
            palette.close();
            tryCompare(palette, "visible", false);
        }

        function test_shortcutColumnReadsTheDeclaration() {
            openPalette();
            filterField().text = "Copy selection";
            const shortcut = rowByName("paletteShortcut-selection.copy");
            verify(shortcut !== null);
            const declared = shellActions.primarySequence(shellActions.find("selection.copy"));
            verify(declared.length > 0);
            compare(shortcut.text, declared);

            // A declaration without a shortcut renders an empty column
            // rather than an invented one.
            filterField().text = "Clear recent destinations";
            const bare = rowByName("paletteShortcut-recent.clear");
            verify(bare !== null);
            compare(bare.text, "");
            palette.close();
            tryCompare(palette, "visible", false);
        }

        function test_typingFiltersByLabelAndId() {
            openPalette();
            filterField().text = "trash";
            tryVerify(function () {
                return palette.entries.length === 1;
            });
            compare(palette.entries[0].actionId, "selection.trash");

            filterField().text = "nav.";
            const ids = palette.entries.map(entry => entry.actionId);
            verify(ids.indexOf("nav.back") !== -1);
            verify(ids.indexOf("selection.trash") === -1);
            palette.close();
            tryCompare(palette, "visible", false);
        }

        function test_keyboardNavigationSkipsDisabledRows() {
            openPalette();
            const list = findChild(palette.contentItem, "paletteList");
            verify(list !== null);
            filterField().text = "selection.";
            // Row positions are derived from the enumeration instead of
            // hardcoded counts, so the case survives future declarations.
            // With nothing selected, selection.all is the only enabled
            // row; rename sits directly above it and trash directly
            // below in declaration order.
            tryVerify(function () {
                return palette.entries.map(entry => entry.actionId).indexOf("selection.trash") !== -1;
            });
            const ids = palette.entries.map(entry => entry.actionId);
            const allIndex = ids.indexOf("selection.all");
            const renameIndex = ids.indexOf("selection.rename");
            const trashIndex = ids.indexOf("selection.trash");
            verify(allIndex !== -1);
            compare(renameIndex, allIndex - 1);
            compare(trashIndex, allIndex + 1);
            compare(list.currentIndex, allIndex);
            palette.moveHighlight(1);
            compare(list.currentIndex, allIndex);
            palette.moveHighlight(-1);
            compare(list.currentIndex, allIndex);

            fakeModel.selectedCount = 1;
            palette.moveHighlight(-1);
            compare(list.currentIndex, renameIndex);
            palette.moveHighlight(1);
            compare(list.currentIndex, allIndex);
            palette.moveHighlight(1);
            compare(list.currentIndex, trashIndex);
            palette.close();
            tryCompare(palette, "visible", false);
        }

        function test_returnTriggersHighlightedThroughRegistry() {
            focusOrigin.forceActiveFocus();
            tryVerify(function () {
                return focusOrigin.activeFocus;
            });
            openPalette();
            filterField().text = "Select all";
            tryVerify(function () {
                return palette.entries.length === 1;
            });
            keyClick(Qt.Key_Return);
            compare(fakeModel.calls.join(","), "selectAll");
            tryCompare(palette, "visible", false);
            tryVerify(function () {
                return focusOrigin.activeFocus;
            });
        }

        function test_disabledRowCannotFireByPointerOrKeyboard() {
            compare(fakeModel.selectedCount, 0);
            openPalette();
            filterField().text = "Move 0 entries to Trash";
            tryVerify(function () {
                return palette.entries.length === 1;
            });
            const row = rowByName("paletteEntry-selection.trash");
            verify(row !== null);
            compare(row.actionEnabled, false);

            // Pointer: the click routes through activate() and the
            // registry refuses the trigger.
            mouseClick(row);
            compare(fakeModel.calls.length, 0);
            compare(palette.opened, true);

            // Keyboard: no enabled row exists, so Return has no target.
            keyClick(Qt.Key_Return);
            compare(fakeModel.calls.length, 0);
            compare(palette.opened, true);
            palette.close();
            tryCompare(palette, "visible", false);
        }

        function test_escapeRestoresFocusToOrigin() {
            focusOrigin.forceActiveFocus();
            tryVerify(function () {
                return focusOrigin.activeFocus;
            });
            openPalette();
            verify(!focusOrigin.activeFocus);
            keyClick(Qt.Key_Escape);
            tryCompare(palette, "visible", false);
            tryVerify(function () {
                return focusOrigin.activeFocus;
            });
        }

        function test_pointerDismissRestoresFocusToOrigin() {
            focusOrigin.forceActiveFocus();
            tryVerify(function () {
                return focusOrigin.activeFocus;
            });
            openPalette();
            // The palette must have taken focus for restoration to mean
            // anything — the same post-open verify its Escape twin makes.
            verify(!focusOrigin.activeFocus);
            // A press outside the popup dismisses it.
            mouseClick(harness, 5, harness.height - 5);
            tryCompare(palette, "visible", false);
            tryVerify(function () {
                return focusOrigin.activeFocus;
            });
        }

        function test_toolBarButtonRoutesToTheShellEntryPoint() {
            const button = findChild(toolBar, "paletteButton");
            verify(button !== null);
            mouseClick(button);
            compare(fakeShell.calls.join(","), "openCommandPalette");
        }

        function test_paletteOwnsItsSequenceAndDualPaneMoved() {
            const entries = shellActions.shortcutEntries();
            let paletteOwner = "";
            let f3Owner = "";
            for (let i = 0; i < entries.length; ++i) {
                if (entries[i].sequence.toLowerCase() === "ctrl+shift+p") {
                    paletteOwner = entries[i].actionId;
                }
                if (entries[i].sequence.toLowerCase() === "f3") {
                    f3Owner = entries[i].actionId;
                }
            }
            compare(paletteOwner, "palette.open");
            compare(f3Owner, "pane.toggleDual");
            compare(shellActions.shortcutConflicts().length, 0);
        }
    }
}

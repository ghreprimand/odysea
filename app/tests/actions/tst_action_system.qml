// The shared action system, tested against recording stand-ins: registry
// mechanics, the concrete shell declarations, and the shared context
// menu. The invariants that keep surfaces honest live here — one
// declaration per action, enablement computed from context rather than
// from the asking surface, revalidation at trigger time, destructive
// separation with target counts, and shortcut declarations that cannot
// silently conflict.
pragma ComponentBehavior: Bound
import QtQuick
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
            gridMode = useGrid;
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
        function openLocations() {
            record("openLocations");
        }
        function focusFilterField() {
            record("focusFilterField");
        }
        function openAppearancePanel() {
            record("openAppearancePanel");
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

    component RecordingNavigationSettings: QtObject {
        property var calls: []
        property var places: [
            {
                "label": "Filesystem",
                "path": "/"
            },
            {
                "label": "Fixture",
                "path": "/synthetic"
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
            const next = places.slice();
            next.push(place);
            places = next;
            return true;
        }
        function removePlace(index) {
            record("removePlace:" + index);
            const next = places.slice();
            next.splice(index, 1);
            places = next;
            return true;
        }
        function movePlace(from, to) {
            record("movePlace:" + from + ":" + to);
            const next = places.slice();
            const moved = next.splice(from, 1)[0];
            next.splice(to, 0, moved);
            places = next;
            return true;
        }
        function clearRecentDestinations() {
            record("clearRecentDestinations");
            recentDestinations = [];
            return true;
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

    // A deliberately conflicting declaration set, proving the detector
    // itself rather than only the shipped declarations.
    ActionRegistry {
        id: conflictedRegistry

        ShellAction {
            actionId: "sample.first"
            label: "First"
            shortcuts: [
                {
                    "sequence": "Ctrl+K"
                }
            ]
        }
        ShellAction {
            actionId: "sample.second"
            label: "Second"
            shortcuts: [
                {
                    "sequence": "ctrl+k"
                }
            ]
        }
    }

    // Two alias spellings of one physical key. Qt normalizes both to the
    // same sequence and routes them to the same press, so the detector
    // must report a conflict even though the raw strings differ.
    ActionRegistry {
        id: aliasConflictedRegistry

        ShellAction {
            actionId: "sample.trashLike"
            label: "Trash-like"
            shortcuts: [
                {
                    "sequence": "Delete"
                }
            ]
        }
        ShellAction {
            actionId: "sample.deleteAlias"
            label: "Delete alias"
            shortcuts: [
                {
                    "sequence": "Del"
                }
            ]
        }
    }

    ActionMenu {
        id: menu

        registry: shellActions
        theme: theme
    }

    Item {
        id: anchorSurface

        x: 40
        y: 40
        width: 200
        height: 200
    }

    TestCase {
        id: testCase

        name: "ActionSystem"
        when: windowShown

        function init() {
            fakeModel.calls = [];
            fakeModel.selectedCount = 0;
            fakeModel.operationBusy = false;
            fakeModel.tabCount = 1;
            fakeModel.paneCount = 1;
            fakeShell.calls = [];
            fakeShell.gridMode = false;
            fakeShell.paneCount = 1;
            fakeShell.activePaneIndex = 0;
            fakeShell.transferAllowed = false;
            fakeNavigationSettings.calls = [];
            fakeNavigationSettings.places = [
                {
                    "label": "Filesystem",
                    "path": "/"
                },
                {
                    "label": "Fixture",
                    "path": "/synthetic"
                }
            ];
            fakeNavigationSettings.recentDestinations = ["/synthetic/recent"];
            if (menu.opened) {
                menu.close();
                tryCompare(menu, "visible", false);
            }
        }

        function menuItemByName(objectName) {
            // Item geometry settles the frame after the menu rebuilds, so
            // callers that click must not map against pre-layout positions.
            waitForRendering(menu.contentItem);
            for (let i = 0; i < menu.count; ++i) {
                const item = menu.itemAt(i);
                if (item !== null && item.objectName === objectName) {
                    return item;
                }
            }
            return null;
        }

        function test_actionIdsAreUnique() {
            const seen = ({});
            for (let i = 0; i < shellActions.actions.length; ++i) {
                const id = shellActions.actions[i].actionId;
                verify(seen[id] === undefined, "duplicate actionId " + id);
                seen[id] = true;
            }
        }

        function test_declaredShortcutsDoNotConflict() {
            compare(shellActions.shortcutConflicts().length, 0);
        }

        function test_conflictDetectorReportsPlantedDuplicate() {
            const conflicts = conflictedRegistry.shortcutConflicts();
            compare(conflicts.length, 1);
            compare(conflicts[0].toLowerCase(), "ctrl+k");
        }

        function test_conflictDetectorCatchesAliasSpellings() {
            // "Delete" and "Del" are different strings but one key to
            // Qt; a raw string comparison reports no conflict here.
            const conflicts = aliasConflictedRegistry.shortcutConflicts();
            compare(conflicts.length, 1);
            compare(aliasConflictedRegistry.normalizedSequence(conflicts[0]), aliasConflictedRegistry.normalizedSequence("Delete"));
        }

        function test_sequenceNormalizationSeesWhatQtSees() {
            compare(shellActions.normalizedSequence("Delete"), shellActions.normalizedSequence("Del"));
            compare(shellActions.normalizedSequence("Escape"), shellActions.normalizedSequence("Esc"));
            verify(shellActions.normalizedSequence("Delete") !== shellActions.normalizedSequence("Escape"));
            // An unparseable sequence keys on its own raw string instead
            // of colliding with other unparseable ones as "".
            compare(shellActions.normalizedSequence("NotAKeyName"), "notakeyname");
        }

        function test_enablementComesFromContextNotSurface() {
            const copy = shellActions.find("selection.copy");
            const entryContext = shellActions.entryContext(0, false, 1);
            const selectionContext = shellActions.selectionContext(1);
            compare(shellActions.isEnabled(copy, entryContext), false);
            compare(shellActions.isEnabled(copy, selectionContext), false);

            fakeModel.selectedCount = 2;
            compare(shellActions.isEnabled(copy, entryContext), true);
            compare(shellActions.isEnabled(copy, selectionContext), true);

            fakeModel.operationBusy = true;
            compare(shellActions.isEnabled(copy, entryContext), false);
            compare(shellActions.isEnabled(copy, selectionContext), false);
        }

        function test_triggerRevalidatesAtInvocation() {
            compare(shellActions.trigger("selection.copy", shellActions.selectionContext(0)), false);
            compare(fakeModel.calls.length, 0);

            fakeModel.selectedCount = 1;
            compare(shellActions.trigger("selection.copy", shellActions.selectionContext(1)), true);
            compare(fakeModel.calls.join(","), "requestCopy");
        }

        function test_entryOpenTracksTargetKindAndIndex() {
            const open = shellActions.find("entry.open");
            const folderContext = shellActions.entryContext(4, true, 1);
            const fileContext = shellActions.entryContext(2, false, 1);
            compare(shellActions.labelFor(open, folderContext), "Open folder");
            compare(shellActions.labelFor(open, fileContext), "Open");
            compare(shellActions.iconFor(open, folderContext), "folder");
            compare(shellActions.iconFor(open, fileContext), "open");

            compare(shellActions.isEnabled(open, shellActions.entryContext(-1, false, 0)), false);
            compare(shellActions.trigger("entry.open", shellActions.entryContext(-1, false, 0)), false);
            compare(shellActions.trigger("entry.open", folderContext), true);
            compare(fakeModel.calls.join(","), "activate:4");
        }

        function test_destructiveLabelStatesTargetCount() {
            const trash = shellActions.find("selection.trash");
            verify(trash.destructive);
            compare(shellActions.labelFor(trash, shellActions.entryContext(0, false, 1)), "Move 1 entry to Trash");
            compare(shellActions.labelFor(trash, shellActions.selectionContext(3)), "Move 3 entries to Trash");
        }

        function test_menuOrderKeepsDestructiveLastEvenWhenDisabled() {
            const listed = shellActions.actionsFor(shellActions.entryContext(0, false, 1));
            const ids = listed.map(action => action.actionId);
            compare(ids.join(","), "entry.open,selection.copy,selection.move,selection.rename,selection.trash");
            // Disabled actions stay listed: nothing is selected, yet the
            // selection operations remain present for the menu to render
            // disabled.
            compare(fakeModel.selectedCount, 0);
            verify(ids.indexOf("selection.copy") !== -1);
        }

        function test_canvasSurfaceListsWorkspaceActions() {
            const ids = shellActions.actionsFor(shellActions.canvasContext("/synthetic/fixture")).map(action => action.actionId);
            compare(ids.join(","), "selection.all,view.toggleHidden,nav.refresh,nav.up,tab.new");
        }

        function test_locationActionsServeNavigationSurfaces() {
            const breadcrumbIds = shellActions.actionsFor(shellActions.breadcrumbContext("/synthetic")).map(action => action.actionId);
            compare(breadcrumbIds.join(","), "location.open,location.openNewTab");
            const placeIds = shellActions.actionsFor(shellActions.placeContext("/synthetic", "Fixture")).map(action => action.actionId);
            compare(placeIds.join(","), "location.open,location.openNewTab,place.moveUp,place.moveDown,place.remove");
            const deviceIds = shellActions.actionsFor(shellActions.deviceContext("/synthetic", "Volume")).map(action => action.actionId);
            compare(deviceIds.join(","), "location.open,location.openNewTab");

            compare(shellActions.trigger("location.openNewTab", shellActions.breadcrumbContext("/synthetic/target")), true);
            compare(fakeModel.calls.join(","), "addTab,navigateToPath:/synthetic/target");
        }

        function test_navigationChromeActionsUseRegistryShortcutsWithoutConflicts() {
            const entries = shellActions.shortcutEntries();
            compare(entries.filter(entry => entry.actionId === "focus.address")[0].sequence, "Ctrl+L");
            compare(entries.filter(entry => entry.actionId === "focus.locations")[0].sequence, "Ctrl+Shift+L");
            compare(shellActions.trigger("focus.address", shellActions.globalContext(undefined)), true);
            compare(shellActions.trigger("focus.locations", shellActions.globalContext(undefined)), true);
            compare(fakeShell.calls.join(","), "focusAddressField,openLocations");
            compare(shellActions.shortcutConflicts().length, 0);
        }

        function test_paneActionsRouteThroughTheWorkspaceController() {
            fakeShell.paneCount = 2;
            fakeShell.transferAllowed = true;
            fakeModel.selectedCount = 1;

            compare(shellActions.trigger("pane.activate", shellActions.paneContext(1)), true);
            compare(shellActions.trigger("pane.switch", shellActions.globalContext(undefined)), true);
            compare(shellActions.trigger("pane.resizeLeft", shellActions.globalContext(undefined)), true);
            compare(shellActions.trigger("pane.resizeRight", shellActions.globalContext(undefined)), true);
            compare(shellActions.trigger("pane.copyToOther", shellActions.globalContext(undefined)), true);
            compare(shellActions.trigger("pane.moveToOther", shellActions.globalContext(undefined)), true);
            compare(fakeShell.calls.join(","), "activatePane:1,switchPane,adjustSplitRatio:-0.05,adjustSplitRatio:0.05,transferToOppositePane:false,transferToOppositePane:true");

            const entries = shellActions.shortcutEntries();
            compare(entries.filter(entry => entry.actionId === "pane.resizeLeft")[0].sequence, "Ctrl+Alt+Left");
            compare(entries.filter(entry => entry.actionId === "pane.resizeRight")[0].sequence, "Ctrl+Alt+Right");
            compare(entries.filter(entry => entry.actionId === "pane.copyToOther")[0].sequence, "Ctrl+Shift+C");
            compare(entries.filter(entry => entry.actionId === "pane.moveToOther")[0].sequence, "Ctrl+Shift+M");
            compare(shellActions.shortcutConflicts().length, 0);
        }

        function test_placeActionsResolveTheLiveTargetByPath() {
            const place = shellActions.placeContext("/synthetic", "Fixture");
            compare(shellActions.trigger("place.moveUp", place), true);
            compare(fakeNavigationSettings.calls.join(","), "movePlace:1:0");
            compare(fakeNavigationSettings.places[0].path, "/synthetic");
            compare(shellActions.trigger("place.moveDown", place), true);
            compare(fakeNavigationSettings.calls.join(","), "movePlace:1:0,movePlace:0:1");
            compare(shellActions.trigger("place.remove", place), true);
            compare(fakeNavigationSettings.calls.join(","), "movePlace:1:0,movePlace:0:1,removePlace:1");
        }

        function test_placeAddAndRecentClearRouteThroughRegistry() {
            const current = shellActions.breadcrumbContext("/synthetic/new-place");
            compare(shellActions.trigger("place.addCurrent", current), true);
            compare(fakeNavigationSettings.calls.join(","), "addPlace:/synthetic/new-place");
            compare(shellActions.trigger("recent.clear", shellActions.globalContext(undefined)), true);
            compare(fakeNavigationSettings.calls.join(","), "addPlace:/synthetic/new-place,clearRecentDestinations");
            compare(fakeNavigationSettings.recentDestinations.length, 0);
            compare(shellActions.trigger("recent.clear", shellActions.globalContext(undefined)), false);
        }

        function test_ordinalTabShortcutsCarryArguments() {
            const entries = shellActions.shortcutEntries().filter(entry => entry.actionId === "tab.activateByOrdinal");
            compare(entries.length, 9);
            compare(entries[0].sequence, "Ctrl+1");
            compare(entries[0].argument, 0);
            compare(entries[8].sequence, "Ctrl+9");
            compare(entries[8].argument, 8);

            fakeModel.tabCount = 3;
            const ordinal = shellActions.find("tab.activateByOrdinal");
            compare(shellActions.isEnabled(ordinal, shellActions.globalContext(2)), true);
            compare(shellActions.isEnabled(ordinal, shellActions.globalContext(5)), false);
            compare(shellActions.trigger("tab.activateByOrdinal", shellActions.globalContext(5)), false);
            compare(shellActions.trigger("tab.activateByOrdinal", shellActions.globalContext(1)), true);
            compare(fakeShell.calls.join(","), "activateTabIndex:1");
        }

        function test_paletteEnumeratesAndFilters() {
            // Enumeration covers exactly the declarations a global
            // context can reach: target-scoped predicates over
            // non-global surfaces are omitted rather than listed dead.
            const everything = shellActions.paletteEntries("", null);
            let reachable = 0;
            for (let i = 0; i < shellActions.actions.length; ++i) {
                const action = shellActions.actions[i];
                if (action.enabledFor === null || action.surfaces.indexOf("global") !== -1) {
                    ++reachable;
                }
            }
            compare(everything.length, reachable);
            verify(everything.length > 0);

            const filtered = shellActions.paletteEntries("trash", null);
            compare(filtered.length, 1);
            compare(filtered[0].actionId, "selection.trash");
            compare(filtered[0].sequence, "Delete");
            compare(filtered[0].enabled, false);

            fakeModel.selectedCount = 1;
            compare(shellActions.paletteEntries("trash", null)[0].enabled, true);

            const byId = shellActions.paletteEntries("nav.", null).map(entry => entry.actionId);
            verify(byId.indexOf("nav.back") !== -1);
        }

        function test_contextSnapshotsAreImmutable() {
            const context = shellActions.entryContext(3, true, 2);
            verify(Object.isFrozen(context));
            context.entryIndex = 9;
            compare(context.entryIndex, 3);
            verify(Object.isFrozen(shellActions.canvasContext("/synthetic")));
            verify(Object.isFrozen(shellActions.tabContext(1)));
        }

        function test_menuRendersFromRegistryAndSeparatesDestructive() {
            fakeModel.selectedCount = 2;
            menu.openFor(shellActions.entryContext(0, false, 2), anchorSurface, Qt.point(10, 10), null);
            tryCompare(menu, "opened", true);

            compare(menu.parent, anchorSurface);
            compare(menu.anchorItem, anchorSurface);
            compare(menu.anchorPosition, Qt.point(10, 10));

            verify(menuItemByName("menuAction-entry.open") !== null);
            verify(menuItemByName("menuAction-selection.copy") !== null);
            const trashItem = menuItemByName("menuAction-selection.trash");
            verify(trashItem !== null);
            compare(trashItem.text, "Move 2 entries to Trash");

            // The destructive group renders last, after a separator.
            const lastItem = menu.itemAt(menu.count - 1);
            compare(lastItem.objectName, "menuAction-selection.trash");
            let separatorSeen = false;
            for (let i = 0; i < menu.count; ++i) {
                if (menu.itemAt(i).objectName === "destructiveSeparator") {
                    separatorSeen = true;
                }
            }
            verify(separatorSeen);
            menu.close();
            tryCompare(menu, "visible", false);
        }

        function test_menuShowsDisabledActionsAsDisabledNotAbsent() {
            compare(fakeModel.selectedCount, 0);
            menu.openFor(shellActions.entryContext(1, false, 1), anchorSurface, Qt.point(0, 0), null);
            tryCompare(menu, "opened", true);

            const copyItem = menuItemByName("menuAction-selection.copy");
            verify(copyItem !== null);
            verify(copyItem.visible);
            compare(copyItem.enabled, false);
            compare(copyItem.disabledReason, "Nothing is selected");

            // Live model changes re-enable the visible item while open.
            fakeModel.selectedCount = 1;
            tryCompare(copyItem, "enabled", true);
            menu.close();
            tryCompare(menu, "visible", false);
        }

        function test_menuTriggersThroughRegistryAndRestoresFocus() {
            fakeModel.selectedCount = 1;
            anchorSurface.focus = true;
            menu.openFor(shellActions.entryContext(0, true, 1), anchorSurface, Qt.point(0, 0), anchorSurface);
            tryCompare(menu, "opened", true);

            const openItem = menuItemByName("menuAction-entry.open");
            verify(openItem !== null);
            compare(openItem.text, "Open folder");
            mouseClick(openItem);
            tryCompare(menu, "visible", false);
            compare(fakeModel.calls.join(","), "activate:0");
            tryVerify(function () {
                return anchorSurface.activeFocus;
            });
        }

        function test_menuRebuildsForEachContextKind() {
            menu.openFor(shellActions.canvasContext("/synthetic"), anchorSurface, Qt.point(0, 0), null);
            tryCompare(menu, "opened", true);
            verify(menuItemByName("menuAction-selection.all") !== null);
            verify(menuItemByName("menuAction-entry.open") === null);
            verify(menuItemByName("menuAction-selection.trash") === null);
            menu.close();
            tryCompare(menu, "visible", false);

            menu.openFor(shellActions.tabContext(0), anchorSurface, Qt.point(0, 0), null);
            tryCompare(menu, "opened", true);
            verify(menuItemByName("menuAction-tab.activate") !== null);
            verify(menuItemByName("menuAction-tab.close") !== null);
            const closeItem = menuItemByName("menuAction-tab.close");
            compare(closeItem.enabled, false);
            fakeModel.tabCount = 2;
            tryCompare(closeItem, "enabled", true);
            menu.close();
            tryCompare(menu, "visible", false);
        }
    }
}

// The shell's action declarations: the single site where every
// user-facing action states its label, icon, enablement, shortcuts, and
// handler. Menus, toolbar buttons, the tab strip, pane surfaces, key
// sequences, and the command palette all render from these declarations
// through the ActionRegistry API — no surface restates enablement or
// wording, which is what keeps them from drifting apart.
//
// `shellModel` is the live directory/workspace model; `shell` is the
// window-level controller carrying view mode, focus entry points, and
// the appearance surface. Both arrive by injection so scenes and tests
// drive the same declarations against stand-ins.
import QtQml

ActionRegistry {
    id: actionSet

    required property var shellModel
    required property var shell
    required property var navigationSettings

    function placeIndex(path) {
        const places = actionSet.navigationSettings.places;
        for (let index = 0; index < places.length; ++index) {
            if (places[index].path === path) {
                return index;
            }
        }
        return -1;
    }

    ShellAction {
        actionId: "entry.open"
        labelFor: context => context.isDirectory ? qsTr("Open folder") : qsTr("Open")
        iconFor: context => context.isDirectory ? "folder" : "open"
        surfaces: ["entry"]
        enabledFor: context => context.entryIndex !== undefined && context.entryIndex >= 0
        perform: context => actionSet.shellModel.activate(context.entryIndex)
    }
    ShellAction {
        actionId: "location.open"
        label: qsTr("Open this location")
        iconName: "folder"
        surfaces: ["breadcrumb", "place", "device"]
        enabledFor: context => typeof context.path === "string" && context.path.length > 0
        perform: context => actionSet.shellModel.navigateToPath(context.path)
    }
    ShellAction {
        actionId: "location.openNewTab"
        label: qsTr("Open in new tab")
        iconName: "add"
        surfaces: ["breadcrumb", "place", "device"]
        enabledFor: context => typeof context.path === "string" && context.path.length > 0
        perform: context => {
            actionSet.shellModel.addTab();
            actionSet.shellModel.navigateToPath(context.path);
        }
    }
    ShellAction {
        actionId: "place.addCurrent"
        label: qsTr("Add current location to Places")
        shortLabel: qsTr("Add current location")
        iconName: "add"
        enabledFor: context => typeof context.path === "string" && context.path.length > 0 && actionSet.placeIndex(context.path) < 0
        perform: context => actionSet.navigationSettings.addPlace({
                "label": "",
                "path": context.path
            })
    }
    ShellAction {
        actionId: "place.moveUp"
        label: qsTr("Move Place up")
        shortLabel: qsTr("Up")
        iconName: "up"
        surfaces: ["place"]
        enabledFor: context => actionSet.placeIndex(context.path) > 0
        perform: context => {
            const index = actionSet.placeIndex(context.path);
            actionSet.navigationSettings.movePlace(index, index - 1);
        }
    }
    ShellAction {
        actionId: "place.moveDown"
        label: qsTr("Move Place down")
        shortLabel: qsTr("Down")
        surfaces: ["place"]
        enabledFor: context => {
            const index = actionSet.placeIndex(context.path);
            return index >= 0 && index + 1 < actionSet.navigationSettings.places.length;
        }
        perform: context => {
            const index = actionSet.placeIndex(context.path);
            actionSet.navigationSettings.movePlace(index, index + 1);
        }
    }
    ShellAction {
        actionId: "place.remove"
        labelFor: context => qsTr("Remove %1 from Places").arg(context.label)
        shortLabel: qsTr("Remove")
        iconName: "close"
        surfaces: ["place"]
        enabledFor: context => actionSet.placeIndex(context.path) >= 0
        perform: context => actionSet.navigationSettings.removePlace(actionSet.placeIndex(context.path))
    }
    ShellAction {
        actionId: "recent.clear"
        label: qsTr("Clear recent destinations")
        shortLabel: label
        enabled: actionSet.navigationSettings.recentDestinations.length > 0
        perform: () => actionSet.navigationSettings.clearRecentDestinations()
    }
    ShellAction {
        actionId: "selection.copy"
        label: qsTr("Copy selection")
        shortLabel: qsTr("Copy")
        iconName: "copy"
        surfaces: ["entry", "selection"]
        enabled: actionSet.shellModel.selectedCount > 0 && !actionSet.shellModel.operationBusy
        disabledReasonFor: () => actionSet.shellModel.operationBusy ? qsTr("An operation is already running") : qsTr("Nothing is selected")
        shortcuts: [
            {
                "sequence": "Ctrl+C"
            }
        ]
        perform: () => actionSet.shellModel.requestCopy()
    }
    ShellAction {
        actionId: "selection.move"
        label: qsTr("Move selection")
        shortLabel: qsTr("Move")
        iconName: "move"
        surfaces: ["entry", "selection"]
        enabled: actionSet.shellModel.selectedCount > 0 && !actionSet.shellModel.operationBusy
        disabledReasonFor: () => actionSet.shellModel.operationBusy ? qsTr("An operation is already running") : qsTr("Nothing is selected")
        shortcuts: [
            {
                "sequence": "Ctrl+X"
            }
        ]
        perform: () => actionSet.shellModel.requestMove()
    }
    ShellAction {
        actionId: "selection.rename"
        label: qsTr("Rename")
        iconName: "rename"
        surfaces: ["entry", "selection"]
        enabled: actionSet.shellModel.selectedCount === 1 && !actionSet.shellModel.operationBusy
        disabledReasonFor: () => actionSet.shellModel.selectedCount > 1 ? qsTr("Renaming takes exactly one entry") : qsTr("Nothing is selected")
        shortcuts: [
            {
                "sequence": "F2"
            }
        ]
        perform: () => actionSet.shellModel.requestRename()
    }
    ShellAction {
        actionId: "selection.all"
        label: qsTr("Select all")
        iconName: "select-all"
        surfaces: ["canvas"]
        shortcuts: [
            {
                "sequence": "Ctrl+A"
            }
        ]
        perform: () => actionSet.shellModel.selectAll()
    }
    ShellAction {
        actionId: "view.toggleHidden"
        label: qsTr("Show hidden files")
        shortLabel: qsTr("Hidden")
        surfaces: ["canvas"]
        checkable: true
        checked: actionSet.shellModel.showHidden
        shortcuts: [
            {
                "sequence": "Ctrl+H"
            }
        ]
        perform: () => actionSet.shellModel.showHidden = !actionSet.shellModel.showHidden
    }
    ShellAction {
        actionId: "nav.refresh"
        label: qsTr("Refresh")
        iconName: "refresh"
        surfaces: ["canvas"]
        shortcuts: [
            {
                "sequence": "F5"
            }
        ]
        perform: () => actionSet.shellModel.refresh()
    }
    ShellAction {
        actionId: "nav.up"
        label: qsTr("Up one level")
        shortLabel: qsTr("Up")
        iconName: "up"
        surfaces: ["canvas"]
        enabled: actionSet.shellModel.canGoUp
        shortcuts: [
            {
                "sequence": "Alt+Up"
            }
        ]
        perform: () => actionSet.shellModel.goUp()
    }
    ShellAction {
        actionId: "nav.back"
        label: qsTr("Back")
        iconName: "back"
        enabled: actionSet.shellModel.canGoBack
        shortcuts: [
            {
                "sequence": "Alt+Left"
            }
        ]
        perform: () => actionSet.shellModel.goBack()
    }
    ShellAction {
        actionId: "nav.forward"
        label: qsTr("Forward")
        iconName: "forward"
        enabled: actionSet.shellModel.canGoForward
        shortcuts: [
            {
                "sequence": "Alt+Right"
            }
        ]
        perform: () => actionSet.shellModel.goForward()
    }
    ShellAction {
        actionId: "view.list"
        label: qsTr("List view")
        shortLabel: qsTr("List")
        iconName: "list"
        checkable: true
        checked: !actionSet.shell.gridMode
        shortcuts: [
            {
                "sequence": "Ctrl+Shift+1"
            }
        ]
        perform: () => actionSet.shell.switchView(false)
    }
    ShellAction {
        actionId: "view.grid"
        label: qsTr("Grid view")
        shortLabel: qsTr("Grid")
        iconName: "grid"
        checkable: true
        checked: actionSet.shell.gridMode
        shortcuts: [
            {
                "sequence": "Ctrl+Shift+2"
            }
        ]
        perform: () => actionSet.shell.switchView(true)
    }
    ShellAction {
        actionId: "view.cycleSort"
        label: qsTr("Cycle sort order")
        shortcuts: [
            {
                "sequence": "Ctrl+Shift+S"
            }
        ]
        perform: () => actionSet.shellModel.sortMode = (actionSet.shellModel.sortMode + 1) % 3
    }
    ShellAction {
        actionId: "focus.address"
        label: qsTr("Go to location")
        shortcuts: [
            {
                "sequence": "Ctrl+L"
            }
        ]
        perform: () => actionSet.shell.focusAddressField()
    }
    ShellAction {
        actionId: "focus.locations"
        label: qsTr("Places and recent destinations")
        shortLabel: qsTr("Places")
        iconName: "folder"
        shortcuts: [
            {
                "sequence": "Ctrl+Shift+L"
            }
        ]
        perform: () => actionSet.shell.openLocations()
    }
    ShellAction {
        actionId: "focus.filter"
        label: qsTr("Filter this folder")
        shortcuts: [
            {
                "sequence": "Ctrl+F"
            }
        ]
        perform: () => actionSet.shell.focusFilterField()
    }
    ShellAction {
        actionId: "appearance.open"
        label: qsTr("Appearance settings")
        shortLabel: qsTr("Appearance")
        iconName: "appearance"
        shortcuts: [
            {
                "sequence": "Ctrl+,"
            }
        ]
        perform: () => actionSet.shell.openAppearancePanel()
    }
    ShellAction {
        actionId: "tab.activate"
        label: qsTr("Switch to this tab")
        surfaces: ["tab"]
        enabledFor: context => context.tabIndex !== undefined && context.tabIndex >= 0 && context.tabIndex < actionSet.shellModel.tabCount
        perform: context => actionSet.shell.activateTabIndex(context.tabIndex)
    }
    ShellAction {
        actionId: "tab.new"
        label: qsTr("New tab")
        iconName: "add"
        surfaces: ["tab", "canvas"]
        shortcuts: [
            {
                "sequence": "Ctrl+T"
            }
        ]
        perform: () => actionSet.shellModel.addTab()
    }
    ShellAction {
        actionId: "tab.close"
        label: qsTr("Close tab")
        iconName: "close"
        surfaces: ["tab"]
        enabled: actionSet.shellModel.tabCount > 1
        disabledReasonFor: () => qsTr("The last tab stays open")
        shortcuts: [
            {
                "sequence": "Ctrl+W"
            }
        ]
        perform: context => actionSet.shellModel.closeTab(context.tabIndex !== undefined ? context.tabIndex : actionSet.shellModel.activeTab)
    }
    ShellAction {
        actionId: "tab.next"
        label: qsTr("Next tab")
        shortcuts: [
            {
                "sequence": "Ctrl+Tab"
            }
        ]
        perform: () => actionSet.shell.activateRelativeTab(1)
    }
    ShellAction {
        actionId: "tab.previous"
        label: qsTr("Previous tab")
        shortcuts: [
            {
                "sequence": "Ctrl+Shift+Tab"
            }
        ]
        perform: () => actionSet.shell.activateRelativeTab(-1)
    }
    ShellAction {
        actionId: "tab.activateByOrdinal"
        label: qsTr("Switch to numbered tab")
        enabledFor: context => context.argument !== undefined && context.argument < actionSet.shellModel.tabCount
        shortcuts: [
            {
                "sequence": "Ctrl+1",
                "argument": 0
            },
            {
                "sequence": "Ctrl+2",
                "argument": 1
            },
            {
                "sequence": "Ctrl+3",
                "argument": 2
            },
            {
                "sequence": "Ctrl+4",
                "argument": 3
            },
            {
                "sequence": "Ctrl+5",
                "argument": 4
            },
            {
                "sequence": "Ctrl+6",
                "argument": 5
            },
            {
                "sequence": "Ctrl+7",
                "argument": 6
            },
            {
                "sequence": "Ctrl+8",
                "argument": 7
            },
            {
                "sequence": "Ctrl+9",
                "argument": 8
            }
        ]
        perform: context => actionSet.shell.activateTabIndex(context.argument)
    }
    ShellAction {
        actionId: "pane.activate"
        label: qsTr("Focus this pane")
        surfaces: ["pane"]
        enabledFor: context => context.paneIndex !== undefined && context.paneIndex >= 0 && context.paneIndex < actionSet.shell.paneCount
        perform: context => actionSet.shell.activatePane(context.paneIndex)
    }
    ShellAction {
        actionId: "pane.toggleDual"
        label: actionSet.shell.paneCount === 2 ? qsTr("Merge to a single pane") : qsTr("Split into dual panes")
        shortLabel: actionSet.shell.paneCount === 2 ? qsTr("1 pane") : qsTr("2 panes")
        iconName: "panes"
        surfaces: ["pane"]
        shortcuts: [
            {
                "sequence": "F3"
            }
        ]
        perform: () => actionSet.shell.setDualPaneEnabled(actionSet.shell.paneCount === 1)
    }
    ShellAction {
        actionId: "pane.switch"
        label: qsTr("Switch pane")
        enabled: actionSet.shell.paneCount === 2
        shortcuts: [
            {
                "sequence": "F6"
            }
        ]
        perform: () => actionSet.shell.switchPane()
    }
    ShellAction {
        actionId: "pane.resizeLeft"
        label: qsTr("Move pane divider left")
        enabled: actionSet.shell.paneCount === 2
        shortcuts: [
            {
                "sequence": "Ctrl+Alt+Left"
            }
        ]
        perform: () => actionSet.shell.adjustSplitRatio(-0.05)
    }
    ShellAction {
        actionId: "pane.resizeRight"
        label: qsTr("Move pane divider right")
        enabled: actionSet.shell.paneCount === 2
        shortcuts: [
            {
                "sequence": "Ctrl+Alt+Right"
            }
        ]
        perform: () => actionSet.shell.adjustSplitRatio(0.05)
    }
    ShellAction {
        actionId: "pane.copyToOther"
        label: qsTr("Copy selection to other pane")
        shortLabel: qsTr("Copy across")
        iconName: "copy"
        surfaces: ["pane"]
        enabled: actionSet.shell.canTransferToOppositePane(false)
        disabledReasonFor: () => actionSet.shellModel.selectedCount === 0 ? qsTr("Nothing is selected") : qsTr("The selection cannot be copied to the other pane")
        shortcuts: [
            {
                "sequence": "Ctrl+Shift+C"
            }
        ]
        perform: () => actionSet.shell.transferToOppositePane(false)
    }
    ShellAction {
        actionId: "pane.moveToOther"
        label: qsTr("Move selection to other pane")
        shortLabel: qsTr("Move across")
        iconName: "move"
        surfaces: ["pane"]
        enabled: actionSet.shell.canTransferToOppositePane(true)
        disabledReasonFor: () => actionSet.shellModel.selectedCount === 0 ? qsTr("Nothing is selected") : qsTr("The selection cannot be moved to the other pane")
        shortcuts: [
            {
                "sequence": "Ctrl+Shift+M"
            }
        ]
        perform: () => actionSet.shell.transferToOppositePane(true)
    }
    ShellAction {
        actionId: "selection.trash"
        labelFor: context => {
            const count = context !== null && context.targetCount !== undefined ? context.targetCount : actionSet.shellModel.selectedCount;
            return count === 1 ? qsTr("Move 1 entry to Trash") : qsTr("Move %1 entries to Trash").arg(count);
        }
        shortLabel: qsTr("Trash")
        iconName: "trash"
        destructive: true
        surfaces: ["entry", "selection"]
        enabled: actionSet.shellModel.selectedCount > 0 && !actionSet.shellModel.operationBusy
        disabledReasonFor: () => actionSet.shellModel.operationBusy ? qsTr("An operation is already running") : qsTr("Nothing is selected")
        shortcuts: [
            {
                "sequence": "Delete"
            }
        ]
        perform: () => actionSet.shellModel.requestTrash()
    }
    ShellAction {
        actionId: "palette.open"
        label: qsTr("Command palette")
        shortLabel: qsTr("Commands")
        iconName: "commands"
        shortcuts: [
            {
                "sequence": "Ctrl+Shift+P"
            }
        ]
        perform: () => actionSet.shell.openCommandPalette()
    }
}

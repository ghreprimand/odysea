// Tests that the shell's reusable chrome components work standalone: each
// one is instantiated here against a recording model stand-in, outside
// Main.qml, and driven through pointer and keyboard paths. A component
// that silently depends on the shell scene fails here even while the
// full-shell suites stay green.
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea

Item {
    id: harness

    width: 960
    height: 600

    ShellTheme {
        id: theme
    }

    // A recording stand-in for the shell model: every action a component
    // may invoke appends its name here, and every property a component
    // binds is a plain notifyable value.
    component RecordingModel: QtObject {
        property var calls: []
        property bool canGoBack: false
        property bool canGoForward: false
        property bool canGoUp: false
        property string path: "/synthetic/fixture"
        property int paneCount: 1
        property int activePane: 0
        property int activeTab: 0
        property int tabCount: 3
        property string filterText: ""
        property int sortMode: 0
        property bool showHidden: false
        property int selectedCount: 0
        property bool busy: false
        property bool operationBusy: false
        property string operationErrorString: ""
        property string errorString: ""
        property string statusMessage: "ready"
        property bool navigateAccepted: true
        property string completionText: "/synthetic/projects/"
        property string completionSuffix: "ojects/"

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
        function activate(row) {
            record("activate:" + row);
        }
        function refresh() {
            record("refresh");
        }
        function setDualPaneEnabled(enabled) {
            record("setDualPaneEnabled:" + enabled);
        }
        function tabLabel(index) {
            return "tab " + index;
        }
        function activateTab(index) {
            record("activateTab:" + index);
        }
        function addTab() {
            record("addTab");
        }
        function closeTab(index) {
            record("closeTab:" + index);
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
        function breadcrumbSegments() {
            const root = {
                "label": "/",
                "path": "/",
                "url": "file:///"
            };
            if (path === "/") {
                return [root];
            }
            return [root,
                {
                    "label": "synthetic",
                    "path": "/synthetic",
                    "url": "file:///synthetic"
                },
                {
                    "label": "fixture",
                    "path": path,
                    "url": "file://" + path
                }
            ];
        }
        function navigateToPath(destination) {
            record("navigateToPath:" + destination);
            path = destination;
        }
        function navigateFromInput(input) {
            record("navigateFromInput:" + input);
            if (navigateAccepted) {
                path = input;
            }
            return navigateAccepted;
        }
        function navigationCompletion(input) {
            return {
                "completed": completionText,
                "suffix": completionSuffix,
                "candidates": ["projects"]
            };
        }
        function dropSelection(destination, move, conflictMode) {
            record("dropSelection:" + destination + ":" + move + ":" + conflictMode);
            return true;
        }
    }

    component RecordingSettings: QtObject {
        property var calls: []
        property var places: [
            {
                "label": "Filesystem",
                "path": "/"
            },
            {
                "label": "Projects",
                "path": "/synthetic/projects"
            }
        ]
        property var recentDestinations: ["/synthetic/recent-a", "/synthetic/recent-b"]

        function record(name) {
            const seen = calls;
            seen.push(name);
            calls = seen;
        }
        function addPlace(place) {
            const label = place.label;
            const path = place.path;
            record("addPlace:" + label + ":" + path);
            const next = places.slice();
            next.push({
                "label": label.length > 0 ? label : "Current",
                "path": path
            });
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
            if (from === to) {
                return false;
            }
            const next = places.slice();
            const moved = next.splice(from, 1)[0];
            next.splice(to, 0, moved);
            places = next;
            return true;
        }
        function recordRecentDestination(path) {
            record("recordRecentDestination:" + path);
            return true;
        }
        function clearRecentDestinations() {
            record("clearRecentDestinations");
            recentDestinations = [];
            return true;
        }
    }

    component RecordingController: QtObject {
        property var calls: []
        property bool gridMode: false
        property bool columnsMode: false
        property var pathNavigator: null
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
            if (pathNavigator !== null) {
                pathNavigator.beginEditing();
            }
        }
        function openLocations() {
            record("openLocations");
            if (pathNavigator !== null) {
                pathNavigator.openLocations();
            }
        }
        function focusFilterField() {
            record("focusFilterField");
        }
        function openAppearancePanel() {
            record("openAppearancePanel");
        }
        function openTreeSearch() {
            record("openTreeSearch");
        }
        function clearTypeAhead() {
            const seen = calls;
            seen.push("clearTypeAhead");
            calls = seen;
        }
        function focusCurrentView() {
            const seen = calls;
            seen.push("focusCurrentView");
            calls = seen;
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

    Component {
        id: modelFactory

        RecordingModel {}
    }

    Component {
        id: controllerFactory

        RecordingController {}
    }

    // Each test builds the real declaration set against its recording
    // stand-ins, so the chrome under test renders from the same registry
    // the shell uses.
    Component {
        id: registryFactory

        ShellActions {}
    }

    Component {
        id: settingsFactory

        RecordingSettings {}
    }

    Component {
        id: buttonFactory

        ShellButton {
            theme: theme
            text: "probe"
        }
    }

    Component {
        id: fieldFactory

        ShellTextField {
            theme: theme
            width: 200
        }
    }

    Component {
        id: toolBarFactory

        ShellToolBar {
            width: harness.width
            theme: theme
        }
    }

    Component {
        id: tabStripFactory

        TabStrip {
            width: harness.width
            theme: theme
        }
    }

    Component {
        id: pathNavigatorFactory

        PathNavigator {
            width: harness.width
            height: 48
            theme: theme
        }
    }

    Component {
        id: actionBarFactory

        ActionBar {
            width: harness.width
            theme: theme
        }
    }

    Component {
        id: statusBarFactory

        StatusBar {
            width: harness.width
            theme: theme
        }
    }

    Component {
        id: placeholderFactory

        PanePlaceholder {
            width: 200
            height: 160
            theme: theme
        }
    }

    TestCase {
        id: testCase

        name: "ShellComponents"
        when: windowShown

        function flush() {
            wait(20);
        }

        function makeRegistry(model, controller, settings) {
            const navigationSettings = settings === undefined ? createTemporaryObject(settingsFactory, harness) : settings;
            return createTemporaryObject(registryFactory, harness, {
                "shellModel": model,
                "shell": controller,
                "navigationSettings": navigationSettings
            });
        }

        function test_buttonPointerAndKeyboardActivation() {
            const button = createTemporaryObject(buttonFactory, harness);
            verify(button !== null);
            let activations = 0;
            button.clicked.connect(function () {
                ++activations;
            });

            mouseClick(button);
            compare(activations, 1);

            button.forceActiveFocus();
            verify(button.activeFocus);
            keyClick(Qt.Key_Space);
            compare(activations, 2);
        }

        function test_buttonDisabledStateBlocksAndRestyles() {
            const button = createTemporaryObject(buttonFactory, harness, {
                "enabled": false
            });
            verify(button !== null);
            let activations = 0;
            button.clicked.connect(function () {
                ++activations;
            });
            mouseClick(button);
            compare(activations, 0);
        }

        function test_buttonFocusRingUsesAccentRole() {
            const button = createTemporaryObject(buttonFactory, harness);
            verify(button !== null);
            compare(button.background.border.color, theme.border);
            button.forceActiveFocus();
            compare(button.background.border.color, theme.accent);
        }

        function test_textFieldFocusUsesAccentRole() {
            const field = createTemporaryObject(fieldFactory, harness);
            verify(field !== null);
            compare(field.background.border.color, theme.border);
            field.forceActiveFocus();
            compare(field.background.border.color, theme.accent);
            compare(field.color, theme.text);
        }

        function test_toolBarNavigationActions() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const bar = createTemporaryObject(toolBarFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, controller)
            });
            verify(bar !== null);
            flush();

            const back = findChild(bar, "backButton");
            verify(back !== null);
            verify(!back.enabled, "back must gate on canGoBack");
            model.canGoBack = true;
            verify(back.enabled);
            mouseClick(back);
            compare(model.calls[model.calls.length - 1], "goBack");
        }

        function test_toolBarViewToggleDrivesController() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const bar = createTemporaryObject(toolBarFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, controller)
            });
            verify(bar !== null);
            flush();

            const gridButton = findChild(bar, "gridViewButton");
            const listButton = findChild(bar, "listViewButton");
            const columnsButton = findChild(bar, "columnsViewButton");
            verify(gridButton !== null && listButton !== null && columnsButton !== null);
            verify(listButton.checked);
            verify(!gridButton.checked);
            verify(!columnsButton.checked);

            mouseClick(gridButton);
            compare(controller.calls[controller.calls.length - 1], "switchView:true");
            verify(gridButton.checked);
            verify(!listButton.checked);

            mouseClick(columnsButton);
            compare(controller.calls[controller.calls.length - 1], "switchColumnsView");
            verify(columnsButton.checked);
            verify(!gridButton.checked);
            verify(!listButton.checked);
        }

        function test_pathNavigatorCalmEditorRetainsDraft() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const settings = createTemporaryObject(settingsFactory, harness);
            const registry = makeRegistry(model, controller, settings);
            const navigator = createTemporaryObject(pathNavigatorFactory, harness, {
                "shellModel": model,
                "navigationController": controller,
                "registry": registry,
                "settings": settings
            });
            verify(navigator !== null);
            controller.pathNavigator = navigator;
            flush();

            verify(findChild(navigator, "calmPathRow").visible);
            verify(!findChild(navigator, "pathEditorRow").visible);
            mouseClick(findChild(navigator, "editLocationButton"));
            verify(navigator.editing);
            const address = findChild(navigator, "pathEntryField");
            tryVerify(function () {
                return address.activeFocus;
            });
            navigator.draftText = "/synthetic/unfinished";
            mouseClick(findChild(navigator, "hidePathEditorButton"));
            verify(!navigator.editing);
            verify(navigator.retainedDraft);
            verify(findChild(navigator, "retainedPathIndicator").visible);

            mouseClick(findChild(navigator, "editLocationButton"));
            compare(address.text, "/synthetic/unfinished");
            keyClick(Qt.Key_Escape);
            verify(!navigator.editing);
            compare(navigator.draftText, "/synthetic/unfinished");
        }

        function test_pathNavigatorRegistryFocusRouteSelectsExistingPath() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const settings = createTemporaryObject(settingsFactory, harness);
            const registry = makeRegistry(model, controller, settings);
            const navigator = createTemporaryObject(pathNavigatorFactory, harness, {
                "shellModel": model,
                "navigationController": controller,
                "registry": registry,
                "settings": settings
            });
            verify(navigator !== null);
            controller.pathNavigator = navigator;

            verify(registry.trigger("focus.address", registry.globalContext(undefined)));
            const address = findChild(navigator, "pathEntryField");
            tryVerify(function () {
                return navigator.editing && address.activeFocus;
            });
            compare(address.selectedText, "/synthetic/fixture");
        }

        function test_pathNavigatorCompletionAndDirectEntryParity() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const settings = createTemporaryObject(settingsFactory, harness);
            const navigator = createTemporaryObject(pathNavigatorFactory, harness, {
                "shellModel": model,
                "navigationController": controller,
                "registry": makeRegistry(model, controller, settings),
                "settings": settings
            });
            verify(navigator !== null);
            controller.pathNavigator = navigator;
            flush();

            navigator.beginEditing();
            navigator.draftText = "/synthetic/pr";
            const address = findChild(navigator, "pathEntryField");
            address.forceActiveFocus();
            keyClick(Qt.Key_Tab);
            compare(navigator.draftText, "/synthetic/projects/");
            keyClick(Qt.Key_Return);
            compare(model.calls[model.calls.length - 1], "navigateFromInput:/synthetic/projects/");
            verify(!navigator.editing);

            mouseClick(findChild(navigator, "editLocationButton"));
            navigator.draftText = "/synthetic/pr";
            mouseClick(findChild(navigator, "pathCompletionButton"));
            compare(navigator.draftText, "/synthetic/projects/");
            mouseClick(findChild(navigator, "commitPathButton"));
            compare(model.calls[model.calls.length - 1], "navigateFromInput:/synthetic/projects/");

            model.navigateAccepted = false;
            navigator.beginEditing();
            navigator.draftText = "relative";
            mouseClick(findChild(navigator, "commitPathButton"));
            verify(navigator.editing, "a rejected path stays editable");
        }

        function test_pathNavigatorPlaceManagementPointerAndKeyboard() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const settings = createTemporaryObject(settingsFactory, harness);
            const navigator = createTemporaryObject(pathNavigatorFactory, harness, {
                "shellModel": model,
                "navigationController": controller,
                "registry": makeRegistry(model, controller, settings),
                "settings": settings
            });
            verify(navigator !== null);
            controller.pathNavigator = navigator;
            flush();

            mouseClick(findChild(navigator, "locationsButton"));
            const popup = findChild(navigator, "locationsPopup");
            tryCompare(popup, "opened", true);
            const popupContent = popup.contentItem;
            mouseClick(findChild(popupContent, "addCurrentPlaceButton"));
            compare(settings.calls[settings.calls.length - 1], "addPlace::/synthetic/fixture");

            const add = findChild(popupContent, "addCurrentPlaceButton");
            add.forceActiveFocus();
            keyClick(Qt.Key_Space);
            compare(settings.calls[settings.calls.length - 1], "addPlace::/synthetic/fixture");

            mouseClick(findChild(popupContent, "movePlaceDownButton-0"));
            verify(settings.calls[settings.calls.length - 1].startsWith("movePlace:"));
            let place = findChild(popupContent, "placeButton-1");
            place.forceActiveFocus();
            keyClick(Qt.Key_Up, Qt.AltModifier);
            verify(settings.calls[settings.calls.length - 1].startsWith("movePlace:"));

            mouseClick(findChild(popupContent, "removePlaceButton-1"));
            verify(settings.calls[settings.calls.length - 1].startsWith("removePlace:"));
            place = findChild(popupContent, "placeButton-0");
            place.forceActiveFocus();
            keyClick(Qt.Key_Delete);
            verify(settings.calls[settings.calls.length - 1].startsWith("removePlace:"));
        }

        function test_pathNavigatorFastJumpsAndRecentClearParity() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const settings = createTemporaryObject(settingsFactory, harness);
            const navigator = createTemporaryObject(pathNavigatorFactory, harness, {
                "shellModel": model,
                "navigationController": controller,
                "registry": makeRegistry(model, controller, settings),
                "settings": settings
            });
            verify(navigator !== null);
            controller.pathNavigator = navigator;
            flush();

            navigator.openLocations();
            const popup = findChild(navigator, "locationsPopup");
            const popupContent = popup.contentItem;
            mouseClick(findChild(popupContent, "placeButton-1"));
            compare(model.calls[model.calls.length - 1], "navigateToPath:/synthetic/projects");
            navigator.openLocations();
            const place = findChild(popupContent, "placeButton-0");
            place.forceActiveFocus();
            keyClick(Qt.Key_Space);
            compare(model.calls[model.calls.length - 1], "navigateToPath:/");

            navigator.openLocations();
            mouseClick(findChild(popupContent, "clearRecentsButton"));
            compare(settings.calls[settings.calls.length - 1], "clearRecentDestinations");
            settings.recentDestinations = ["/synthetic/recent-c"];
            const clear = findChild(popupContent, "clearRecentsButton");
            clear.forceActiveFocus();
            keyClick(Qt.Key_Space);
            compare(settings.calls[settings.calls.length - 1], "clearRecentDestinations");

            model.path = "/synthetic/fixture";
            flush();
            mouseClick(findChild(navigator, "breadcrumb-0"));
            compare(model.calls[model.calls.length - 1], "navigateToPath:/");
            model.path = "/synthetic/fixture";
            flush();
            const ancestor = findChild(navigator, "breadcrumb-1");
            ancestor.forceActiveFocus();
            keyClick(Qt.Key_Return);
            compare(model.calls[model.calls.length - 1], "navigateToPath:/synthetic");
        }

        function test_toolBarAppearanceRoutesThroughRegistry() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const bar = createTemporaryObject(toolBarFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, controller)
            });
            verify(bar !== null);
            flush();

            mouseClick(findChild(bar, "appearanceButton"));
            compare(controller.calls[controller.calls.length - 1], "openAppearancePanel");
        }

        function test_toolBarTreeSearchRoutesThroughRegistry() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const bar = createTemporaryObject(toolBarFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, controller)
            });
            verify(bar !== null);
            flush();

            mouseClick(findChild(bar, "treeSearchButton"));
            compare(controller.calls[controller.calls.length - 1], "openTreeSearch");
        }

        function test_tabStripActivationAndLifecycle() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const strip = createTemporaryObject(tabStripFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, controller)
            });
            verify(strip !== null);
            flush();

            const secondTab = findChild(strip, "tabButton-1");
            verify(secondTab !== null);
            compare(secondTab.text, "tab 1");
            mouseClick(secondTab);
            compare(controller.calls[controller.calls.length - 1], "activateTabIndex:1");

            const closeButton = findChild(strip, "closeTabButton");
            verify(closeButton !== null);
            verify(closeButton.enabled, "three tabs are open, closing is allowed");
            mouseClick(closeButton);
            compare(model.calls[model.calls.length - 1], "closeTab:0");
        }

        function test_actionBarGatesOnSelectionAndBusyState() {
            const model = createTemporaryObject(modelFactory, harness);
            const bar = createTemporaryObject(actionBarFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, createTemporaryObject(controllerFactory, harness))
            });
            verify(bar !== null);
            flush();

            const copyButton = findChild(bar, "copyButton");
            const renameButton = findChild(bar, "renameButton");
            verify(copyButton !== null && renameButton !== null);
            verify(!copyButton.enabled, "copy must gate on selection");
            verify(!renameButton.enabled);

            model.selectedCount = 2;
            verify(copyButton.enabled);
            verify(!renameButton.enabled, "rename requires exactly one entry");

            model.selectedCount = 1;
            verify(renameButton.enabled);

            model.operationBusy = true;
            verify(!copyButton.enabled, "a running operation gates the actions");

            model.operationBusy = false;
            mouseClick(copyButton);
            compare(model.calls[model.calls.length - 1], "requestCopy");
        }

        function test_actionBarFilterReachesModel() {
            const model = createTemporaryObject(modelFactory, harness);
            const bar = createTemporaryObject(actionBarFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, createTemporaryObject(controllerFactory, harness))
            });
            verify(bar !== null);
            flush();

            bar.focusFilterField();
            const filter = findChild(bar, "filterField");
            verify(filter.activeFocus);
            keyClick(Qt.Key_B);
            compare(model.filterText, "b");
        }

        function test_actionBarHiddenToggle() {
            const model = createTemporaryObject(modelFactory, harness);
            const bar = createTemporaryObject(actionBarFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, createTemporaryObject(controllerFactory, harness))
            });
            verify(bar !== null);
            flush();

            mouseClick(findChild(bar, "hiddenToggle"));
            verify(model.showHidden);
        }

        function test_statusBarErrorTakesDangerRole() {
            const model = createTemporaryObject(modelFactory, harness);
            const bar = createTemporaryObject(statusBarFactory, harness, {
                "shellModel": model
            });
            verify(bar !== null);
            flush();

            const message = findChild(bar, "statusMessageText");
            verify(message !== null);
            compare(message.text, "ready");
            compare(message.color, theme.textMuted);

            model.operationErrorString = "operation failed";
            compare(message.text, "operation failed");
            compare(message.color, theme.danger);
        }

        function test_placeholderClickActivatesItsPane() {
            const model = createTemporaryObject(modelFactory, harness, {
                "paneCount": 2
            });
            const controller = createTemporaryObject(controllerFactory, harness, {
                "paneCount": 2
            });
            const placeholder = createTemporaryObject(placeholderFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, controller),
                "paneIndex": 1
            });
            verify(placeholder !== null);
            flush();

            mouseClick(placeholder);
            compare(controller.calls[controller.calls.length - 1], "activatePane:1");
        }
    }
}

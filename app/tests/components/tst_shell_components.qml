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
        function navigateToPath(path) {
            record("navigateToPath:" + path);
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
    }

    component RecordingController: QtObject {
        property var calls: []
        property bool gridMode: false

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
        function focusFilterField() {
            record("focusFilterField");
        }
        function openAppearancePanel() {
            record("openAppearancePanel");
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

        function makeRegistry(model, controller) {
            return createTemporaryObject(registryFactory, harness, {
                "shellModel": model,
                "shell": controller
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
            verify(gridButton !== null && listButton !== null);
            verify(listButton.checked);
            verify(!gridButton.checked);

            mouseClick(gridButton);
            compare(controller.calls[controller.calls.length - 1], "switchView:true");
            verify(gridButton.checked);
            verify(!listButton.checked);
        }

        function test_toolBarAddressFieldRoundTrip() {
            const model = createTemporaryObject(modelFactory, harness);
            const controller = createTemporaryObject(controllerFactory, harness);
            const bar = createTemporaryObject(toolBarFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, controller)
            });
            verify(bar !== null);
            flush();

            const address = findChild(bar, "addressField");
            verify(address !== null);
            compare(address.text, "/synthetic/fixture");

            // The keyboard entry point selects everything, so typing
            // replaces the path and Return commits it to the model.
            bar.focusAddressField();
            verify(address.activeFocus);
            keyClick(Qt.Key_A);
            keyClick(Qt.Key_Return);
            compare(model.path, "a");

            // A model-side change lands in the unfocused field.
            model.path = "/other/place";
            compare(address.text, "/other/place");
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
            const placeholder = createTemporaryObject(placeholderFactory, harness, {
                "shellModel": model,
                "registry": makeRegistry(model, createTemporaryObject(controllerFactory, harness)),
                "paneIndex": 1
            });
            verify(placeholder !== null);
            flush();

            mouseClick(placeholder);
            compare(model.calls[model.calls.length - 1], "activatePane:1");
        }
    }
}

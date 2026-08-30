// OdySea shell foundation. Navigation, selection, filtering, tabs, panes, and
// filesystem-operation requests expose matching pointer and keyboard paths.
//
// The window composes the module's reusable chrome components — toolbar,
// breadcrumbs, tab strip, action row, directory panes, and status strip —
// and owns what is genuinely shell-wide: the theme instance, the action
// registry with its instantiated shortcut table, the type-ahead engine,
// view-mode state, and the presentation pipeline that processes everything
// beneath the popup overlay.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
// Self-import: resolves the module's compiled types (ShellTheme) for tooling
// and runtime alike.
import OdySea

ApplicationWindow {
    id: root

    required property var shellModel
    /// The application supplies a second independent adapter. The fallback
    /// keeps existing single-pane scenes source-compatible.
    property var secondaryShellModel: shellModel

    /// Where appearance preferences persist. The application injects the real
    /// location; the default keeps scenes and tests in memory only.
    property string themeStoragePath: ""
    readonly property ShellTheme shellTheme: ShellTheme {
        storagePath: root.themeStoragePath
    }

    /// The single action declaration site. Every menu, button, and key
    /// sequence in the shell renders from these declarations.
    readonly property ShellActions actions: ShellActions {
        shellModel: root.activeShellModel
        entryModel: root.activeEntryModel
        shell: root
        navigationSettings: root.shellTheme
    }

    objectName: "mainWindow"
    readonly property int rowHeight: shellTheme.rowHeight
    readonly property color backgroundColor: shellTheme.background
    readonly property color panelColor: shellTheme.panel
    readonly property color borderColor: shellTheme.border
    readonly property color primaryTextColor: shellTheme.text
    readonly property color secondaryTextColor: shellTheme.textMuted
    readonly property color accentColor: shellTheme.accent
    readonly property color selectionColor: shellTheme.selectionBed
    /// Set by the entry point from the scene's requested alpha setting. Tests
    /// can model both outcomes without relying on a host
    /// compositor's alpha support.
    property bool alphaBufferAvailable: false
    // main.cpp publishes this after the scene graph exposes its renderer API.
    // The safe initial state keeps the window opaque until that probe completes.
    property bool rendererSupportsWindowTransparency: false
    readonly property bool materialEffectsEnabled: shellTheme.profile !== ShellTheme.Off && !shellTheme.highContrast
    readonly property bool windowTransparencyAvailable: alphaBufferAvailable && rendererSupportsWindowTransparency
    readonly property bool windowTransparencyEnabled: materialEffectsEnabled && windowTransparencyAvailable
    readonly property real windowGroundOpacity: windowTransparencyEnabled ? shellTheme.glassOpacity : 1.0
    readonly property int typeAheadTimeoutMs: 900
    property bool gridMode: false
    property bool columnsMode: false
    property string typeAheadBuffer: ""
    property int activePaneIndex: 0
    readonly property bool dualPaneEnabled: shellTheme.dualPaneEnabled
    readonly property int paneCount: dualPaneEnabled ? 2 : 1
    readonly property var activeShellModel: activePaneIndex === 0 ? shellModel : secondaryShellModel
    readonly property var oppositeShellModel: activePaneIndex === 0 ? secondaryShellModel : shellModel
    readonly property var activeEntryModel: paneLayout.activeEntryModel !== null ? paneLayout.activeEntryModel : activeShellModel
    readonly property var oppositeEntryModel: paneLayout.oppositeEntryModel
    readonly property bool activeEntryModelReady: paneLayout.activeEntryModelReady
    readonly property bool oppositeEntryModelReady: paneLayout.oppositeEntryModelReady

    onDualPaneEnabledChanged: {
        if (!dualPaneEnabled && activePaneIndex !== 0) {
            activePaneIndex = 0;
            clearTypeAhead();
            focusCurrentView();
        }
    }

    width: 1100
    height: 720
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: root.activeShellModel.path.length > 0 ? root.activeShellModel.path + " — OdySea" : "OdySea"
    // Alpha on the clear color matters only after the entry point confirmed a
    // requested alpha setting. The opaque fallback avoids a black or unpainted
    // window on renderers and compositors that cannot preserve destination
    // alpha.
    color: windowTransparencyEnabled ? "transparent" : backgroundColor
    font.family: shellTheme.chromeFontFamily
    font.pixelSize: shellTheme.chromeFontPixelSize

    function formatSize(bytes) {
        if (bytes < 1024) {
            return bytes + " B";
        }
        if (bytes < 1024 * 1024) {
            return (bytes / 1024).toFixed(1) + " KiB";
        }
        if (bytes < 1024 * 1024 * 1024) {
            return (bytes / (1024 * 1024)).toFixed(1) + " MiB";
        }
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB";
    }

    function activateRelativeTab(offset) {
        const count = root.activeShellModel.tabCount;
        if (count < 1) {
            return;
        }
        const nextTab = (root.activeShellModel.activeTab + offset + count) % count;
        root.activeShellModel.activateTab(nextTab);
    }

    function activeDirectoryPane() {
        return paneLayout.paneItem(root.activePaneIndex);
    }

    function activatePane(paneIndex) {
        if (paneIndex < 0 || paneIndex >= root.paneCount) {
            return;
        }
        if (root.activePaneIndex === paneIndex) {
            return;
        }
        root.activePaneIndex = paneIndex;
        root.clearTypeAhead();
        root.focusCurrentView();
    }

    function setDualPaneEnabled(enabled) {
        root.shellTheme.dualPaneEnabled = enabled;
        if (!enabled && root.activePaneIndex !== 0) {
            root.activePaneIndex = 0;
        }
        root.clearTypeAhead();
        root.focusCurrentView();
    }

    function switchPane() {
        if (root.paneCount === 2) {
            root.activatePane(root.activePaneIndex === 0 ? 1 : 0);
        }
    }

    function adjustSplitRatio(delta) {
        if (root.paneCount !== 2) {
            return;
        }
        paneLayout.adjustSplitRatio(delta);
    }

    function canTransferToOppositePane(move) {
        return root.paneCount === 2 && root.activeEntryModelReady && root.oppositeEntryModelReady && root.oppositeEntryModel !== null && root.activeEntryModel !== root.oppositeEntryModel && root.activeEntryModel.selectedCount > 0 && !root.activeEntryModel.operationBusy && !root.oppositeEntryModel.operationBusy && root.oppositeEntryModel.path.length > 0 && root.activeEntryModel.path !== root.oppositeEntryModel.path;
    }

    function oppositeTransferPath() {
        return root.oppositeEntryModel !== null ? root.oppositeEntryModel.path : "";
    }

    function transferToOppositePane(move) {
        if (!root.canTransferToOppositePane(move)) {
            return false;
        }
        return root.activeEntryModel.dropSelection(root.oppositeTransferPath(), move, 0);
    }

    function clearTypeAhead() {
        typeAheadBuffer = "";
        typeAheadTimer.stop();
    }

    function focusCurrentView() {
        Qt.callLater(function () {
            const pane = root.activeDirectoryPane();
            if (pane !== null) {
                pane.focusCurrentView();
                pane.revealCurrent();
            }
        });
    }

    function printableKeyText(event) {
        if ((event.key === Qt.Key_Space && typeAheadBuffer.length === 0) || (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) !== 0 || event.text.length === 0) {
            return "";
        }
        const codePoint = event.text.codePointAt(0);
        return codePoint >= 0x20 && codePoint !== 0x7f ? event.text : "";
    }

    function handleTypeAhead(event, view) {
        if (event.key === Qt.Key_Backspace) {
            if (typeAheadBuffer.length === 0) {
                return false;
            }
            typeAheadBuffer = typeAheadBuffer.slice(0, -1);
            if (typeAheadBuffer.length > 0) {
                root.activeShellModel.selectByPrefix(typeAheadBuffer, false);
                typeAheadTimer.restart();
                view.revealCurrent();
            } else {
                typeAheadTimer.stop();
            }
            return true;
        }
        if (event.key === Qt.Key_Escape && typeAheadBuffer.length > 0) {
            root.clearTypeAhead();
            return true;
        }

        const text = root.printableKeyText(event);
        if (text.length === 0) {
            return false;
        }
        const previous = typeAheadBuffer;
        const repeatedSingleCharacter = previous.length === 1 && previous.toLocaleLowerCase() === text.toLocaleLowerCase();
        const cycle = previous.length === 0 || repeatedSingleCharacter;
        typeAheadBuffer = repeatedSingleCharacter ? text : previous + text;
        root.activeShellModel.selectByPrefix(typeAheadBuffer, cycle);
        typeAheadTimer.restart();
        view.revealCurrent();
        return true;
    }

    function switchView(useGrid) {
        columnsMode = false;
        gridMode = useGrid;
        root.clearTypeAhead();
        root.focusCurrentView();
    }

    function switchColumnsView() {
        gridMode = false;
        columnsMode = true;
        root.clearTypeAhead();
        root.focusCurrentView();
    }

    function activateTabIndex(index) {
        if (index < root.activeShellModel.tabCount) {
            root.activeShellModel.activateTab(index);
            root.clearTypeAhead();
            root.focusCurrentView();
        }
    }

    // Focus and surface entry points the registry's actions target. The
    // declarations call these instead of reaching into chrome internals.
    function focusAddressField() {
        pathNavigator.beginEditing();
    }

    function openLocations() {
        pathNavigator.openLocations();
    }

    function focusFilterField() {
        actionBar.focusFilterField();
    }

    function openAppearancePanel() {
        appearancePanel.open();
    }

    function openStorageUsage() {
        storageUsagePanel.openFor(root.activeShellModel.path);
    }

    function openCommandPalette() {
        // Open over the current location and the first-tab ordinal, so the
        // two global-surface declarations that read those fields — add the
        // current location to Places, switch to a numbered tab — are listed,
        // enabled, and invocable from the palette instead of silently absent.
        commandPalette.openFor(root.actions.globalContext(0, root.activeShellModel.path));
    }

    function openTreeSearch() {
        fuzzyFindOverlay.openFor(root.activeEntryModel.path, root.activeEntryModel.showHidden);
    }

    Timer {
        id: typeAheadTimer

        interval: root.typeAheadTimeoutMs
        onTriggered: root.typeAheadBuffer = ""
    }

    // The shortcut table is instantiated from the registry's declarations:
    // one Shortcut per declared sequence, firing through the registry so
    // enablement is revalidated at activation. The declarations themselves
    // live with the actions in ShellActions.
    Instantiator {
        objectName: "shortcutTable"
        model: root.actions.shortcutEntries()

        delegate: Shortcut {
            id: boundShortcut

            required property var modelData
            readonly property var action: root.actions.find(modelData.actionId)
            readonly property var actionContext: root.actions.globalContext(modelData.argument)

            sequence: modelData.sequence
            // isEnabled reads live model state inside the predicates, so
            // this binding re-evaluates with the model; triggering
            // revalidates once more before the handler runs.
            enabled: root.actions.isEnabled(action, actionContext)
            onActivated: root.actions.trigger(modelData.actionId, actionContext)
        }
    }

    AppearancePanel {
        id: appearancePanel

        parent: root.contentItem
        theme: root.shellTheme
    }

    StorageUsageModel {
        id: storageUsageModel
    }

    StorageUsagePanel {
        id: storageUsagePanel

        parent: root.contentItem
        usageModel: storageUsageModel
        theme: root.shellTheme
    }

    CommandPalette {
        id: commandPalette

        parent: root.contentItem
        registry: root.actions
        theme: root.shellTheme
    }

    FuzzyFindModel {
        id: fuzzyFindModel
    }

    FuzzyFindOverlay {
        id: fuzzyFindOverlay

        parent: root.contentItem
        finderModel: fuzzyFindModel
        shellModel: root.activeShellModel
        theme: root.shellTheme
    }

    // Everything the presentation pipeline processes lives under one item:
    // chrome, panes, and status. Popups render in the window overlay above
    // the pipeline instead, which keeps modal surfaces solid and color-true.
    Item {
        id: shellContent

        anchors.fill: parent

        // Window ground: the deep-field material behind every surface.
        DeepFieldGround {
            objectName: "windowGround"
            anchors.fill: parent
            deepField: root.shellTheme.effectiveDeepField
            sheetColor: root.backgroundColor
            deepColor: root.shellTheme.backgroundDeep
            fillOpacity: root.windowGroundOpacity
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            ShellToolBar {
                id: navigationToolBar

                Layout.fillWidth: true
                shellModel: root.activeShellModel
                registry: root.actions
                theme: root.shellTheme
            }

            PathNavigator {
                id: pathNavigator

                Layout.fillWidth: true
                shellModel: root.activeShellModel
                navigationController: root
                registry: root.actions
                settings: root.shellTheme
                theme: root.shellTheme
            }

            TabStrip {
                Layout.fillWidth: true
                shellModel: root.activeShellModel
                registry: root.actions
                theme: root.shellTheme
                glowEnabled: presentationLayer.contextGlowAvailable
            }

            ActionBar {
                id: actionBar

                Layout.fillWidth: true
                shellModel: root.activeShellModel
                registry: root.actions
                theme: root.shellTheme
            }

            DualPaneLayout {
                id: paneLayout

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 8
                primaryModel: root.shellModel
                secondaryModel: root.secondaryShellModel
                navigationController: root
                registry: root.actions
                theme: root.shellTheme
                dualPaneEnabled: root.dualPaneEnabled
                activePane: root.activePaneIndex
                splitRatio: root.shellTheme.splitRatio
                gridMode: root.gridMode
                columnsMode: root.columnsMode
                persistenceDurationMs: presentationLayer.motionDurationMs
                glowEnabled: presentationLayer.contextGlowAvailable
                wellLayer: wellMaskLayer
                onPaneActivationRequested: paneIndex => root.activatePane(paneIndex)
                onSplitRatioCommitted: ratio => root.shellTheme.splitRatio = ratio
            }

            StatusBar {
                Layout.fillWidth: true
                shellModel: root.activeEntryModel
                registry: root.actions
                activePane: root.activePaneIndex
                paneCount: root.paneCount
                theme: root.shellTheme
            }
        }
    }

    Connections {
        target: root.shellModel

        function onPathChanged() {
            root.shellTheme.recordRecentDestination(root.shellModel.path);
        }
    }

    Connections {
        target: root.secondaryShellModel

        function onPathChanged() {
            root.shellTheme.recordRecentDestination(root.secondaryShellModel.path);
        }
    }

    WellMaskLayer {
        id: wellMaskLayer

        objectName: "wellMaskLayer"
        anchors.fill: shellContent
    }

    PresentationLayer {
        id: presentationLayer

        objectName: "presentationLayer"
        anchors.fill: shellContent
        content: shellContent
        wellMask: wellMaskLayer
        theme: root.shellTheme
        translucentGround: root.windowTransparencyEnabled
    }

    FilesystemDialogs {
        shellModel: root.activeEntryModel
        theme: root.shellTheme
        backgroundColor: root.backgroundColor
        panelColor: root.panelColor
        borderColor: root.borderColor
        primaryTextColor: root.primaryTextColor
        secondaryTextColor: root.secondaryTextColor
        accentColor: root.accentColor
        dangerColor: root.shellTheme.danger
    }
}

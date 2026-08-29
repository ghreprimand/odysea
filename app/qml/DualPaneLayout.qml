// Workspace layout for one or two live directory panes. Pointer dragging and
// declared keyboard actions update the same bounded divider ratio.
pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: layout

    property var primaryModel: null
    property var secondaryModel: null
    property var navigationController: null
    property var registry: null
    property var theme: null
    property bool dualPaneEnabled: false
    property int activePane: 0
    property real splitRatio: 0.5
    property real workingRatio: 0.5
    property bool gridMode: false
    property bool columnsMode: false
    property int persistenceDurationMs: 0
    property bool glowEnabled: false
    property var wellLayer: null
    property real minimumPaneWidth: 240
    readonly property real splitterWidth: 18
    readonly property real usableWidth: Math.max(0, width - (dualPaneEnabled ? splitterWidth : 0))
    readonly property PaneFrame secondPane: secondPaneLoader.item as PaneFrame
    readonly property var activeEntryModel: activePane === 0 ? firstPane.entryModel : (secondPane !== null ? secondPane.entryModel : secondaryModel)
    readonly property var oppositeEntryModel: activePane === 0 ? (secondPane !== null ? secondPane.entryModel : null) : firstPane.entryModel
    readonly property bool activeEntryModelReady: activePane === 0 ? firstPane.entryModelReady : (secondPane !== null && secondPane.entryModelReady)
    readonly property bool oppositeEntryModelReady: activePane === 0 ? (secondPane !== null && secondPane.entryModelReady) : firstPane.entryModelReady

    signal paneActivationRequested(int paneIndex)
    signal splitRatioCommitted(real ratio)

    function minimumRatio() {
        if (usableWidth <= 0) {
            return 0.5;
        }
        return Math.min(0.5, Math.max(0.25, minimumPaneWidth / usableWidth));
    }

    function boundedRatio(value) {
        const lower = minimumRatio();
        return Math.max(lower, Math.min(1.0 - lower, value));
    }

    function setSplitRatio(value) {
        workingRatio = boundedRatio(value);
    }

    function adjustSplitRatio(delta) {
        setSplitRatio(workingRatio + delta);
        splitRatioCommitted(workingRatio);
    }

    function focusPane(paneIndex) {
        const pane = paneItem(paneIndex);
        if (pane !== null) {
            pane.focusCurrentView();
            pane.revealCurrent();
        }
    }

    function paneItem(paneIndex) {
        return paneIndex === 0 ? firstPane : secondPaneLoader.item;
    }

    Component.onCompleted: workingRatio = boundedRatio(splitRatio)
    onWidthChanged: workingRatio = boundedRatio(workingRatio)
    onDualPaneEnabledChanged: workingRatio = boundedRatio(workingRatio)
    onSplitRatioChanged: workingRatio = boundedRatio(splitRatio)

    PaneFrame {
        id: firstPane

        objectName: "firstPane"
        x: 0
        y: 0
        width: layout.dualPaneEnabled ? Math.round(layout.usableWidth * layout.workingRatio) : layout.width
        height: layout.height
        shellModel: layout.primaryModel
        navigationController: layout.navigationController
        registry: layout.registry
        theme: layout.theme
        paneIndex: 0
        activePane: layout.activePane === 0
        dualPane: layout.dualPaneEnabled
        gridMode: layout.gridMode
        columnsMode: layout.columnsMode
        persistenceDurationMs: layout.persistenceDurationMs
        glowEnabled: layout.glowEnabled
        wellLayer: layout.wellLayer
        onActivationRequested: paneIndex => layout.paneActivationRequested(paneIndex)
    }

    Rectangle {
        id: splitter

        objectName: "paneSplitter"
        visible: layout.dualPaneEnabled
        x: firstPane.width
        y: 0
        width: layout.splitterWidth
        height: layout.height
        color: splitterPointer.pressed ? layout.theme.pressed : (splitterPointer.containsMouse ? layout.theme.hover : "transparent")

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: Math.max(48, parent.height * 0.16)
            color: layout.activePane === 0 ? layout.theme.focus : layout.theme.accent
            radius: 1
        }

        MouseArea {
            id: splitterPointer

            objectName: "paneSplitterHandle"
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            hoverEnabled: true
            cursorShape: Qt.SplitHCursor
            preventStealing: true
            Accessible.role: Accessible.Splitter
            Accessible.name: qsTr("Resize directory panes")

            onPositionChanged: mouse => {
                if (!pressed || layout.usableWidth <= 0) {
                    return;
                }
                const point = mapToItem(layout, mouse.x, mouse.y);
                layout.setSplitRatio(point.x / layout.usableWidth);
            }
            onReleased: layout.splitRatioCommitted(layout.workingRatio)
        }
    }

    Loader {
        id: secondPaneLoader

        objectName: "secondPaneLoader"
        visible: layout.dualPaneEnabled
        active: layout.dualPaneEnabled
        x: splitter.x + splitter.width
        y: 0
        width: Math.max(0, layout.width - x)
        height: layout.height
        sourceComponent: PaneFrame {
            objectName: "secondPane"
            shellModel: layout.secondaryModel
            navigationController: layout.navigationController
            registry: layout.registry
            theme: layout.theme
            paneIndex: 1
            activePane: layout.activePane === 1
            dualPane: layout.dualPaneEnabled
            gridMode: layout.gridMode
            columnsMode: layout.columnsMode
            persistenceDurationMs: layout.persistenceDurationMs
            glowEnabled: layout.glowEnabled
            wellLayer: layout.wellLayer
            onActivationRequested: paneIndex => layout.paneActivationRequested(paneIndex)
        }
    }
}

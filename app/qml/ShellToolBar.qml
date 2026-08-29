// The navigation toolbar: history and refresh actions plus workspace toggles
// for panes, view mode, and appearance. Path orientation and direct entry live
// in PathNavigator. Every button renders from the shared action registry, so
// labels, icons, enablement, and shortcut hints share one declaration site.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: bar

    required property var shellModel
    required property ActionRegistry registry
    required property var theme

    background: ChromeStrip {
        theme: bar.theme
    }

    // Below the full-row width, workspace toggles render as icons
    // only. Their accessible names and tooltips retain the action labels.
    RowLayout {
        id: toolbarRow

        anchors.fill: parent
        anchors.margins: 6
        spacing: 6
        property bool compactLayout: width < 1100 * bar.theme.uiScale

        ApplicationMark {
            objectName: "applicationIdentityMark"
            theme: bar.theme
            Layout.leftMargin: 2
            Accessible.ignored: true
        }

        ActionButton {
            objectName: "backButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "nav.back"
            showLabel: false
        }
        ActionButton {
            objectName: "forwardButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "nav.forward"
            showLabel: false
        }
        ActionButton {
            objectName: "upButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "nav.up"
            showLabel: false
        }
        ActionButton {
            objectName: "refreshButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "nav.refresh"
            showLabel: false
        }
        ActionButton {
            objectName: "undoButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "edit.undo"
            showLabel: false
        }

        Item {
            Layout.fillWidth: true
        }

        ActionButton {
            objectName: "paneToggleButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "pane.toggleDual"
            showLabel: !toolbarRow.compactLayout
        }
        ActionButton {
            objectName: "listViewButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "view.list"
            showLabel: !toolbarRow.compactLayout
        }
        ActionButton {
            objectName: "gridViewButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "view.grid"
            showLabel: !toolbarRow.compactLayout
        }
        ActionButton {
            objectName: "columnsViewButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "view.columns"
            showLabel: !toolbarRow.compactLayout
        }
        ActionButton {
            objectName: "treeSearchButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "find.tree"
            showLabel: !toolbarRow.compactLayout
        }
        ActionButton {
            objectName: "paletteButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "palette.open"
            showLabel: !toolbarRow.compactLayout
        }
        ActionButton {
            objectName: "appearanceButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "appearance.open"
            showLabel: !toolbarRow.compactLayout
        }
    }
}

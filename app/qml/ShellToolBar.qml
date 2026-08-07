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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

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

        Item {
            Layout.fillWidth: true
        }

        ActionButton {
            objectName: "paneToggleButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "pane.toggleDual"
        }
        ActionButton {
            objectName: "listViewButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "view.list"
        }
        ActionButton {
            objectName: "gridViewButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "view.grid"
        }
        ActionButton {
            objectName: "paletteButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "palette.open"
        }
        ActionButton {
            objectName: "appearanceButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "appearance.open"
        }
    }
}

// The navigation toolbar: history and refresh actions, the direct-entry
// address field, and the workspace toggles for panes, view mode, and
// appearance. Every button renders from the shared action registry, so
// its label, icon, enablement, and the key sequence its tooltip names
// all come from the same declaration the menus and shortcuts use.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: bar

    required property var shellModel
    required property ActionRegistry registry
    required property var theme

    /// Keyboard entry point for direct path entry (Ctrl+L on the shell).
    function focusAddressField() {
        addressField.forceActiveFocus();
        addressField.selectAll();
    }

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

        ShellTextField {
            id: addressField

            objectName: "addressField"
            Layout.fillWidth: true
            theme: bar.theme
            text: bar.shellModel.path
            font.family: bar.theme.pathFontFamily
            font.pixelSize: bar.theme.pathFontPixelSize
            placeholderText: qsTr("Location")
            onAccepted: {
                bar.shellModel.path = text;
                text = bar.shellModel.path;
                focus = false;
            }
        }

        Connections {
            target: bar.shellModel

            function onPathChanged() {
                if (!addressField.activeFocus) {
                    addressField.text = bar.shellModel.path;
                }
            }
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
            objectName: "appearanceButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "appearance.open"
        }
    }
}

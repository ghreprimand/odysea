// The navigation toolbar: history and refresh actions, the direct-entry
// address field, and the workspace toggles for panes, view mode, and
// appearance. Every action here is a pointer path whose keyboard
// equivalent lives on the shell's shortcut table; the tooltips name those
// sequences so both paths stay discoverable.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: bar

    required property var shellModel
    required property var navigationController
    required property var theme

    /// The shell opens the appearance surface; the toolbar only requests it,
    /// so the button works in any scene that hosts the bar.
    signal appearanceRequested

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

        ShellButton {
            objectName: "backButton"
            Accessible.name: qsTr("Back")
            theme: bar.theme
            iconName: "back"
            enabled: bar.shellModel.canGoBack
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Back (Alt+Left)")
            onClicked: bar.shellModel.goBack()
        }
        ShellButton {
            objectName: "forwardButton"
            Accessible.name: qsTr("Forward")
            theme: bar.theme
            iconName: "forward"
            enabled: bar.shellModel.canGoForward
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Forward (Alt+Right)")
            onClicked: bar.shellModel.goForward()
        }
        ShellButton {
            objectName: "upButton"
            Accessible.name: qsTr("Up")
            theme: bar.theme
            iconName: "up"
            enabled: bar.shellModel.canGoUp
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Up (Alt+Up)")
            onClicked: bar.shellModel.goUp()
        }
        ShellButton {
            objectName: "refreshButton"
            Accessible.name: qsTr("Refresh")
            theme: bar.theme
            iconName: "refresh"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Refresh (F5)")
            onClicked: bar.shellModel.refresh()
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

        ShellButton {
            theme: bar.theme
            iconName: "panes"
            text: bar.shellModel.paneCount === 2 ? qsTr("1 pane") : qsTr("2 panes")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Toggle pane workspace (Ctrl+Shift+P)")
            onClicked: bar.shellModel.setDualPaneEnabled(bar.shellModel.paneCount === 1)
        }
        ShellButton {
            objectName: "listViewButton"
            theme: bar.theme
            iconName: "list"
            text: qsTr("List")
            checkable: true
            checked: !bar.navigationController.gridMode
            ToolTip.visible: hovered
            ToolTip.text: qsTr("List view (Ctrl+Shift+1)")
            onClicked: bar.navigationController.switchView(false)
        }
        ShellButton {
            objectName: "gridViewButton"
            theme: bar.theme
            iconName: "grid"
            text: qsTr("Grid")
            checkable: true
            checked: bar.navigationController.gridMode
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Grid view (Ctrl+Shift+2)")
            onClicked: bar.navigationController.switchView(true)
        }
        ShellButton {
            objectName: "appearanceButton"
            theme: bar.theme
            iconName: "appearance"
            text: qsTr("Appearance")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Appearance settings (Ctrl+,)")
            onClicked: bar.appearanceRequested()
        }
    }
}

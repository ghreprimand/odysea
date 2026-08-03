// The tab strip: one button per open tab plus the add and close actions.
// Pointer activation lives here; the keyboard equivalents (Ctrl+1..9,
// Ctrl+Tab, Ctrl+T, Ctrl+W) live on the shell's shortcut table.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ChromeStrip {
    id: strip

    required property var shellModel

    implicitHeight: Math.max(40, strip.theme.chromeFontPixelSize + 18)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 7
        anchors.rightMargin: 7
        spacing: 5

        TabBar {
            id: tabs

            Layout.fillWidth: true
            implicitHeight: Math.max(34, strip.theme.chromeFontPixelSize + 14)
            currentIndex: strip.shellModel.activeTab

            background: Item {}

            Repeater {
                model: strip.shellModel.tabCount

                TabButton {
                    id: tabButton

                    required property int index

                    objectName: "tabButton-" + index
                    implicitWidth: 140
                    text: strip.shellModel.tabLabel(index)
                    onClicked: strip.shellModel.activateTab(index)

                    contentItem: Text {
                        text: tabButton.text
                        color: tabButton.checked ? strip.theme.accent : strip.theme.text
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: tabButton.checked ? strip.theme.background : strip.theme.panel
                        border.color: tabButton.checked ? strip.theme.accent : strip.theme.border
                        radius: 5
                    }
                }
            }
        }

        ShellButton {
            Accessible.name: qsTr("New tab")
            theme: strip.theme
            iconName: "add"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("New tab (Ctrl+T)")
            onClicked: strip.shellModel.addTab()
        }
        ShellButton {
            Accessible.name: qsTr("Close tab")
            theme: strip.theme
            iconName: "close"
            enabled: strip.shellModel.tabCount > 1
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Close tab (Ctrl+W)")
            onClicked: strip.shellModel.closeTab(strip.shellModel.activeTab)
        }
    }
}

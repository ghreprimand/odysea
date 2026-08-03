// The tab strip: one button per open tab plus the add and close actions.
// Activation, creation, and closing all route through the shared action
// registry, and one shared context menu serves every tab, parameterized
// by the tab it was opened for. A pointer opens the menu at the press
// position on the tab; the Menu key opens it anchored to the focused
// tab, never at a pointer position.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ChromeStrip {
    id: strip

    required property var shellModel
    required property ActionRegistry registry

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
                    onClicked: strip.registry.trigger("tab.activate", strip.registry.tabContext(index))
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier) !== 0)) {
                            tabMenu.openFor(strip.registry.tabContext(tabButton.index), tabButton, Qt.point(tabButton.width / 2, tabButton.height), tabButton);
                            event.accepted = true;
                        }
                    }

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

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onPressed: mouse => tabMenu.openFor(strip.registry.tabContext(tabButton.index), tabButton, Qt.point(mouse.x, mouse.y), tabButton)
                    }
                }
            }
        }

        ActionButton {
            objectName: "newTabButton"
            theme: strip.theme
            registry: strip.registry
            actionId: "tab.new"
            showLabel: false
        }
        ActionButton {
            objectName: "closeTabButton"
            theme: strip.theme
            registry: strip.registry
            actionId: "tab.close"
            showLabel: false
        }
    }

    ActionMenu {
        id: tabMenu

        objectName: "tabActionMenu"
        registry: strip.registry
        theme: strip.theme
    }
}

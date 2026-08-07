// The inactive-pane stand-in: a labeled surface a pointer click
// activates. The keyboard equivalents (F6, F3) live on the
// registry's declared sequences; the label names the switch key so the
// parked pane stays discoverable from either path. A right press opens
// the shared pane menu parameterized by this pane.
import QtQuick

Rectangle {
    id: placeholder

    required property var shellModel
    required property var registry
    required property var theme
    /// Which pane this surface stands in for.
    required property int paneIndex

    color: placeholder.theme.panel
    border.color: placeholder.theme.border
    radius: 6

    Text {
        anchors.centerIn: parent
        text: qsTr("Pane %1\nClick or press F6 to activate").arg(placeholder.paneIndex + 1)
        color: placeholder.theme.textMuted
        horizontalAlignment: Text.AlignHCenter
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            if (mouse.button === Qt.LeftButton) {
                placeholder.registry.trigger("pane.activate", placeholder.registry.paneContext(placeholder.paneIndex));
            }
        }
        onPressed: mouse => {
            if (mouse.button === Qt.RightButton) {
                paneMenu.openFor(placeholder.registry.paneContext(placeholder.paneIndex), placeholder, Qt.point(mouse.x, mouse.y), null);
            }
        }
    }

    ActionMenu {
        id: paneMenu

        objectName: "paneContextMenu"
        registry: placeholder.registry
        theme: placeholder.theme
    }
}

// The inactive-pane stand-in: a labeled surface a pointer click activates.
// The keyboard equivalent (F6) lives on the shell's shortcut table; the
// label names it so the parked pane stays discoverable from either path.
import QtQuick

Rectangle {
    id: placeholder

    required property var shellModel
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
        onClicked: placeholder.shellModel.activatePane(placeholder.paneIndex)
    }
}

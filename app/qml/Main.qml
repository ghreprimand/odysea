// OdySea main window. A first, deliberately minimal GPU-rendered listing view:
// a virtualized ListView bound to the directory model. This is the seed the
// richer shell (grid/dual-pane, thumbnails, command palette) grows from.
import QtQuick
import QtQuick.Window

Window {
    id: root
    width: 900
    height: 640
    visible: true
    title: "OdySea"
    color: "#16130f"

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 8
        model: directoryModel
        clip: true
        focus: true

        delegate: Item {
            id: row
            width: list.width
            height: 26

            required property string name
            required property bool isDir

            Row {
                spacing: 8
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    text: row.isDir ? "\u25B8" : "\u2022"
                    color: row.isDir ? "#ffb454" : "#7a7266"
                    font.pixelSize: 14
                }
                Text {
                    text: row.name
                    color: "#e8e2d6"
                    font.pixelSize: 14
                    font.family: "monospace"
                }
            }
        }

        highlight: Rectangle { color: "#2a2118"; radius: 4 }
        highlightMoveDuration: 80
    }
}

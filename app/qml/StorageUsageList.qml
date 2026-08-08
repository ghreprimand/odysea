// Accessible list equivalent for the storage map. It is visible by default,
// carries the same model and selection, and exposes content-derived names and
// state rather than serving as a failure-only fallback.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

FocusScope {
    id: list

    required property var usageModel
    required property var theme

    Accessible.role: Accessible.List
    Accessible.name: qsTr("Storage usage list")
    Accessible.focusable: true
    Accessible.focused: list.activeFocus

    Rectangle {
        anchors.fill: parent
        color: list.theme.well
        border.color: list.activeFocus ? list.theme.accent : list.theme.border
        radius: 5
    }

    ListView {
        id: usageList

        objectName: "storageUsageList"
        anchors.fill: parent
        anchors.margins: 4
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: list.usageModel
        currentIndex: list.usageModel.currentIndex
        activeFocusOnTab: true
        focus: true

        Keys.onUpPressed: event => {
            list.usageModel.moveCursor(-1);
            usageList.positionViewAtIndex(list.usageModel.currentIndex, ListView.Contain);
            event.accepted = true;
        }
        Keys.onDownPressed: event => {
            list.usageModel.moveCursor(1);
            usageList.positionViewAtIndex(list.usageModel.currentIndex, ListView.Contain);
            event.accepted = true;
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home) {
                list.usageModel.selectRow(0);
                usageList.positionViewAtBeginning();
                event.accepted = true;
            } else if (event.key === Qt.Key_End) {
                list.usageModel.selectRow(usageList.count - 1);
                usageList.positionViewAtEnd();
                event.accepted = true;
            }
        }
        Keys.onReturnPressed: event => {
            list.usageModel.activateCurrent();
            event.accepted = true;
        }
        Keys.onEnterPressed: event => {
            list.usageModel.activateCurrent();
            event.accepted = true;
        }

        delegate: Rectangle {
            id: row

            required property int index
            required property string name
            required property string kindLabel
            required property bool isDirectory
            required property double apparentBytes
            required property double allocatedBytes
            required property string apparentText
            required property string allocatedText
            required property double fileCount
            required property double directoryCount
            required property double deduplicatedEntries
            required property bool finished
            required property bool selected

            objectName: "storageListRow-" + index
            width: usageList.width
            height: Math.max(38 * list.theme.uiScale, list.theme.rowHeight)
            color: selected ? list.theme.selectionBed : (rowMouse.containsMouse ? list.theme.hover : "transparent")
            border.color: selected ? list.theme.accent : "transparent"
            border.width: selected ? 1 : 0
            radius: 3

            Accessible.role: Accessible.ListItem
            Accessible.name: qsTr("%1, %2, %3 allocated, %4 apparent").arg(name).arg(kindLabel).arg(allocatedText).arg(apparentText)
            Accessible.description: isDirectory ? qsTr("Open this subtree") : qsTr("Select this entry")
            Accessible.focusable: true
            Accessible.focused: usageList.activeFocus && index === list.usageModel.currentIndex
            Accessible.selectable: true
            Accessible.selected: selected

            Row {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 10

                Text {
                    width: Math.max(80, parent.width * 0.40)
                    height: parent.height
                    text: row.name
                    color: list.theme.text
                    font.family: list.theme.contentFontFamily
                    font.pixelSize: list.theme.contentFontPixelSize
                    font.bold: row.selected
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                }

                Text {
                    width: Math.max(70, parent.width * 0.17)
                    height: parent.height
                    text: row.allocatedText
                    color: list.theme.textMuted
                    font.family: list.theme.captionFontFamily
                    font.pixelSize: list.theme.captionFontPixelSize
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                Text {
                    width: Math.max(70, parent.width * 0.17)
                    height: parent.height
                    text: row.apparentText
                    color: list.theme.textMuted
                    font.family: list.theme.captionFontFamily
                    font.pixelSize: list.theme.captionFontPixelSize
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                Text {
                    width: Math.max(60, parent.width * 0.14)
                    height: parent.height
                    text: row.finished ? qsTr("%1 items").arg(row.fileCount + row.directoryCount) : qsTr("Scanning…")
                    color: row.finished ? list.theme.textFaint : list.theme.accent
                    font.family: list.theme.captionFontFamily
                    font.pixelSize: list.theme.captionFontPixelSize
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            MouseArea {
                id: rowMouse

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                onClicked: {
                    list.usageModel.selectRow(row.index);
                    usageList.forceActiveFocus();
                }
                onDoubleClicked: list.usageModel.activate(row.index)
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }
}

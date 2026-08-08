// Proportional, interactive view of one storage-usage scan. The horizontal
// strip virtualizes immediate children and gives each a share of the viewport
// based on allocated bytes, with a minimum target size for pointer access.
// Selection and activation route through the same model as the parallel list.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

FocusScope {
    id: map

    required property var usageModel
    required property var theme

    implicitHeight: 170 * map.theme.uiScale
    Accessible.role: Accessible.List
    Accessible.name: qsTr("Storage usage map")
    Accessible.focusable: true
    Accessible.focused: map.activeFocus

    Rectangle {
        anchors.fill: parent
        color: map.theme.well
        border.color: map.activeFocus ? map.theme.accent : map.theme.border
        radius: 5
    }

    ListView {
        id: mapView

        objectName: "storageUsageMap"
        anchors.fill: parent
        anchors.margins: 6
        orientation: ListView.Horizontal
        spacing: 4
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: map.usageModel
        currentIndex: map.usageModel.currentIndex
        activeFocusOnTab: true
        focus: true

        Keys.onLeftPressed: event => {
            map.usageModel.moveCursor(-1);
            mapView.positionViewAtIndex(map.usageModel.currentIndex, ListView.Contain);
            event.accepted = true;
        }
        Keys.onRightPressed: event => {
            map.usageModel.moveCursor(1);
            mapView.positionViewAtIndex(map.usageModel.currentIndex, ListView.Contain);
            event.accepted = true;
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home) {
                map.usageModel.selectRow(0);
                mapView.positionViewAtBeginning();
                event.accepted = true;
            } else if (event.key === Qt.Key_End) {
                map.usageModel.selectRow(mapView.count - 1);
                mapView.positionViewAtEnd();
                event.accepted = true;
            }
        }
        Keys.onReturnPressed: event => {
            map.usageModel.activateCurrent();
            event.accepted = true;
        }
        Keys.onEnterPressed: event => {
            map.usageModel.activateCurrent();
            event.accepted = true;
        }

        delegate: Rectangle {
            id: slice

            required property int index
            required property string name
            required property string kindLabel
            required property bool isDirectory
            required property double apparentBytes
            required property double allocatedBytes
            required property string apparentText
            required property string allocatedText
            required property bool finished
            required property bool selected

            readonly property string accessibleLabel: qsTr("%1, %2, %3 allocated, %4 apparent").arg(name).arg(kindLabel).arg(allocatedText).arg(apparentText)

            objectName: "storageMapSlice-" + index
            width: Math.max(92 * map.theme.uiScale, mapView.width * (allocatedBytes / Math.max(1, map.usageModel.allocatedBytes)))
            height: mapView.height
            color: selected ? map.theme.selectionBed : (index % 2 === 0 ? map.theme.panel : map.theme.hover)
            border.color: selected ? map.theme.accent : map.theme.border
            border.width: selected ? 2 : 1
            radius: 4

            Accessible.role: Accessible.Button
            Accessible.name: accessibleLabel
            Accessible.description: isDirectory ? qsTr("Open this subtree") : qsTr("Select this entry")
            Accessible.focusable: true
            Accessible.focused: mapView.activeFocus && index === map.usageModel.currentIndex
            Accessible.selected: selected

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 5

                Text {
                    width: parent.width
                    text: slice.name
                    color: map.theme.text
                    font.family: map.theme.contentFontFamily
                    font.pixelSize: map.theme.contentFontPixelSize
                    font.bold: slice.selected
                    elide: Text.ElideMiddle
                }

                Text {
                    width: parent.width
                    text: qsTr("%1 allocated").arg(slice.allocatedText)
                    color: map.theme.textMuted
                    font.family: map.theme.captionFontFamily
                    font.pixelSize: map.theme.captionFontPixelSize
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: slice.finished ? slice.kindLabel : qsTr("Scanning…")
                    color: slice.finished ? map.theme.textFaint : map.theme.accent
                    font.family: map.theme.captionFontFamily
                    font.pixelSize: map.theme.captionFontPixelSize
                    elide: Text.ElideRight
                }
            }

            MouseArea {
                id: sliceMouse

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                onClicked: {
                    map.usageModel.selectRow(slice.index);
                    mapView.forceActiveFocus();
                }
                onDoubleClicked: map.usageModel.activate(slice.index)
            }

            ToolTip.visible: sliceMouse.containsMouse
            ToolTip.text: slice.accessibleLabel
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }
}

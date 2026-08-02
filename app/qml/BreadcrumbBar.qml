pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

FocusScope {
    id: bar

    required property var shellModel
    required property var navigationController
    required property color backgroundColor
    required property color borderColor
    required property color primaryTextColor
    required property color accentColor
    required property string pathFontFamily
    required property int pathFontPixelSize

    // Optional roles with the shell's former fixed values as defaults, so the
    // bar renders sensibly when a scene does not bind a theme.
    property color hoverColor: "#382b22"
    property color pressedColor: "#4a392b"

    readonly property var segments: shellModel.path.length >= 0 ? shellModel.breadcrumbSegments() : []

    implicitHeight: Math.max(36, pathFontPixelSize + 18)

    function activateSegment(path) {
        shellModel.navigateToPath(path);
        navigationController.clearTypeAhead();
        navigationController.focusCurrentView();
    }

    Flickable {
        id: scroller

        anchors.fill: parent
        contentWidth: crumbs.implicitWidth
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        Row {
            id: crumbs

            height: scroller.height
            spacing: 2

            Repeater {
                id: crumbRepeater

                model: bar.segments

                delegate: Button {
                    id: crumbButton

                    required property int index
                    required property var modelData
                    readonly property string segmentPath: modelData.path
                    readonly property string segmentUrl: modelData.url

                    objectName: "breadcrumb-" + index
                    implicitWidth: label.implicitWidth + 24 + (index + 1 < crumbRepeater.count ? separator.implicitWidth + 6 : 0)
                    height: crumbs.height
                    text: modelData.label
                    Accessible.name: qsTr("Open location %1").arg(modelData.path)

                    function dropSelectedEntries(action) {
                        return bar.shellModel.dropSelection(segmentPath, action === Qt.MoveAction, 0);
                    }

                    Keys.onLeftPressed: {
                        const previous = crumbRepeater.itemAt(Math.max(0, index - 1));
                        if (previous !== null) {
                            previous.forceActiveFocus();
                        }
                    }
                    Keys.onRightPressed: {
                        const next = crumbRepeater.itemAt(Math.min(crumbRepeater.count - 1, index + 1));
                        if (next !== null) {
                            next.forceActiveFocus();
                        }
                    }
                    Keys.onReturnPressed: bar.activateSegment(modelData.path)
                    Keys.onEnterPressed: bar.activateSegment(modelData.path)
                    onClicked: bar.activateSegment(modelData.path)

                    contentItem: Row {
                        spacing: 6

                        Text {
                            id: label

                            text: crumbButton.text
                            color: bar.primaryTextColor
                            elide: Text.ElideMiddle
                            font.family: bar.pathFontFamily
                            font.pixelSize: bar.pathFontPixelSize
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            id: separator

                            visible: crumbButton.index + 1 < crumbRepeater.count
                            text: "\u203a"
                            color: bar.primaryTextColor
                            font.family: bar.pathFontFamily
                            font.pixelSize: bar.pathFontPixelSize
                        }
                    }

                    background: Rectangle {
                        color: crumbButton.down ? bar.pressedColor : (crumbButton.hovered ? bar.hoverColor : bar.backgroundColor)
                        border.color: crumbButton.activeFocus ? bar.accentColor : bar.borderColor
                        radius: 4
                    }

                    DropArea {
                        objectName: "breadcrumbDropTarget-" + crumbButton.index
                        anchors.fill: crumbButton
                        keys: ["odysea-entry"]
                        onDropped: drop => {
                            if (crumbButton.dropSelectedEntries(drop.proposedAction)) {
                                drop.acceptProposedAction();
                            }
                        }
                    }
                }
            }
        }
    }
}

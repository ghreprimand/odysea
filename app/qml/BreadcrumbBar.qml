pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

FocusScope {
    id: bar

    required property var shellModel
    required property var navigationController
    required property ActionRegistry registry
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

    /// Routed to the shell-wide action registry by the host. The breadcrumb
    /// never declares a private menu, so every path surface shares the same
    /// action descriptors and enablement rules.
    signal contextActionsRequested(string surfaceKind, string targetPath, string targetLabel, var anchor, point position)

    // Reading path creates the binding dependency that refreshes the derived
    // segment list after navigation; breadcrumbSegments() is invokable and
    // cannot advertise that dependency to the QML engine by itself.
    readonly property var segments: shellModel !== null && shellModel.path.length >= 0 ? shellModel.breadcrumbSegments() : []

    implicitHeight: Math.max(36, pathFontPixelSize + 18)

    function activateSegment(path) {
        if (registry.trigger("location.open", registry.breadcrumbContext(path))) {
            navigationController.clearTypeAhead();
            navigationController.focusCurrentView();
        }
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
                    readonly property string actionSurfaceKind: "breadcrumb"
                    readonly property string actionTargetPath: segmentPath

                    objectName: "breadcrumb-" + index
                    implicitWidth: label.implicitWidth + 24 + (index + 1 < crumbRepeater.count ? separator.implicitWidth + 6 : 0)
                    height: crumbs.height
                    text: modelData.label
                    Accessible.name: qsTr("Open location %1").arg(modelData.path)

                    function dropSelectedEntries(action) {
                        return bar.shellModel.dropSelection(segmentPath, action === Qt.MoveAction, 0);
                    }

                    function requestContext(position) {
                        bar.contextActionsRequested(actionSurfaceKind, actionTargetPath, text, crumbButton, position);
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
                    Keys.onPressed: event => {
                        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier) !== 0)) {
                            crumbButton.requestContext(Qt.point(crumbButton.width / 2, crumbButton.height));
                            event.accepted = true;
                        }
                    }
                    onClicked: bar.activateSegment(modelData.path)

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: (eventPoint, button) => crumbButton.requestContext(eventPoint.position)
                    }

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

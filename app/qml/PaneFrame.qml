// One live directory pane with an active-pane marker and transfer controls.
// The directory model is independent from the other pane; only activation and
// workspace-level actions travel through the shared navigation controller.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: frame

    property var shellModel: null
    property var navigationController: null
    property var registry: null
    property var theme: null
    property int paneIndex: 0
    property bool activePane: false
    property bool dualPane: false
    property bool gridMode: false
    property bool columnsMode: false
    property int persistenceDurationMs: 0
    property var wellLayer: null
    readonly property var entryModel: directoryPane.entryModel

    signal activationRequested(int paneIndex)

    function focusCurrentView() {
        directoryPane.focusCurrentView();
    }

    function revealCurrent() {
        directoryPane.revealCurrent();
    }

    focus: activePane

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: frame.activePane ? frame.theme.focus : frame.theme.border
        border.width: frame.activePane ? 2 : 1
        radius: 6
    }

    ChromeStrip {
        id: paneHeader

        objectName: "paneHeader-" + frame.paneIndex
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 2
        height: Math.max(34, frame.theme.captionFontPixelSize + 16)
        theme: frame.theme

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onClicked: frame.activationRequested(frame.paneIndex)
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 4
            spacing: 6

            Rectangle {
                Layout.preferredWidth: 3
                Layout.preferredHeight: Math.max(14, frame.theme.captionFontPixelSize)
                color: frame.activePane ? frame.theme.focus : frame.theme.border
                radius: 2
            }

            Text {
                text: qsTr("Pane %1").arg(frame.paneIndex + 1)
                color: frame.activePane ? frame.theme.text : frame.theme.textMuted
                font.family: frame.theme.captionFontFamily
                font.pixelSize: frame.theme.captionFontPixelSize
                font.bold: frame.activePane
            }

            Text {
                Layout.fillWidth: true
                text: frame.entryModel !== null ? frame.entryModel.path : ""
                color: frame.theme.textMuted
                elide: Text.ElideMiddle
                font.family: frame.theme.pathFontFamily
                font.pixelSize: frame.theme.captionFontPixelSize
            }

            ActionButton {
                objectName: "paneCopyToOther-" + frame.paneIndex
                visible: frame.dualPane && frame.activePane
                theme: frame.theme
                registry: frame.registry
                actionId: "pane.copyToOther"
                showLabel: false
            }

            ActionButton {
                objectName: "paneMoveToOther-" + frame.paneIndex
                visible: frame.dualPane && frame.activePane
                theme: frame.theme
                registry: frame.registry
                actionId: "pane.moveToOther"
                showLabel: false
            }
        }
    }

    DirectoryPane {
        id: directoryPane

        objectName: "directoryPane-" + frame.paneIndex
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: paneHeader.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 2
        shellModel: frame.shellModel
        navigationController: frame.navigationController
        registry: frame.registry
        theme: frame.theme
        gridMode: frame.gridMode
        columnsMode: frame.columnsMode
        persistenceDurationMs: frame.persistenceDurationMs
        wellLayer: frame.wellLayer

        onActiveFocusChanged: {
            if (activeFocus) {
                frame.activationRequested(frame.paneIndex);
            }
        }
    }
}

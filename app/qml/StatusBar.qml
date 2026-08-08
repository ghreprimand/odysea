// The status strip: scan and operation activity, the current status or
// error message, the selection count, and the pane indicator. Errors take
// the danger role; everything else reads in the muted caption ink.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ChromeStrip {
    id: bar

    required property var shellModel
    /// Workspace layouts can override these when pane state lives above
    /// the directory adapters. The defaults preserve standalone use.
    property int activePane: shellModel.activePane
    property int paneCount: shellModel.paneCount

    implicitHeight: Math.max(30, bar.theme.captionFontPixelSize + 14)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 12

        BusyIndicator {
            visible: bar.shellModel.busy || bar.shellModel.operationBusy
            running: visible
            implicitWidth: 20
            implicitHeight: 20
        }

        Text {
            objectName: "statusMessageText"
            Layout.fillWidth: true
            text: bar.shellModel.operationErrorString.length > 0 ? bar.shellModel.operationErrorString : (bar.shellModel.errorString.length > 0 ? qsTr("Could not read folder: ") + bar.shellModel.errorString : bar.shellModel.statusMessage)
            color: bar.shellModel.operationErrorString.length > 0 || bar.shellModel.errorString.length > 0 ? bar.theme.danger : bar.theme.textMuted
            elide: Text.ElideRight
            font.family: bar.theme.captionFontFamily
            font.pixelSize: bar.theme.captionFontPixelSize
        }

        Text {
            text: qsTr("%1 selected").arg(bar.shellModel.selectedCount)
            color: bar.theme.textMuted
            font.family: bar.theme.captionFontFamily
            font.pixelSize: bar.theme.captionFontPixelSize
        }

        Text {
            text: qsTr("Pane %1 of %2").arg(bar.activePane + 1).arg(bar.paneCount)
            color: bar.theme.textMuted
            font.family: bar.theme.captionFontFamily
            font.pixelSize: bar.theme.captionFontPixelSize
        }
    }
}

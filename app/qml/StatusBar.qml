// The status strip: scan and operation activity, the current status or
// error message, the selection count, and the pane indicator. Errors take
// the danger role; everything else reads in the muted caption ink.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ChromeStrip {
    id: bar

    required property var shellModel
    /// The shared action registry, when the surrounding layout has one. The
    /// transfer controls render through it so their wording, icons, and
    /// enablement come from the same declaration the keyboard and the menus
    /// use. Absent in standalone scenes, where the controls are simply not
    /// shown rather than restated here.
    property var registry: null
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

        // The running transfer: how far along it is, what it is working on,
        // and the two controls that act on it. Shown only while something is
        // running that can actually be held or stopped.
        ProgressBar {
            objectName: "operationProgressBar"
            visible: bar.shellModel.operationInterruptible
            indeterminate: !bar.shellModel.operationProgressKnown
            value: bar.shellModel.operationProgress
            implicitWidth: 120
        }

        Text {
            objectName: "operationEstimateText"
            visible: bar.shellModel.operationInterruptible
            text: bar.shellModel.operationPaused ? qsTr("Held") : (bar.shellModel.operationEntry.length > 0 ? qsTr("%1 — %2").arg(bar.shellModel.operationEntry).arg(bar.shellModel.operationEstimate) : bar.shellModel.operationEstimate)
            color: bar.theme.textMuted
            elide: Text.ElideRight
            Layout.maximumWidth: 240
            font.family: bar.theme.captionFontFamily
            font.pixelSize: bar.theme.captionFontPixelSize
        }

        ActionButton {
            objectName: "operationHoldButton"
            visible: bar.registry !== null && bar.shellModel.operationInterruptible && !bar.shellModel.operationPaused
            theme: bar.theme
            registry: bar.registry
            actionId: "operation.pause"
            showLabel: false
        }

        ActionButton {
            objectName: "operationResumeButton"
            visible: bar.registry !== null && bar.shellModel.operationInterruptible && bar.shellModel.operationPaused
            theme: bar.theme
            registry: bar.registry
            actionId: "operation.resume"
            showLabel: false
        }

        ActionButton {
            objectName: "operationStopButton"
            visible: bar.registry !== null && bar.shellModel.operationInterruptible
            theme: bar.theme
            registry: bar.registry
            actionId: "operation.cancel"
            showLabel: false
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

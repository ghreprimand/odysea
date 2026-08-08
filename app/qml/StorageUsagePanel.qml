// Modal storage-usage workspace. The proportional map and accessible list are
// parallel, always-visible representations of one model. Every action has a
// pointer control and a keyboard route, and hiding the panel cancels the scan.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: panel

    required property var usageModel
    required property var theme

    function openFor(path) {
        panel.usageModel.start(path);
        panel.open();
        Qt.callLater(function () {
            usageMap.forceActiveFocus();
        });
    }

    objectName: "storageUsagePanel"
    width: Math.min(parent ? parent.width - 32 : 1000, 1120 * panel.theme.uiScale)
    height: Math.min(parent ? parent.height - 32 : 700, 760 * panel.theme.uiScale)
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    onAboutToHide: panel.usageModel.cancel()

    background: Rectangle {
        color: panel.theme.background
        border.color: panel.theme.border
        radius: 7
    }

    contentItem: ColumnLayout {
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ShellButton {
                objectName: "storageUsageUpButton"
                theme: panel.theme
                iconName: "up"
                text: qsTr("Up")
                enabled: panel.usageModel.canGoUp
                Accessible.name: qsTr("Scan parent folder")
                onClicked: panel.usageModel.goUp()
            }

            Text {
                Layout.fillWidth: true
                text: panel.usageModel.rootPath
                color: panel.theme.text
                font.family: panel.theme.pathFontFamily
                font.pixelSize: panel.theme.pathFontPixelSize
                elide: Text.ElideMiddle
            }

            ShellButton {
                objectName: "storageUsageCancelButton"
                theme: panel.theme
                text: panel.usageModel.cancelling ? qsTr("Cancelling…") : qsTr("Cancel scan")
                enabled: panel.usageModel.busy && !panel.usageModel.cancelling
                Accessible.name: qsTr("Cancel storage scan")
                onClicked: panel.usageModel.cancel()
            }

            ShellButton {
                objectName: "storageUsageCloseButton"
                theme: panel.theme
                iconName: "close"
                text: qsTr("Close")
                Accessible.name: qsTr("Close storage usage")
                onClicked: panel.close()
            }
        }

        ProgressBar {
            objectName: "storageUsageProgress"
            Layout.fillWidth: true
            visible: panel.usageModel.busy
            indeterminate: panel.usageModel.busy
            Accessible.name: qsTr("Storage scan progress")
            Accessible.description: qsTr("%1 entries examined").arg(panel.usageModel.entriesVisited)
        }

        Text {
            objectName: "storageUsageSummary"
            Layout.fillWidth: true
            text: qsTr("%1 entries examined • %2 allocated • %3 apparent").arg(panel.usageModel.entriesVisited).arg(panel.usageModel.formatBytes(panel.usageModel.allocatedBytes)).arg(panel.usageModel.formatBytes(panel.usageModel.apparentBytes))
            color: panel.theme.textMuted
            font.family: panel.theme.captionFontFamily
            font.pixelSize: panel.theme.captionFontPixelSize
            elide: Text.ElideRight
        }

        Text {
            objectName: "storageUsageAccounting"
            Layout.fillWidth: true
            text: qsTr("%1 repeated entries deduplicated • %2 unreadable folders • %3 filesystem boundaries skipped").arg(panel.usageModel.deduplicatedEntries).arg(panel.usageModel.unreadableDirectories).arg(panel.usageModel.skippedBoundaries)
            color: panel.usageModel.unreadableDirectories > 0 ? panel.theme.danger : panel.theme.textFaint
            font.family: panel.theme.captionFontFamily
            font.pixelSize: panel.theme.captionFontPixelSize
            elide: Text.ElideRight
        }

        Text {
            objectName: "storageUsageState"
            Layout.fillWidth: true
            visible: panel.usageModel.cancelled || panel.usageModel.errorString.length > 0
            text: panel.usageModel.errorString.length > 0 ? panel.usageModel.errorString : qsTr("Scan cancelled; partial totals remain visible")
            color: panel.theme.danger
            font.family: panel.theme.captionFontFamily
            font.pixelSize: panel.theme.captionFontPixelSize
            wrapMode: Text.Wrap
        }

        StorageUsageMap {
            id: usageMap

            objectName: "storageUsageMapSurface"
            Layout.fillWidth: true
            Layout.preferredHeight: 170 * panel.theme.uiScale
            usageModel: panel.usageModel
            theme: panel.theme
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Accessible list — Allocated — Apparent — Contents")
            color: panel.theme.textMuted
            font.family: panel.theme.captionFontFamily
            font.pixelSize: panel.theme.captionFontPixelSize
        }

        StorageUsageList {
            id: usageList

            objectName: "storageUsageListSurface"
            Layout.fillWidth: true
            Layout.fillHeight: true
            usageModel: panel.usageModel
            theme: panel.theme
        }
    }

    Shortcut {
        sequence: "Alt+Up"
        enabled: panel.opened && panel.usageModel.canGoUp
        onActivated: panel.usageModel.goUp()
    }

    Shortcut {
        sequence: "Escape"
        enabled: panel.opened
        onActivated: {
            if (panel.usageModel.busy) {
                panel.usageModel.cancel();
            } else {
                panel.close();
            }
        }
    }
}

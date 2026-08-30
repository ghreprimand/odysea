// Modal quick preview for one focused entry. Content loading belongs to the
// cancellable C++ model; this surface owns only semantic presentation, focus,
// and the keyboard/pointer dismissal routes.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OdySea

Popup {
    id: preview

    required property var previewModel
    required property var theme

    readonly property bool effectsOff: theme.profile === ShellTheme.Off || theme.highContrast
    /// Reduced motion removes time only. The panel, ink, focus border, and
    /// image luminance continue to use the same semantic tokens.
    readonly property int transitionDurationMs: (theme.reducedMotion || preview.effectsOff) ? 0 : 120
    readonly property color semanticPanelColor: theme.panel

    signal dismissed

    objectName: "quickPreviewOverlay"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(860, parent ? parent.width - 80 : 860)
    height: Math.min(620, parent ? parent.height - 100 : 620)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 12

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: preview.transitionDurationMs
        }
    }
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: preview.transitionDurationMs
        }
    }

    background: Rectangle {
        color: preview.semanticPanelColor
        border.color: preview.theme.highContrast ? preview.theme.text : preview.theme.border
        border.width: preview.theme.highContrast ? 2 : 1
        radius: preview.theme.highContrast ? 0 : 8
    }

    function openFor(path) {
        preview.previewModel.open(path);
        preview.open();
        previewFocusSurface.forceActiveFocus();
    }

    onAboutToHide: previewModel.cancel()
    onClosed: dismissed()

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            VectorIcon {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                name: "preview"
                highContrast: preview.theme.highContrast
                ink: preview.theme.iconInk
                Accessible.ignored: true
            }

            Text {
                objectName: "quickPreviewTitle"
                Layout.fillWidth: true
                text: preview.previewModel.displayName.length > 0 ? preview.previewModel.displayName : qsTr("Quick preview")
                elide: Text.ElideMiddle
                color: preview.theme.text
                font.family: preview.theme.chromeFontFamily
                font.pixelSize: preview.theme.chromeFontPixelSize
            }

            ShellButton {
                objectName: "closeQuickPreviewButton"
                theme: preview.theme
                text: qsTr("Close")
                iconName: "close"
                Accessible.name: qsTr("Close quick preview")
                onClicked: preview.close()
            }
        }

        Rectangle {
            id: previewFocusSurface

            objectName: "quickPreviewFocusSurface"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: preview.theme.well
            border.color: activeFocus ? preview.theme.focus : preview.theme.border
            border.width: activeFocus ? 2 : 1
            radius: preview.theme.highContrast ? 0 : 4
            activeFocusOnTab: true
            focus: true
            Accessible.role: Accessible.Pane
            Accessible.name: qsTr("Preview of %1").arg(preview.previewModel.displayName)

            Keys.onEscapePressed: event => {
                preview.close();
                event.accepted = true;
            }

            Text {
                objectName: "quickPreviewLoading"
                anchors.centerIn: parent
                visible: preview.previewModel.state === QuickPreviewModel.Loading
                text: qsTr("Loading preview…")
                color: preview.theme.textMuted
                font.family: preview.theme.chromeFontFamily
                font.pixelSize: preview.theme.chromeFontPixelSize
            }

            PreviewImageItem {
                objectName: "quickPreviewImage"
                anchors.fill: previewFocusSurface
                anchors.margins: 12
                visible: preview.previewModel.state === QuickPreviewModel.Ready && preview.previewModel.contentKind === QuickPreviewModel.RasterImage
                source: preview.previewModel.image
            }

            ScrollView {
                id: textScroll

                objectName: "quickPreviewTextScroll"
                anchors.fill: parent
                anchors.margins: 8
                visible: preview.previewModel.state === QuickPreviewModel.Ready && (preview.previewModel.contentKind === QuickPreviewModel.PlainText || preview.previewModel.contentKind === QuickPreviewModel.MarkdownDocument)
                clip: true

                TextArea {
                    objectName: "quickPreviewText"
                    readOnly: true
                    selectByMouse: true
                    text: preview.previewModel.text
                    textFormat: preview.previewModel.contentKind === QuickPreviewModel.MarkdownDocument ? TextEdit.MarkdownText : TextEdit.PlainText
                    wrapMode: preview.previewModel.contentKind === QuickPreviewModel.MarkdownDocument ? TextEdit.WordWrap : TextEdit.NoWrap
                    color: preview.theme.longFormInk
                    selectionColor: preview.theme.selectionBed
                    selectedTextColor: preview.theme.selectionInk
                    font.family: preview.previewModel.contentKind === QuickPreviewModel.MarkdownDocument ? preview.theme.longFormFontFamily : preview.theme.contentFontFamily
                    font.pixelSize: preview.previewModel.contentKind === QuickPreviewModel.MarkdownDocument ? preview.theme.longFormFontPixelSize : preview.theme.contentFontPixelSize
                    background: null
                }
            }

            LongFormText {
                objectName: "quickPreviewMessage"
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.width - 48)
                visible: preview.previewModel.state === QuickPreviewModel.Unsupported || preview.previewModel.state === QuickPreviewModel.Error
                theme: preview.theme
                text: preview.previewModel.message
                horizontalAlignment: Text.AlignHCenter
            }
        }

        Text {
            objectName: "quickPreviewNote"
            Layout.fillWidth: true
            visible: preview.previewModel.state === QuickPreviewModel.Ready && preview.previewModel.message.length > 0
            text: preview.previewModel.message
            color: preview.theme.textMuted
            font.family: preview.theme.captionFontFamily
            font.pixelSize: preview.theme.captionFontPixelSize
            elide: Text.ElideRight
        }
    }
}

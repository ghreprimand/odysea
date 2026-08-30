// The appearance settings surface. Every control writes the shared theme
// state the moment it moves, so the shell restyles live; nothing here waits
// for a confirmation step. Reset restores the shipped configuration.
//
// Reachable by keyboard and mouse alike: the shell opens it from a toolbar
// button and a shortcut, controls participate in tab focus, and Escape closes.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: panel

    required property var theme

    objectName: "appearancePanel"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(560, parent ? parent.width - 40 : 560)
    height: Math.min(640, parent ? parent.height - 40 : 640)
    anchors.centerIn: parent
    padding: 14

    background: Rectangle {
        color: panel.theme.panel
        border.color: panel.theme.border
        radius: 8
    }

    component SectionLabel: Text {
        Layout.topMargin: 8
        color: panel.theme.textMuted
        font.family: panel.theme.captionFontFamily
        font.pixelSize: panel.theme.captionFontPixelSize
        font.bold: true
    }

    component FieldLabel: Text {
        Layout.preferredWidth: 130
        color: panel.theme.text
        font.family: panel.theme.chromeFontFamily
        font.pixelSize: panel.theme.chromeFontPixelSize
        elide: Text.ElideRight
    }

    component ValueLabel: Text {
        Layout.preferredWidth: 40
        color: panel.theme.textMuted
        font.family: panel.theme.captionFontFamily
        font.pixelSize: panel.theme.captionFontPixelSize
        horizontalAlignment: Text.AlignRight
    }

    // A slider that follows the theme while idle and writes to it while
    // moved. The explicit Binding keeps the follow direction alive after the
    // user interacts, so profile presets keep steering the handles.
    component ThemeSlider: RowLayout {
        id: sliderRow

        property alias sliderObjectName: control.objectName
        property string label
        property real from: 0
        property real to: 1
        property real themeValue
        signal moved(real value)

        Layout.fillWidth: true
        spacing: 8

        FieldLabel {
            text: sliderRow.label
        }
        Slider {
            id: control

            // The visible FieldLabel is a sibling, so assistive technology
            // needs the association made explicitly.
            Accessible.name: sliderRow.label
            Layout.fillWidth: true
            from: sliderRow.from
            to: sliderRow.to
            onMoved: sliderRow.moved(control.value)

            Binding on value {
                value: sliderRow.themeValue
            }
        }
        ValueLabel {
            text: control.value.toFixed(2)
        }
    }

    component ThemeCombo: RowLayout {
        id: comboRow

        property alias comboObjectName: control.objectName
        property alias model: control.model
        property string label
        property string textRole
        property int themeIndex
        signal picked(int index)

        Layout.fillWidth: true
        spacing: 8

        FieldLabel {
            text: comboRow.label
        }
        ComboBox {
            id: control

            Accessible.name: comboRow.label
            Layout.fillWidth: true
            textRole: comboRow.textRole
            onActivated: index => comboRow.picked(index)

            Binding on currentIndex {
                value: comboRow.themeIndex
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 6

        Text {
            text: qsTr("Appearance")
            color: panel.theme.text
            font.family: panel.theme.chromeFontFamily
            font.pixelSize: panel.theme.chromeFontPixelSize
            font.bold: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 6

                SectionLabel {
                    text: qsTr("Colors")
                }
                ThemeCombo {
                    comboObjectName: "paletteBox"
                    label: qsTr("Palette")
                    model: panel.theme.availablePalettes
                    themeIndex: panel.theme.availablePalettes.indexOf(panel.theme.paletteId)
                    onPicked: index => panel.theme.paletteId = panel.theme.availablePalettes[index]
                }
                ThemeCombo {
                    comboObjectName: "accentPresetBox"
                    label: qsTr("Accent")
                    model: panel.theme.accentPresets
                    textRole: "name"
                    themeIndex: panel.theme.accentPresetIndex
                    onPicked: index => panel.theme.accentPresetIndex = index
                }
                Rectangle {
                    objectName: "accentPreview"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 6
                    color: panel.theme.accent
                    radius: 3
                }
                Text {
                    objectName: "accentContrastWarning"
                    Layout.fillWidth: true
                    visible: panel.theme.accentContrastWarning.length > 0
                    text: panel.theme.accentContrastWarning
                    color: panel.theme.danger
                    wrapMode: Text.Wrap
                    font.family: panel.theme.captionFontFamily
                    font.pixelSize: panel.theme.captionFontPixelSize
                    Accessible.name: text
                }
                CheckBox {
                    objectName: "highContrastCheck"
                    text: qsTr("High contrast")
                    checked: panel.theme.highContrast
                    onToggled: panel.theme.highContrast = checked
                }

                SectionLabel {
                    text: qsTr("Screen effects")
                }
                ThemeCombo {
                    comboObjectName: "profileBox"
                    label: qsTr("Profile")
                    model: [qsTr("Off"), qsTr("Minimal"), qsTr("Balanced"), qsTr("Strong"), qsTr("Custom")]
                    themeIndex: panel.theme.profile
                    onPicked: index => panel.theme.profile = index
                }
                ThemeSlider {
                    sliderObjectName: "bloomCoreSlider"
                    label: qsTr("Glow, core")
                    to: 0.8
                    themeValue: panel.theme.bloomCore
                    onMoved: value => panel.theme.bloomCore = value
                }
                ThemeSlider {
                    sliderObjectName: "bloomWideSlider"
                    label: qsTr("Glow, halo")
                    themeValue: panel.theme.bloomWide
                    onMoved: value => panel.theme.bloomWide = value
                }
                ThemeSlider {
                    sliderObjectName: "scanlineSlider"
                    label: qsTr("Scanlines")
                    to: 0.35
                    themeValue: panel.theme.scanline
                    onMoved: value => panel.theme.scanline = value
                }
                ThemeSlider {
                    sliderObjectName: "vignetteSlider"
                    label: qsTr("Vignette")
                    to: 0.45
                    themeValue: panel.theme.vignette
                    onMoved: value => panel.theme.vignette = value
                }
                ThemeSlider {
                    sliderObjectName: "persistenceSlider"
                    label: qsTr("Persistence")
                    themeValue: panel.theme.persistence
                    onMoved: value => panel.theme.persistence = value
                }
                ThemeSlider {
                    sliderObjectName: "deepFieldSlider"
                    label: qsTr("Ground depth")
                    themeValue: panel.theme.deepField
                    onMoved: value => panel.theme.deepField = value
                }
                ThemeSlider {
                    sliderObjectName: "textLiftSlider"
                    label: qsTr("Text brightness")
                    from: 1.0
                    to: 1.5
                    themeValue: panel.theme.textLift
                    onMoved: value => panel.theme.textLift = value
                }
                CheckBox {
                    objectName: "reducedMotionCheck"
                    text: qsTr("Reduced motion")
                    checked: panel.theme.reducedMotion
                    onToggled: panel.theme.reducedMotion = checked
                }

                SectionLabel {
                    text: qsTr("Material")
                }
                ThemeSlider {
                    sliderObjectName: "glassOpacitySlider"
                    label: qsTr("Window opacity")
                    from: 0.2
                    themeValue: panel.theme.glassOpacity
                    onMoved: value => panel.theme.glassOpacity = value
                }
                ThemeSlider {
                    sliderObjectName: "surfaceOpacitySlider"
                    label: qsTr("Surface blend")
                    from: 0.45
                    themeValue: panel.theme.surfaceOpacity
                    onMoved: value => panel.theme.surfaceOpacity = value
                }

                SectionLabel {
                    text: qsTr("Type and density")
                }
                ThemeCombo {
                    comboObjectName: "fontSourceBox"
                    label: qsTr("Typeface")
                    model: [qsTr("Bundled"), qsTr("System monospace"), qsTr("Named family")]
                    themeIndex: panel.theme.fontSource
                    onPicked: index => panel.theme.fontSource = index
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: panel.theme.fontSource === 2

                    FieldLabel {
                        text: qsTr("Family name")
                    }
                    TextField {
                        id: namedFamilyField

                        objectName: "namedFamilyField"
                        Accessible.name: qsTr("Family name")
                        Layout.fillWidth: true
                        text: panel.theme.namedFontFamily
                        color: panel.theme.text
                        selectByMouse: true
                        placeholderText: qsTr("Exact installed family")
                        onEditingFinished: panel.theme.namedFontFamily = text

                        background: Rectangle {
                            color: panel.theme.background
                            border.color: namedFamilyField.activeFocus ? panel.theme.focus : panel.theme.border
                            radius: 5
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    FieldLabel {
                        text: qsTr("Renders as")
                    }
                    Text {
                        objectName: "resolvedFamilyLabel"
                        Layout.fillWidth: true
                        text: panel.theme.contentFontFamily
                        color: panel.theme.textMuted
                        font.family: panel.theme.contentFontFamily
                        font.pixelSize: panel.theme.chromeFontPixelSize
                        elide: Text.ElideRight
                    }
                }
                ThemeCombo {
                    comboObjectName: "densityBox"
                    label: qsTr("Density")
                    model: [qsTr("Compact"), qsTr("Cozy"), qsTr("Comfortable")]
                    themeIndex: panel.theme.density
                    onPicked: index => panel.theme.density = index
                }
                ThemeSlider {
                    sliderObjectName: "uiScaleSlider"
                    label: qsTr("Scale")
                    from: 0.75
                    to: 2.0
                    themeValue: panel.theme.uiScale
                    onMoved: value => panel.theme.uiScale = value
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                objectName: "resetAppearanceButton"
                text: qsTr("Reset to defaults")
                onClicked: panel.theme.resetToDefaults()
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                objectName: "closeAppearanceButton"
                text: qsTr("Close")
                onClicked: panel.close()
            }
        }
    }
}

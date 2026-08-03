// The shell's shared push button: a vector icon, an optional label, and the
// semantic chrome roles from the theme. Toolbars, tab strips, action rows,
// and any future action surface instantiate this instead of restyling a
// plain Button, so every button responds to density, scale, contrast, and
// palette changes from one definition.
//
// Activation carries both input paths: pointer click and, once focused,
// the platform key activation Button already provides.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control

    required property var theme
    property string iconName: ""
    property int iconSize: Math.round(18 * control.theme.uiScale)

    implicitHeight: Math.max(32, control.iconSize + 12, control.theme.chromeFontPixelSize + 14)
    leftPadding: 11
    rightPadding: 11

    contentItem: RowLayout {
        spacing: control.text.length > 0 && control.iconName.length > 0 ? 7 : 0

        VectorIcon {
            visible: control.iconName.length > 0
            Layout.preferredWidth: control.iconSize
            Layout.preferredHeight: control.iconSize
            name: control.iconName
            ink: control.enabled ? control.theme.iconInk : control.theme.textFaint
            highContrast: control.theme.highContrast
        }

        Text {
            visible: control.text.length > 0
            text: control.text
            color: control.enabled ? control.theme.text : control.theme.textFaint
            font.family: control.theme.chromeFontFamily
            font.pixelSize: control.theme.chromeFontPixelSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        color: control.down ? control.theme.pressed : (control.hovered ? control.theme.hover : control.theme.panel)
        border.color: control.activeFocus ? control.theme.accent : control.theme.border
        radius: 5
    }
}

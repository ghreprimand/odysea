// The shell's shared line edit: theme-driven ink and a focus-accented
// field surface. The address bar, the filter field, and any future prompt
// share this one definition instead of duplicating the field chrome.
import QtQuick
import QtQuick.Controls

TextField {
    id: field

    required property var theme

    /// The surface behind the text. Fields on the panel strip sit on the
    /// window ground; fields on the ground sit on the panel color.
    property color fieldColor: field.theme.background

    color: field.theme.text
    selectByMouse: true

    background: Rectangle {
        color: field.fieldColor
        border.color: field.activeFocus ? field.theme.accent : field.theme.border
        radius: 5
    }
}

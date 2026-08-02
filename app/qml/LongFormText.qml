// Readable prose for dialogs, errors, settings explanations, and previews.
// The proportional face, bounded measure, and open leading are deliberately
// distinct from the dense fixed-width roles used for entries and paths.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

Text {
    id: prose

    required property var theme

    color: theme.longFormInk
    font.family: theme.longFormFontFamily
    font.pixelSize: theme.longFormFontPixelSize
    lineHeightMode: Text.ProportionalHeight
    lineHeight: theme.longFormLineHeight
    wrapMode: Text.WordWrap
    Layout.maximumWidth: theme.longFormMeasure
}

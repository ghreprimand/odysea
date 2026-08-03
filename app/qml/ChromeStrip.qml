// A horizontal chrome surface: the translucent panel material every shell
// strip (toolbar, tab row, action row, status row) sits on. The surface
// opacity is the theme's glass amount for chrome, so the strips thin
// together when the material changes.
import QtQuick

Rectangle {
    id: strip

    required property var theme

    /// The opaque role behind the strip; the surface opacity is applied here.
    property color surfaceColor: strip.theme.panel
    /// Whether the strip draws the hairline border. The action row sits
    /// borderless on the window ground; the other strips are outlined.
    property bool outlined: true

    color: Qt.alpha(strip.surfaceColor, strip.theme.surfaceOpacity)
    border.color: strip.outlined ? strip.theme.border : "transparent"
}

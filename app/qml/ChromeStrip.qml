// A horizontal chrome surface: the panel material every shell strip
// (toolbar, tab row, action row, status row) sits on. Surface blend moves
// the panel tone toward the window ground but resolves to an opaque color;
// chrome labels therefore keep the same measured contrast bed even when the
// window ground behind the strips is transparent.
import QtQuick

Rectangle {
    id: strip

    required property var theme

    /// The opaque role behind the strip; surface blend is applied to this
    /// color against the window-ground role before the strip is painted.
    property color surfaceColor: strip.theme.panel
    /// Whether the strip draws the hairline border. The action row sits
    /// borderless on the window ground; the other strips are outlined.
    property bool outlined: true

    readonly property color blendedSurfaceColor: Qt.rgba((strip.surfaceColor.r * strip.theme.surfaceOpacity) + (strip.theme.background.r * (1.0 - strip.theme.surfaceOpacity)), (strip.surfaceColor.g * strip.theme.surfaceOpacity) + (strip.theme.background.g * (1.0 - strip.theme.surfaceOpacity)), (strip.surfaceColor.b * strip.theme.surfaceOpacity) + (strip.theme.background.b * (1.0 - strip.theme.surfaceOpacity)), 1.0)

    color: strip.blendedSurfaceColor
    border.color: strip.outlined ? strip.theme.border : "transparent"
}

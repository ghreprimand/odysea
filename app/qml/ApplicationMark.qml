// OdySea's application mark: an O-shaped horizon carrying one wave.
// It stays on the shared vector path and the semantic icon ink, so accent
// customization cannot change the product identity or introduce another
// rendering path.
pragma ComponentBehavior: Bound
import QtQuick

VectorIcon {
    id: mark

    required property var theme

    name: "identity"
    ink: theme.iconInk
    highContrast: theme.highContrast
    outlineStrokeWidth: highContrast ? 2.35 : 1.8
    implicitWidth: 22 * theme.uiScale
    implicitHeight: 22 * theme.uiScale
}

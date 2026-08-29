// A bounded context marker that becomes an emitter only through the shell's
// existing bright-pass and phosphor-bloom chain. It owns no blur, shadow, or
// animation path: without that chain the frame remains a crisp semantic
// outline, and under the Off profile it has no emissive treatment at all.
import QtQuick

Rectangle {
    id: frame

    required property color accentColor

    /// The three semantic requests are deliberately not additive. A selected
    /// current item inside a focused surface may satisfy every request at
    /// once, yet it still produces one bounded emitter at the accent role.
    property bool activeTab: false
    property bool focusedSurface: false
    property bool selected: false

    /// The presentation layer sets this only when its existing emission path
    /// is available. Profiles with no bloom, high contrast, and software
    /// fallback leave it false; the outline remains an affordance, not glow.
    property bool glowEnabled: false

    property int activeBorderWidth: 1

    readonly property int requestedTreatmentCount: (frame.activeTab ? 1 : 0) + (frame.focusedSurface ? 1 : 0) + (frame.selected ? 1 : 0)
    readonly property bool requested: frame.requestedTreatmentCount > 0
    /// A logical OR expressed as a fixed one, not a state-count sum. This is
    /// the composition bound: overlap cannot increase source brightness.
    readonly property real compositionLevel: frame.requested ? 1.0 : 0.0
    readonly property bool glowEmitting: frame.glowEnabled && frame.compositionLevel > 0.0
    /// Reuse the resolved accent role. Its render-site contrast samples are
    /// the same ones the theme controller verifies for focus and selection.
    readonly property color emitterColor: frame.accentColor

    color: "transparent"
    // The ordinary focus border stays in its owning surface. This second,
    // transparent frame is the only added emitter, so the glow capability
    // can turn off without changing keyboard or pointer affordances.
    border.width: frame.glowEmitting ? frame.activeBorderWidth : 0
    border.color: frame.glowEmitting ? frame.emitterColor : "transparent"
}

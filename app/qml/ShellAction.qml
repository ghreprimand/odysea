// One user-facing action, declared once. The declaration carries the
// label, icon, enablement, shortcuts, and handler together, so every
// surface that renders the action — menu, toolbar, palette, shortcut —
// reads the same definition instead of restating any part of it.
import QtQml

QtObject {
    /// Stable dot-namespaced identity, e.g. "selection.copy". Surfaces and
    /// tests address actions by this id; it never changes with wording.
    required property string actionId

    /// The action's full label. Declared as a binding when the wording
    /// tracks live model state (for example a pane-count toggle).
    property string label: ""

    /// Optional target-dependent label: a function(context) returning the
    /// text for one concrete target, e.g. a destructive label stating the
    /// exact entry count. Null uses `label`.
    property var labelFor: null

    /// Compact wording for tight chrome such as toolbar buttons. Defaults
    /// to the full label.
    property string shortLabel: label

    /// Vector icon role, or empty for text-only rendering.
    property string iconName: ""

    /// Optional target-dependent icon: a function(context) returning the
    /// icon role for one concrete target. Null uses `iconName`.
    property var iconFor: null

    /// Destructive actions render after a separator, at the end of every
    /// menu, and their labels state the target count.
    property bool destructive: false

    /// Context kinds whose menus list this action: "entry", "selection",
    /// "canvas", "breadcrumb", "place", "device", "tab", "pane". An empty
    /// list keeps the action shortcut- and chrome-only.
    property var surfaces: []

    /// Live capability gate, declared as a reactive binding against the
    /// shell model (for example selection count and operation state).
    /// Chrome bound to the action updates the moment the model changes.
    property bool enabled: true

    /// Optional target-dependent gate: a function(context) returning
    /// whether this concrete target supports the action. Combined with
    /// `enabled`; both are revalidated when the action triggers.
    property var enabledFor: null

    /// Optional function(context) naming why the action is unavailable,
    /// surfaced as a tooltip on disabled items.
    property var disabledReasonFor: null

    /// Key sequences, declared with the action: a list of
    /// { sequence, argument } entries. The argument reaches the handler
    /// as context.argument, so one action covers a numbered family.
    property var shortcuts: []

    /// Toggle rendering for checkable chrome; `checked` is declared as a
    /// reactive binding against the state it mirrors.
    property bool checkable: false
    property bool checked: false

    /// The single handler: a function(context). Runs only after the
    /// registry revalidates enablement for that context.
    property var perform: null
}

// A chrome button bound to one registry action. Label, icon, enablement,
// checked state, accessible name, and the shortcut named in the tooltip
// all come from the declaration, so a toolbar can never disagree with
// the menus or the key sequences about what an action is or when it is
// available. Clicks route back through the registry, which revalidates
// enablement before the handler runs.
import QtQuick
import QtQuick.Controls

ShellButton {
    id: button

    required property ActionRegistry registry
    required property string actionId
    /// Optional context snapshot for target-bound chrome; chrome bound to
    /// the workspace at large triggers with the global context.
    property var actionContext: null
    /// Compact chrome renders the icon alone; the label stays available
    /// to accessibility through the action's declaration.
    property bool showLabel: true

    // Untyped on purpose: the enablement predicate is a `var` function
    // property, and calling one through a typed object trips qmllint's
    // missing-property analysis even though the property exists.
    readonly property var shellAction: button.registry.find(button.actionId)
    readonly property string sequenceHint: button.registry.primarySequence(button.shellAction)

    text: button.showLabel ? button.shellAction.shortLabel : ""
    iconName: button.registry.iconFor(button.shellAction, button.actionContext)
    enabled: button.shellAction.enabled && (button.shellAction.enabledFor === null || button.shellAction.enabledFor(button.actionContext) === true)
    checkable: button.shellAction.checkable
    Accessible.name: button.shellAction.shortLabel

    // A checkable Button breaks a plain `checked:` binding the first time
    // a click auto-toggles it. The Binding element keeps reasserting the
    // declared state, so the button always mirrors the action even after
    // pointer toggles.
    Binding on checked {
        value: button.shellAction.checked
    }
    ToolTip.visible: hovered
    ToolTip.text: button.registry.labelFor(button.shellAction, button.actionContext) + (button.sequenceHint.length > 0 ? " (" + button.sequenceHint + ")" : "")
    onClicked: button.registry.trigger(button.actionId, button.actionContext)
}

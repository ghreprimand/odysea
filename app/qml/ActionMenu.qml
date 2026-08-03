// The shared context menu: one instance per hosting surface,
// parameterized by an immutable context snapshot at open time. The menu
// renders whatever the registry declares for that context — routine
// actions first, then a separator and the destructive group — so a menu
// can never carry its own action list or enablement logic.
//
// Disabled actions stay visible and render disabled; menu key navigation
// skips them, and triggering revalidates enablement in the registry, so
// a stale menu can never fire a dead action.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Menu {
    id: menu

    required property ActionRegistry registry
    required property var theme

    /// The frozen context snapshot the visible items were built for.
    property var context: null
    /// Focus returns here when the menu closes; set per invocation.
    property var focusTarget: null
    /// The item the menu anchored to. A keyboard invocation passes the
    /// focused item, never a pointer position.
    property var anchorItem: null
    property point anchorPosition: Qt.point(0, 0)

    property var routineActions: []
    property var destructiveActions: []

    function openFor(context, anchor, position, focusItem) {
        menu.context = context;
        const declared = menu.registry.actionsFor(context);
        menu.routineActions = declared.filter(action => !action.destructive);
        menu.destructiveActions = declared.filter(action => action.destructive);
        menu.anchorItem = anchor;
        menu.anchorPosition = position;
        menu.focusTarget = focusItem === undefined ? null : focusItem;
        menu.popup(anchor, position);
    }

    onClosed: {
        if (menu.focusTarget !== null) {
            menu.focusTarget.forceActiveFocus();
        }
    }

    component ActionMenuItem: MenuItem {
        id: item

        required property var modelData
        readonly property string disabledReason: menu.registry.disabledReason(modelData, menu.context)

        objectName: "menuAction-" + modelData.actionId
        text: menu.registry.labelFor(modelData, menu.context)
        enabled: modelData.enabled && (modelData.enabledFor === null || modelData.enabledFor(menu.context) === true)
        Accessible.description: item.disabledReason
        onTriggered: menu.registry.trigger(item.modelData.actionId, menu.context)

        contentItem: Row {
            spacing: 10

            VectorIcon {
                width: 18
                height: 18
                anchors.verticalCenter: parent.verticalCenter
                name: menu.registry.iconFor(item.modelData, menu.context)
                ink: {
                    const base = item.modelData.destructive ? menu.theme.danger : menu.theme.iconInk;
                    return item.enabled ? base : Qt.alpha(base, 0.45);
                }
                highContrast: menu.theme.highContrast
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: item.text
                color: {
                    const base = item.modelData.destructive ? menu.theme.danger : menu.theme.text;
                    return item.enabled ? base : Qt.alpha(base, 0.45);
                }
            }
        }
    }

    Repeater {
        model: menu.routineActions

        ActionMenuItem {}
    }

    Repeater {
        model: menu.routineActions.length > 0 && menu.destructiveActions.length > 0 ? 1 : 0

        MenuSeparator {
            objectName: "destructiveSeparator"
        }
    }

    Repeater {
        model: menu.destructiveActions

        ActionMenuItem {}
    }
}

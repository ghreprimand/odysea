// The shared action registry: holds every ShellAction declaration and
// answers the questions every surface asks — which actions serve a
// context, whether one is enabled for a target, what it is called, and
// what happens when it triggers. Menus, toolbars, shortcuts, and the
// command palette all consume this one object, so enablement and wording
// cannot drift between surfaces.
//
// Contexts are immutable snapshots: plain frozen objects built by the
// factory functions below, keyed by a `kind` plus the target's identity
// (path, index). Capabilities are NOT snapshotted — enablement reads the
// live declarations and is revalidated when an action triggers, so a
// snapshot taken before a model change can never authorize a stale
// operation.
import QtQml

QtObject {
    id: registry

    default property list<ShellAction> actions

    // --- Context snapshot factories -----------------------------------

    function globalContext(argument) {
        return Object.freeze({
            "kind": "global",
            "argument": argument
        });
    }

    function entryContext(entryIndex, isDirectory, targetCount) {
        return Object.freeze({
            "kind": "entry",
            "entryIndex": entryIndex,
            "isDirectory": isDirectory,
            "targetCount": targetCount
        });
    }

    function selectionContext(targetCount) {
        return Object.freeze({
            "kind": "selection",
            "targetCount": targetCount
        });
    }

    function canvasContext(path) {
        return Object.freeze({
            "kind": "canvas",
            "path": path
        });
    }

    function breadcrumbContext(path) {
        return Object.freeze({
            "kind": "breadcrumb",
            "path": path
        });
    }

    function placeContext(path, label) {
        return Object.freeze({
            "kind": "place",
            "path": path,
            "label": label
        });
    }

    function deviceContext(path, label) {
        return Object.freeze({
            "kind": "device",
            "path": path,
            "label": label
        });
    }

    function tabContext(tabIndex) {
        return Object.freeze({
            "kind": "tab",
            "tabIndex": tabIndex
        });
    }

    function paneContext(paneIndex) {
        return Object.freeze({
            "kind": "pane",
            "paneIndex": paneIndex
        });
    }

    // --- Lookup and enablement ----------------------------------------

    function find(actionId) {
        for (let i = 0; i < registry.actions.length; ++i) {
            if (registry.actions[i].actionId === actionId) {
                return registry.actions[i];
            }
        }
        return null;
    }

    /// The actions a context's menu lists, in declaration order with the
    /// destructive group last. Disabled actions are included: a menu
    /// renders them visibly disabled instead of hiding them.
    function actionsFor(context) {
        const kind = context && context.kind !== undefined ? context.kind : "global";
        const routine = [];
        const destructive = [];
        for (let i = 0; i < registry.actions.length; ++i) {
            const action = registry.actions[i];
            if (action.surfaces.indexOf(kind) === -1) {
                continue;
            }
            if (action.destructive) {
                destructive.push(action);
            } else {
                routine.push(action);
            }
        }
        return routine.concat(destructive);
    }

    /// Enablement is a function of the action and the context only —
    /// never of which surface is asking.
    function isEnabled(action, context) {
        if (!action.enabled) {
            return false;
        }
        return action.enabledFor === null || action.enabledFor(context) === true;
    }

    function labelFor(action, context) {
        return action.labelFor !== null ? action.labelFor(context) : action.label;
    }

    function iconFor(action, context) {
        return action.iconFor !== null ? action.iconFor(context) : action.iconName;
    }

    function disabledReason(action, context) {
        if (action.disabledReasonFor === null || isEnabled(action, context)) {
            return "";
        }
        return action.disabledReasonFor(context);
    }

    function primarySequence(action) {
        return action.shortcuts.length > 0 ? action.shortcuts[0].sequence : "";
    }

    /// Triggers an action for a context. Enablement is revalidated here,
    /// against the live declarations, so a surface holding a stale
    /// snapshot or a key press on a contextually dead action is a safe
    /// no-op. Returns whether the handler ran.
    function trigger(actionId, context) {
        const action = find(actionId);
        if (action === null || action.perform === null) {
            return false;
        }
        const effective = context !== undefined && context !== null ? context : globalContext(undefined);
        if (!isEnabled(action, effective)) {
            return false;
        }
        action.perform(effective);
        return true;
    }

    // --- Shortcut table -----------------------------------------------

    /// Every declared key sequence with its action and argument, for the
    /// shell to instantiate. Declarations live with the actions; this is
    /// only the flattened view.
    function shortcutEntries() {
        const entries = [];
        for (let i = 0; i < registry.actions.length; ++i) {
            const action = registry.actions[i];
            for (let j = 0; j < action.shortcuts.length; ++j) {
                entries.push({
                    "actionId": action.actionId,
                    "sequence": action.shortcuts[j].sequence,
                    "argument": action.shortcuts[j].argument
                });
            }
        }
        return entries;
    }

    /// Key sequences declared by more than one action. A non-empty result
    /// is a declaration bug; the test suite asserts emptiness so a
    /// conflicting shortcut fails the build gates instead of silently
    /// shadowing another action at runtime.
    function shortcutConflicts() {
        const owners = ({});
        const conflicts = [];
        const entries = shortcutEntries();
        for (let i = 0; i < entries.length; ++i) {
            const key = entries[i].sequence.toLowerCase();
            if (owners[key] !== undefined && owners[key] !== entries[i].actionId) {
                conflicts.push(entries[i].sequence);
            } else {
                owners[key] = entries[i].actionId;
            }
        }
        return conflicts;
    }

    // --- Enumeration for the command palette --------------------------

    /// Every labeled action, filtered by a case-insensitive substring of
    /// the label or id, with its live enablement for the given context.
    /// The palette consumes this list; it never re-declares actions. The
    /// shortcut and disabled reason come from the declaration, so the
    /// palette cannot restate either.
    function paletteEntries(filterText, context) {
        const needle = filterText === undefined ? "" : filterText.toLowerCase();
        const effective = context !== undefined && context !== null ? context : globalContext(undefined);
        const entries = [];
        for (let i = 0; i < registry.actions.length; ++i) {
            const action = registry.actions[i];
            const label = labelFor(action, effective);
            if (label.length === 0) {
                continue;
            }
            if (needle.length > 0 && label.toLowerCase().indexOf(needle) === -1 && action.actionId.toLowerCase().indexOf(needle) === -1) {
                continue;
            }
            entries.push({
                "actionId": action.actionId,
                "label": label,
                "sequence": primarySequence(action),
                "enabled": isEnabled(action, effective),
                "reason": disabledReason(action, effective),
                "destructive": action.destructive,
                "icon": iconFor(action, effective)
            });
        }
        return entries;
    }
}

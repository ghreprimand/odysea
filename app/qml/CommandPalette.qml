// The command palette: every labeled action the registry declares and
// can reach from the palette's context, searchable by label or id,
// operable from the keyboard and the pointer. The list is enumerated
// from the registry at open and on every filter change — the palette
// holds no action list of its own, so a declaration added to the
// registry is reachable here with no palette-side change. The registry
// omits target-scoped declarations no palette context can ever enable,
// so every listed row is either enabled or disabled with the reason the
// declaration states.
//
// Disabled rows stay listed, render disabled with their stated reason,
// and read as disabled to assistive technology; keyboard navigation
// skips them. Activation — row
// click or Return — goes through registry.trigger(), which revalidates
// enablement, so a stale row can never fire a dead action. Each row shows
// the action's declared key sequence, read from the declaration rather
// than restated.
//
// Focus: the filter field owns focus while the palette is open; arrow
// keys steer the list from the field. Restoration on dismissal — by
// Escape, a press outside, or a successful activation — is the modal
// focus popup's own behavior; the tests pin it, and no palette-side
// restore code exists to drift from it.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: palette

    required property ActionRegistry registry
    required property var theme

    /// The frozen context snapshot activation targets. Normalized at open
    /// to the registry's global context when the caller passes none, so
    /// every declaration predicate receives a real context object.
    property var context: registry.globalContext(undefined)
    /// The rows currently listed, straight from the registry.
    property var entries: []

    objectName: "commandPalette"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(520, parent ? parent.width - 80 : 520)
    height: Math.min(420, parent ? parent.height - 120 : 420)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round(parent.height * 0.12) : 0
    padding: 10

    background: Rectangle {
        color: palette.theme.panel
        border.color: palette.theme.border
        radius: 8
    }

    function openFor(context) {
        palette.context = context === undefined || context === null ? palette.registry.globalContext(undefined) : context;
        filterField.text = "";
        palette.open();
        palette.refresh();
        filterField.forceActiveFocus();
    }

    /// Re-enumerates from the registry for the current filter text and
    /// moves the highlight to the first enabled row.
    function refresh() {
        palette.entries = palette.registry.paletteEntries(filterField.text, palette.context);
        entryList.currentIndex = firstEnabledIndex();
    }

    /// Live enablement for a listed row: reads the declaration through
    /// the registry rather than the snapshot the row was enumerated with,
    /// so navigation agrees with what the row currently renders.
    function rowEnabled(index) {
        const action = palette.registry.find(palette.entries[index].actionId);
        return action !== null && palette.registry.isEnabled(action, palette.context);
    }

    function firstEnabledIndex() {
        for (let i = 0; i < palette.entries.length; ++i) {
            if (rowEnabled(i)) {
                return i;
            }
        }
        return -1;
    }

    /// Moves the highlight by direction, skipping rows whose action is
    /// disabled; stays put when no enabled row exists in that direction.
    function moveHighlight(direction) {
        let index = entryList.currentIndex;
        while (true) {
            index += direction;
            if (index < 0 || index >= palette.entries.length) {
                return;
            }
            if (rowEnabled(index)) {
                entryList.currentIndex = index;
                entryList.positionViewAtIndex(index, ListView.Contain);
                return;
            }
        }
    }

    /// The single activation path for every input route. Triggering goes
    /// through the registry, which revalidates enablement against the
    /// live declarations; the palette closes only when the handler ran.
    function activate(actionId) {
        if (palette.registry.trigger(actionId, palette.context)) {
            palette.close();
        }
    }

    function activateCurrent() {
        if (entryList.currentIndex >= 0 && entryList.currentIndex < palette.entries.length) {
            palette.activate(palette.entries[entryList.currentIndex].actionId);
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        ShellTextField {
            id: filterField

            objectName: "paletteFilterField"
            Layout.fillWidth: true
            theme: palette.theme
            fieldColor: palette.theme.background
            placeholderText: qsTr("Type a command")
            Accessible.name: qsTr("Command search")
            onTextChanged: palette.refresh()
            Keys.onDownPressed: palette.moveHighlight(1)
            Keys.onUpPressed: palette.moveHighlight(-1)
            onAccepted: palette.activateCurrent()
        }

        ListView {
            id: entryList

            objectName: "paletteList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: palette.entries
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: row

                required property int index
                required property var modelData
                // Live enablement: the lookup binds to the declaration's
                // reactive `enabled`, so a model change re-renders open
                // rows exactly as it does open menu items.
                readonly property var action: palette.registry.find(modelData.actionId)
                readonly property bool actionEnabled: row.action !== null && row.action.enabled && (row.action.enabledFor === null || row.action.enabledFor(palette.context) === true)
                readonly property string reason: row.action !== null ? palette.registry.disabledReason(row.action, palette.context) : ""
                readonly property bool highlighted: entryList.currentIndex === row.index

                objectName: "paletteEntry-" + modelData.actionId
                width: entryList.width
                height: palette.theme.rowHeight + (reasonText.visible ? reasonText.height : 0)
                color: row.highlighted ? palette.theme.selectionBed : "transparent"
                radius: 4
                // Disabled rows are disabled at the item level, so
                // assistive technology reads them as unavailable instead
                // of actionable. The gate is the same live enablement
                // that drives the row's rendering — one predicate, no
                // second gate to drift — and the registry still
                // revalidates at trigger time.
                enabled: row.actionEnabled

                Accessible.role: Accessible.ListItem
                Accessible.name: row.modelData.label
                Accessible.description: row.reason

                MouseArea {
                    anchors.fill: parent
                    // Activation routes through activate(), where the
                    // registry revalidates enablement against the live
                    // declarations before anything runs.
                    onClicked: palette.activate(row.modelData.actionId)
                }

                RowLayout {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    height: palette.theme.rowHeight
                    spacing: 8

                    VectorIcon {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        name: row.modelData.icon
                        highContrast: palette.theme.highContrast
                        ink: {
                            const base = row.modelData.destructive ? palette.theme.danger : palette.theme.iconInk;
                            return row.actionEnabled ? base : Qt.alpha(base, 0.45);
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.label
                        elide: Text.ElideRight
                        font.family: palette.theme.chromeFontFamily
                        font.pixelSize: palette.theme.chromeFontPixelSize
                        color: {
                            const base = row.modelData.destructive ? palette.theme.danger : palette.theme.text;
                            return row.actionEnabled ? base : Qt.alpha(base, 0.45);
                        }
                    }

                    Text {
                        objectName: "paletteShortcut-" + row.modelData.actionId
                        text: row.modelData.sequence
                        font.family: palette.theme.captionFontFamily
                        font.pixelSize: palette.theme.captionFontPixelSize
                        color: palette.theme.textMuted
                    }
                }

                Text {
                    id: reasonText

                    objectName: "paletteReason-" + row.modelData.actionId
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.leftMargin: 32
                    anchors.rightMargin: 8
                    visible: !row.actionEnabled && row.reason.length > 0
                    text: row.reason
                    elide: Text.ElideRight
                    font.family: palette.theme.captionFontFamily
                    font.pixelSize: palette.theme.captionFontPixelSize
                    color: palette.theme.textMuted
                }
            }
        }
    }
}

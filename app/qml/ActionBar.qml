// The action row: the folder filter, sort order, hidden-file toggle, and
// the selection operations. The operation buttons render from the shared
// action registry, so their enablement is the same live declaration the
// context menus and shortcuts evaluate — a surface here can never gate
// differently from the rest of the shell.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ChromeStrip {
    id: bar

    required property var shellModel
    required property ActionRegistry registry

    /// Below this width the operation buttons drop their labels and the
    /// filter field narrows, keeping every control reachable down to the
    /// window's minimum width. The bound is the measured implicit width of
    /// the fully labeled row at 1x, scaled with the interface; the labels
    /// stay available through accessible names and tooltips.
    readonly property bool compact: bar.width < 940 * bar.theme.uiScale

    /// Keyboard entry point for the filter field (Ctrl+F on the shell).
    function focusFilterField() {
        filterField.forceActiveFocus();
        filterField.selectAll();
    }

    surfaceColor: bar.theme.background
    outlined: false
    implicitHeight: Math.max(44, bar.theme.chromeFontPixelSize + 20)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        ShellTextField {
            id: filterField

            objectName: "filterField"
            Layout.preferredWidth: bar.compact ? 170 : 260
            theme: bar.theme
            fieldColor: bar.theme.panel
            placeholderText: qsTr("Filter this folder (Ctrl+F)")
            onTextEdited: bar.shellModel.filterText = text
        }

        ComboBox {
            id: sortBox

            objectName: "sortModeBox"
            Layout.preferredWidth: 130
            model: [qsTr("Name"), qsTr("Size"), qsTr("Type")]
            currentIndex: bar.shellModel.sortMode
            onActivated: index => bar.shellModel.sortMode = index
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Sort order (Ctrl+Shift+S)")
        }

        CheckBox {
            objectName: "hiddenToggle"
            text: qsTr("Hidden")
            checked: bar.shellModel.showHidden
            onToggled: bar.registry.trigger("view.toggleHidden", null)
        }

        ActionButton {
            objectName: "selectAllButton"
            theme: bar.theme
            showLabel: !bar.compact
            registry: bar.registry
            actionId: "selection.all"
        }

        Item {
            Layout.fillWidth: true
        }

        ActionButton {
            objectName: "copyButton"
            theme: bar.theme
            showLabel: !bar.compact
            registry: bar.registry
            actionId: "selection.copy"
        }
        ActionButton {
            objectName: "moveButton"
            theme: bar.theme
            showLabel: !bar.compact
            registry: bar.registry
            actionId: "selection.move"
        }
        ActionButton {
            objectName: "renameButton"
            theme: bar.theme
            showLabel: !bar.compact
            registry: bar.registry
            actionId: "selection.rename"
        }
        ActionButton {
            objectName: "trashButton"
            theme: bar.theme
            showLabel: !bar.compact
            registry: bar.registry
            actionId: "selection.trash"
        }
    }
}

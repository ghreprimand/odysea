// The action row: the folder filter, sort order, hidden-file toggle, and
// the selection operations. Buttons gate themselves on the live selection
// and operation state, so a disabled surface always reflects the model.
// Every pointer action names its keyboard sequence in its tooltip; the
// sequences themselves live on the shell's shortcut table.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ChromeStrip {
    id: bar

    required property var shellModel

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
            Layout.preferredWidth: 260
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
            onToggled: bar.shellModel.showHidden = checked
        }

        ShellButton {
            objectName: "selectAllButton"
            theme: bar.theme
            iconName: "select-all"
            text: qsTr("Select all")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Select all entries (Ctrl+A)")
            onClicked: bar.shellModel.selectAll()
        }

        Item {
            Layout.fillWidth: true
        }

        ShellButton {
            objectName: "copyButton"
            theme: bar.theme
            iconName: "copy"
            text: qsTr("Copy")
            enabled: bar.shellModel.selectedCount > 0 && !bar.shellModel.operationBusy
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Copy selection (Ctrl+C)")
            onClicked: bar.shellModel.requestCopy()
        }
        ShellButton {
            objectName: "moveButton"
            theme: bar.theme
            iconName: "move"
            text: qsTr("Move")
            enabled: bar.shellModel.selectedCount > 0 && !bar.shellModel.operationBusy
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Move selection (Ctrl+X)")
            onClicked: bar.shellModel.requestMove()
        }
        ShellButton {
            objectName: "renameButton"
            theme: bar.theme
            iconName: "rename"
            text: qsTr("Rename")
            enabled: bar.shellModel.selectedCount === 1 && !bar.shellModel.operationBusy
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Rename selection (F2)")
            onClicked: bar.shellModel.requestRename()
        }
        ShellButton {
            objectName: "trashButton"
            theme: bar.theme
            iconName: "trash"
            text: qsTr("Trash")
            enabled: bar.shellModel.selectedCount > 0 && !bar.shellModel.operationBusy
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Move selection to trash (Delete)")
            onClicked: bar.shellModel.requestTrash()
        }
    }
}

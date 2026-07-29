pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Menu {
    id: menu

    required property var shellModel
    required property var focusTarget
    property int entryIndex: -1
    property bool entryIsDirectory: false

    function openFor(index, isDirectory) {
        entryIndex = index;
        entryIsDirectory = isDirectory;
        popup();
    }

    onClosed: focusTarget.forceActiveFocus()

    MenuItem {
        objectName: "contextOpenAction-" + menu.entryIndex
        text: menu.entryIsDirectory ? qsTr("Open folder") : qsTr("Open")
        enabled: menu.entryIndex >= 0
        onTriggered: menu.shellModel.activate(menu.entryIndex)
    }
    MenuSeparator {}
    MenuItem {
        objectName: "contextCopyAction-" + menu.entryIndex
        text: qsTr("Copy")
        enabled: menu.shellModel.selectedCount > 0 && !menu.shellModel.operationBusy
        onTriggered: menu.shellModel.requestCopy()
    }
    MenuItem {
        objectName: "contextMoveAction-" + menu.entryIndex
        text: qsTr("Move")
        enabled: menu.shellModel.selectedCount > 0 && !menu.shellModel.operationBusy
        onTriggered: menu.shellModel.requestMove()
    }
    MenuItem {
        objectName: "contextRenameAction-" + menu.entryIndex
        text: qsTr("Rename")
        enabled: menu.shellModel.selectedCount === 1 && !menu.shellModel.operationBusy
        onTriggered: menu.shellModel.requestRename()
    }
    MenuItem {
        objectName: "contextTrashAction-" + menu.entryIndex
        text: qsTr("Move to Trash")
        enabled: menu.shellModel.selectedCount > 0 && !menu.shellModel.operationBusy
        onTriggered: menu.shellModel.requestTrash()
    }
}

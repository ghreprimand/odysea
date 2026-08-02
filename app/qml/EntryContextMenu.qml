pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

Menu {
    id: menu

    required property var shellModel
    required property var focusTarget
    required property color iconInk
    required property color textInk
    required property bool highContrast
    property int entryIndex: -1
    property bool entryIsDirectory: false

    function openFor(index, isDirectory) {
        entryIndex = index;
        entryIsDirectory = isDirectory;
        popup();
    }

    onClosed: focusTarget.forceActiveFocus()

    component IconMenuItem: MenuItem {
        id: action

        required property string iconName

        contentItem: Row {
            spacing: 10

            VectorIcon {
                width: 18
                height: 18
                anchors.verticalCenter: parent.verticalCenter
                name: action.iconName
                ink: action.enabled ? menu.iconInk : Qt.alpha(menu.iconInk, 0.45)
                highContrast: menu.highContrast
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: action.text
                color: action.enabled ? menu.textInk : Qt.alpha(menu.textInk, 0.45)
            }
        }
    }

    IconMenuItem {
        objectName: "contextOpenAction-" + menu.entryIndex
        text: menu.entryIsDirectory ? qsTr("Open folder") : qsTr("Open")
        iconName: menu.entryIsDirectory ? "folder" : "open"
        enabled: menu.entryIndex >= 0
        onTriggered: menu.shellModel.activate(menu.entryIndex)
    }
    MenuSeparator {}
    IconMenuItem {
        objectName: "contextCopyAction-" + menu.entryIndex
        text: qsTr("Copy")
        iconName: "copy"
        enabled: menu.shellModel.selectedCount > 0 && !menu.shellModel.operationBusy
        onTriggered: menu.shellModel.requestCopy()
    }
    IconMenuItem {
        objectName: "contextMoveAction-" + menu.entryIndex
        text: qsTr("Move")
        iconName: "move"
        enabled: menu.shellModel.selectedCount > 0 && !menu.shellModel.operationBusy
        onTriggered: menu.shellModel.requestMove()
    }
    IconMenuItem {
        objectName: "contextRenameAction-" + menu.entryIndex
        text: qsTr("Rename")
        iconName: "rename"
        enabled: menu.shellModel.selectedCount === 1 && !menu.shellModel.operationBusy
        onTriggered: menu.shellModel.requestRename()
    }
    IconMenuItem {
        objectName: "contextTrashAction-" + menu.entryIndex
        text: qsTr("Move to Trash")
        iconName: "trash"
        enabled: menu.shellModel.selectedCount > 0 && !menu.shellModel.operationBusy
        onTriggered: menu.shellModel.requestTrash()
    }
}

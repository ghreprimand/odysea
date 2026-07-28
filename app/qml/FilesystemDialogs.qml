import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: dialogs

    required property var shellModel
    required property color backgroundColor
    required property color panelColor
    required property color borderColor
    required property color primaryTextColor
    required property color secondaryTextColor
    required property color accentColor

    property string pendingOperation: ""
    property var pendingPaths: []

    function openOperation(operation, paths) {
        pendingOperation = operation;
        pendingPaths = paths;
        if (operation === "copy" || operation === "move") {
            destinationField.text = shellModel.path;
            transferDialog.open();
        } else if (operation === "rename") {
            renameField.text = paths.length > 0 ? paths[0].substring(paths[0].lastIndexOf("/") + 1) : "";
            renameDialog.open();
        } else if (operation === "trash") {
            trashDialog.open();
        }
    }

    function conflictMode(combo) {
        return combo.currentIndex;
    }

    Connections {
        target: dialogs.shellModel

        function onFilesystemOperationRequested(operation, paths) {
            dialogs.openOperation(operation, paths);
        }

        function onOperationErrorStringChanged() {
            if (dialogs.shellModel.operationErrorString.length > 0) {
                errorDialog.open();
            }
        }
    }

    Dialog {
        id: transferDialog

        objectName: "transferDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 460
        modal: true
        title: dialogs.pendingOperation === "copy" ? qsTr("Copy selected entries") : qsTr("Move selected entries")
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            destinationField.forceActiveFocus();
            destinationField.selectAll();
        }

        background: Rectangle {
            color: dialogs.panelColor
            border.color: dialogs.borderColor
            radius: 8
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: qsTr("Destination folder")
                color: dialogs.primaryTextColor
            }

            TextField {
                id: destinationField

                objectName: "operationDestinationField"
                Layout.fillWidth: true
                color: dialogs.primaryTextColor
                selectByMouse: true
                onAccepted: {
                    if (text.trim().length > 0) {
                        transferDialog.accept();
                    }
                }

                background: Rectangle {
                    color: dialogs.backgroundColor
                    border.color: destinationField.activeFocus ? dialogs.accentColor : dialogs.borderColor
                    radius: 5
                }
            }

            Label {
                text: qsTr("If a destination already exists")
                color: dialogs.secondaryTextColor
            }

            ComboBox {
                id: transferConflict

                objectName: "operationConflictMode"
                Layout.fillWidth: true
                model: [qsTr("Stop and report an error"), qsTr("Replace the destination"), qsTr("Keep both with a numbered name")]
            }
        }

        footer: DialogButtonBox {
            background: Rectangle {
                color: dialogs.panelColor
            }

            Button {
                objectName: "transferCancelButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }

            Button {
                objectName: "transferConfirmButton"
                text: dialogs.pendingOperation === "copy" ? qsTr("Copy") : qsTr("Move")
                enabled: destinationField.text.trim().length > 0 && !dialogs.shellModel.operationBusy
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }

        onAccepted: {
            if (dialogs.pendingOperation === "copy") {
                dialogs.shellModel.performCopy(destinationField.text, dialogs.conflictMode(transferConflict));
            } else {
                dialogs.shellModel.performMove(destinationField.text, dialogs.conflictMode(transferConflict));
            }
        }
    }

    Dialog {
        id: renameDialog

        objectName: "renameDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        title: qsTr("Rename selected entry")
        closePolicy: Popup.CloseOnEscape
        onOpened: {
            renameField.forceActiveFocus();
            renameField.selectAll();
        }

        background: Rectangle {
            color: dialogs.panelColor
            border.color: dialogs.borderColor
            radius: 8
        }

        contentItem: ColumnLayout {
            spacing: 10

            Label {
                text: qsTr("New name")
                color: dialogs.primaryTextColor
            }

            TextField {
                id: renameField

                objectName: "renameField"
                Layout.fillWidth: true
                color: dialogs.primaryTextColor
                selectByMouse: true
                onAccepted: {
                    if (text.length > 0) {
                        renameDialog.accept();
                    }
                }

                background: Rectangle {
                    color: dialogs.backgroundColor
                    border.color: renameField.activeFocus ? dialogs.accentColor : dialogs.borderColor
                    radius: 5
                }
            }

            Label {
                text: qsTr("If the new name already exists")
                color: dialogs.secondaryTextColor
            }

            ComboBox {
                id: renameConflict

                objectName: "renameConflictMode"
                Layout.fillWidth: true
                model: [qsTr("Stop and report an error"), qsTr("Replace the destination"), qsTr("Keep both with a numbered name")]
            }
        }

        footer: DialogButtonBox {
            background: Rectangle {
                color: dialogs.panelColor
            }

            Button {
                objectName: "renameCancelButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }

            Button {
                objectName: "renameConfirmButton"
                text: qsTr("Rename")
                enabled: renameField.text.length > 0 && !dialogs.shellModel.operationBusy
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }

        onAccepted: dialogs.shellModel.performRename(renameField.text, dialogs.conflictMode(renameConflict))
    }

    Dialog {
        id: trashDialog

        objectName: "trashDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        modal: true
        title: qsTr("Move selected entries to Trash?")
        closePolicy: Popup.CloseOnEscape
        onOpened: trashConfirmButton.forceActiveFocus()

        background: Rectangle {
            color: dialogs.panelColor
            border.color: dialogs.borderColor
            radius: 8
        }

        contentItem: Label {
            text: qsTr("%1 selected item(s) will remain recoverable in Trash.").arg(dialogs.pendingPaths.length)
            color: dialogs.primaryTextColor
            wrapMode: Text.Wrap
        }

        footer: DialogButtonBox {
            background: Rectangle {
                color: dialogs.panelColor
            }

            Button {
                objectName: "trashCancelButton"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }

            Button {
                id: trashConfirmButton

                objectName: "trashConfirmButton"
                text: qsTr("Move to Trash")
                enabled: !dialogs.shellModel.operationBusy
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
        }

        onAccepted: dialogs.shellModel.performTrash()
    }

    Dialog {
        id: errorDialog

        objectName: "operationErrorDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 460
        modal: true
        title: qsTr("Filesystem operation failed")
        standardButtons: Dialog.Ok

        background: Rectangle {
            color: dialogs.panelColor
            border.color: "#ff8f7a"
            radius: 8
        }

        contentItem: Label {
            text: dialogs.shellModel.operationErrorString
            color: dialogs.primaryTextColor
            wrapMode: Text.Wrap
        }
    }
}

// Calm path orientation and the summoned navigation workspace.
//
// Normal browsing presents breadcrumbs plus two quiet affordances. Ctrl+L or
// the Location button replaces that strip with a full path editor; Ctrl+Shift+L
// or the Places button opens direct Places and recent-destination jumps. A
// hidden dirty editor retains its draft and advertises that fact when calm, so
// switching modes never discards typed input silently.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: navigator

    objectName: "pathNavigator"

    required property var shellModel
    required property var navigationController
    required property ActionRegistry registry
    required property var settings
    required property var theme

    property bool editing: false
    property bool retainedDraft: false
    property string draftText: ""
    readonly property var completionResult: editing ? shellModel.navigationCompletion(draftText) : ({
            "completed": draftText,
            "suffix": "",
            "candidates": []
        })
    readonly property string completionSuffix: completionResult.suffix ?? ""

    implicitHeight: Math.max(calmRow.implicitHeight, editorRow.implicitHeight)

    function beginEditing() {
        const resume = retainedDraft;
        if (!resume) {
            draftText = shellModel.path;
        }
        editing = true;
        Qt.callLater(function () {
            addressField.forceActiveFocus();
            if (!resume) {
                addressField.selectAll();
            } else {
                addressField.cursorPosition = addressField.length;
            }
        });
    }

    function hideEditorRetainingDraft() {
        retainedDraft = draftText !== shellModel.path;
        editing = false;
        navigationController.focusCurrentView();
    }

    function acceptCompletion() {
        const completed = completionResult.completed ?? draftText;
        if (completed !== draftText) {
            draftText = completed;
            addressField.cursorPosition = addressField.length;
        }
    }

    function commitPath() {
        if (!shellModel.navigateFromInput(draftText)) {
            addressField.forceActiveFocus();
            return false;
        }
        retainedDraft = false;
        editing = false;
        draftText = shellModel.path;
        settings.recordRecentDestination(shellModel.path);
        navigationController.clearTypeAhead();
        navigationController.focusCurrentView();
        return true;
    }

    function activatePath(path, kind, label) {
        const context = kind === "place" ? registry.placeContext(path, label) : registry.breadcrumbContext(path);
        if (registry.trigger("location.open", context)) {
            settings.recordRecentDestination(path);
            locationsPopup.close();
            navigationController.clearTypeAhead();
            navigationController.focusCurrentView();
        }
    }

    function openLocations() {
        locationsPopup.open();
        Qt.callLater(function () {
            const first = placeRepeater.itemAt(0);
            if (first !== null) {
                first.pathButton.forceActiveFocus();
            } else {
                addCurrentPlaceButton.forceActiveFocus();
            }
        });
    }

    function requestPlaceContext(path, label, anchor, position) {
        pathActionMenu.openFor(registry.placeContext(path, label), anchor, position, anchor);
    }

    function requestPathContext(path, anchor, position) {
        pathActionMenu.openFor(registry.breadcrumbContext(path), anchor, position, anchor);
    }

    Shortcut {
        sequence: "Escape"
        enabled: navigator.editing
        context: Qt.WindowShortcut
        onActivated: navigator.hideEditorRetainingDraft()
    }

    ChromeStrip {
        anchors.fill: parent
        theme: navigator.theme
    }

    RowLayout {
        id: calmRow

        objectName: "calmPathRow"
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 6
        visible: !navigator.editing

        BreadcrumbBar {
            id: breadcrumbs

            Layout.fillWidth: true
            Layout.fillHeight: true
            shellModel: navigator.shellModel
            navigationController: navigator.navigationController
            registry: navigator.registry
            backgroundColor: Qt.alpha(navigator.theme.panel, navigator.theme.surfaceOpacity)
            borderColor: navigator.theme.border
            primaryTextColor: navigator.theme.text
            accentColor: navigator.theme.accent
            hoverColor: navigator.theme.hover
            pressedColor: navigator.theme.pressed
            pathFontFamily: navigator.theme.pathFontFamily
            pathFontPixelSize: navigator.theme.pathFontPixelSize
            onContextActionsRequested: (surfaceKind, targetPath, targetLabel, anchor, position) => {
                if (surfaceKind === "place") {
                    navigator.requestPlaceContext(targetPath, targetLabel, anchor, position);
                } else {
                    navigator.requestPathContext(targetPath, anchor, position);
                }
            }
        }

        Text {
            id: retainedIndicator

            objectName: "retainedPathIndicator"
            visible: navigator.retainedDraft
            text: qsTr("Draft retained")
            color: navigator.theme.warning
            font.family: navigator.theme.captionFontFamily
            font.pixelSize: navigator.theme.captionFontPixelSize
        }

        ActionButton {
            id: editLocationButton

            objectName: "editLocationButton"
            theme: navigator.theme
            registry: navigator.registry
            actionId: "focus.address"
        }

        ActionButton {
            id: locationsButton

            objectName: "locationsButton"
            theme: navigator.theme
            registry: navigator.registry
            actionId: "focus.locations"
        }
    }

    RowLayout {
        id: editorRow

        objectName: "pathEditorRow"
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6
        visible: navigator.editing

        ShellTextField {
            id: addressField

            objectName: "pathEntryField"
            Layout.fillWidth: true
            theme: navigator.theme
            text: navigator.draftText
            font.family: navigator.theme.pathFontFamily
            font.pixelSize: navigator.theme.pathFontPixelSize
            placeholderText: qsTr("Absolute path or ~/path")
            Accessible.name: qsTr("Location")
            Accessible.description: navigator.completionSuffix.length > 0 ? qsTr("Completion %1").arg(navigator.completionSuffix) : ""
            onTextEdited: navigator.draftText = text
            onAccepted: navigator.commitPath()
            Keys.priority: Keys.BeforeItem
            Keys.onTabPressed: navigator.acceptCompletion()
            Keys.onEscapePressed: navigator.hideEditorRetainingDraft()
        }

        ShellButton {
            id: completionButton

            objectName: "pathCompletionButton"
            visible: navigator.completionSuffix.length > 0
            theme: navigator.theme
            text: navigator.completionSuffix
            Accessible.name: qsTr("Complete path with %1").arg(navigator.completionSuffix)
            onClicked: navigator.acceptCompletion()
        }

        ShellButton {
            id: commitPathButton

            objectName: "commitPathButton"
            theme: navigator.theme
            text: qsTr("Go")
            iconName: "forward"
            Accessible.name: qsTr("Open typed location")
            onClicked: navigator.commitPath()
        }

        ShellButton {
            id: hidePathEditorButton

            objectName: "hidePathEditorButton"
            theme: navigator.theme
            text: qsTr("Hide")
            Accessible.name: qsTr("Hide editor and retain its draft")
            onClicked: navigator.hideEditorRetainingDraft()
        }
    }

    Connections {
        target: navigator.shellModel

        function onPathChanged() {
            if (!navigator.editing && !navigator.retainedDraft) {
                navigator.draftText = navigator.shellModel.path;
            }
        }
    }

    Popup {
        id: locationsPopup

        objectName: "locationsPopup"
        x: Math.max(0, navigator.width - width - 8)
        y: navigator.height
        width: Math.min(560, Math.max(360, navigator.width - 16))
        height: Math.min(440, locationsColumn.implicitHeight + 24)
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: navigator.navigationController.focusCurrentView()

        background: Rectangle {
            color: navigator.theme.panel
            border.color: navigator.theme.border
            radius: 6
        }

        contentItem: Flickable {
            contentWidth: width
            contentHeight: locationsColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: locationsColumn

                width: parent.width
                spacing: 6

                Text {
                    text: qsTr("Places")
                    color: navigator.theme.text
                    font.family: navigator.theme.chromeFontFamily
                    font.pixelSize: navigator.theme.chromeFontPixelSize
                    font.bold: true
                }

                Repeater {
                    id: placeRepeater

                    model: navigator.settings.places

                    delegate: RowLayout {
                        id: placeRow

                        required property int index
                        required property var modelData
                        property alias pathButton: placePathButton
                        readonly property string placePath: modelData.path

                        Layout.fillWidth: true
                        spacing: 4

                        ShellButton {
                            id: placePathButton

                            objectName: "placeButton-" + placeRow.index
                            Layout.fillWidth: true
                            theme: navigator.theme
                            text: placeRow.modelData.label
                            Accessible.name: qsTr("Open Place %1").arg(placeRow.modelData.label)
                            Accessible.description: placeRow.placePath
                            onClicked: navigator.activatePath(placeRow.placePath, "place", placeRow.modelData.label)
                            Keys.onDeletePressed: navigator.registry.trigger("place.remove", navigator.registry.placeContext(placeRow.placePath, placeRow.modelData.label))
                            Keys.onPressed: event => {
                                if ((event.modifiers & Qt.AltModifier) !== 0 && event.key === Qt.Key_Up) {
                                    navigator.registry.trigger("place.moveUp", navigator.registry.placeContext(placeRow.placePath, placeRow.modelData.label));
                                    event.accepted = true;
                                } else if ((event.modifiers & Qt.AltModifier) !== 0 && event.key === Qt.Key_Down) {
                                    navigator.registry.trigger("place.moveDown", navigator.registry.placeContext(placeRow.placePath, placeRow.modelData.label));
                                    event.accepted = true;
                                } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier) !== 0)) {
                                    navigator.requestPlaceContext(placeRow.placePath, placeRow.modelData.label, placePathButton, Qt.point(placePathButton.width / 2, placePathButton.height));
                                    event.accepted = true;
                                }
                            }

                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: (eventPoint, button) => navigator.requestPlaceContext(placeRow.placePath, placeRow.modelData.label, placePathButton, eventPoint.position)
                            }
                        }

                        ActionButton {
                            objectName: "movePlaceUpButton-" + placeRow.index
                            theme: navigator.theme
                            registry: navigator.registry
                            actionId: "place.moveUp"
                            actionContext: navigator.registry.placeContext(placeRow.placePath, placeRow.modelData.label)
                            showLabel: false
                        }

                        ActionButton {
                            objectName: "movePlaceDownButton-" + placeRow.index
                            theme: navigator.theme
                            registry: navigator.registry
                            actionId: "place.moveDown"
                            actionContext: navigator.registry.placeContext(placeRow.placePath, placeRow.modelData.label)
                        }

                        ActionButton {
                            objectName: "removePlaceButton-" + placeRow.index
                            theme: navigator.theme
                            registry: navigator.registry
                            actionId: "place.remove"
                            actionContext: navigator.registry.placeContext(placeRow.placePath, placeRow.modelData.label)
                        }
                    }
                }

                ActionButton {
                    id: addCurrentPlaceButton

                    objectName: "addCurrentPlaceButton"
                    Layout.fillWidth: true
                    theme: navigator.theme
                    registry: navigator.registry
                    actionId: "place.addCurrent"
                    actionContext: navigator.registry.breadcrumbContext(navigator.shellModel.path)
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: navigator.theme.border
                }

                Text {
                    text: qsTr("Recent destinations")
                    color: navigator.theme.text
                    font.family: navigator.theme.chromeFontFamily
                    font.pixelSize: navigator.theme.chromeFontPixelSize
                    font.bold: true
                }

                Text {
                    visible: navigator.settings.recentDestinations.length === 0
                    text: qsTr("No recent destinations")
                    color: navigator.theme.textMuted
                    font.family: navigator.theme.captionFontFamily
                    font.pixelSize: navigator.theme.captionFontPixelSize
                }

                Repeater {
                    id: recentRepeater

                    model: navigator.settings.recentDestinations

                    delegate: ShellButton {
                        id: recentButton

                        required property int index
                        required property string modelData

                        objectName: "recentButton-" + index
                        Layout.fillWidth: true
                        theme: navigator.theme
                        text: modelData
                        Accessible.name: qsTr("Open recent destination %1").arg(modelData)
                        onClicked: navigator.activatePath(modelData, "breadcrumb", modelData)
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier) !== 0)) {
                                navigator.requestPathContext(modelData, recentButton, Qt.point(recentButton.width / 2, recentButton.height));
                                event.accepted = true;
                            }
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: (eventPoint, button) => navigator.requestPathContext(recentButton.modelData, recentButton, eventPoint.position)
                        }
                    }
                }

                ActionButton {
                    id: clearRecentsButton

                    objectName: "clearRecentsButton"
                    Layout.fillWidth: true
                    theme: navigator.theme
                    registry: navigator.registry
                    actionId: "recent.clear"
                }
            }
        }
    }

    ActionMenu {
        id: pathActionMenu

        objectName: "pathActionMenu"
        registry: navigator.registry
        theme: navigator.theme
    }
}

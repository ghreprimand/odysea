// Current-tree fuzzy find. Interaction deliberately mirrors CommandPalette:
// a modal popup, filter focus on open, arrow navigation from the field,
// Return or pointer activation, Escape/outside dismissal, and automatic focus
// restoration through Popup rather than a second focus-management path.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: finder

    required property var finderModel
    required property var shellModel
    required property var theme

    objectName: "fuzzyFindOverlay"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(620, parent ? parent.width - 80 : 620)
    height: Math.min(480, parent ? parent.height - 120 : 480)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round(parent.height * 0.12) : 0
    padding: 10

    background: Rectangle {
        color: finder.theme.panel
        border.color: finder.theme.border
        radius: 8
    }

    onAboutToHide: finder.finderModel.cancel()

    function openFor(path, showHidden) {
        filterField.text = "";
        finder.finderModel.query = "";
        finder.finderModel.start(path, showHidden);
        finder.open();
        filterField.forceActiveFocus();
    }

    function revealCurrent() {
        if (finder.finderModel.currentIndex >= 0) {
            resultList.positionViewAtIndex(finder.finderModel.currentIndex, ListView.Contain);
        }
    }

    function moveHighlight(direction) {
        finder.finderModel.moveCursor(direction);
        revealCurrent();
    }

    function activateCurrent() {
        finder.finderModel.activateCurrent();
    }

    Connections {
        target: finder.finderModel

        function onResultActivated(path, isDirectory) {
            finder.close();
            finder.shellModel.navigateToEntry(path, isDirectory);
        }
    }

    contentItem: ColumnLayout {
        spacing: 8

        ShellTextField {
            id: filterField

            objectName: "fuzzyFindField"
            Layout.fillWidth: true
            theme: finder.theme
            fieldColor: finder.theme.background
            placeholderText: qsTr("Find in current tree")
            Accessible.name: qsTr("Current tree search")
            onTextChanged: finder.finderModel.query = text
            Keys.onDownPressed: finder.moveHighlight(1)
            Keys.onUpPressed: finder.moveHighlight(-1)
            onAccepted: finder.activateCurrent()
        }

        Text {
            objectName: "fuzzyFindStatus"
            Layout.fillWidth: true
            text: {
                if (finder.finderModel.errorString.length > 0) {
                    return finder.finderModel.errorString;
                }
                if (finder.finderModel.busy) {
                    return qsTr("Indexing %1 entries…").arg(finder.finderModel.entriesVisited);
                }
                if (filterField.text.trim().length === 0) {
                    return qsTr("Type to search %1 indexed paths").arg(finder.finderModel.candidatesIndexed);
                }
                if (finder.finderModel.ranking) {
                    return qsTr("Ranking %1 indexed paths…").arg(finder.finderModel.candidatesIndexed);
                }
                return qsTr("%1 matches").arg(resultList.count);
            }
            font.family: finder.theme.captionFontFamily
            font.pixelSize: finder.theme.captionFontPixelSize
            color: finder.finderModel.errorString.length > 0 ? finder.theme.danger : finder.theme.textMuted
            elide: Text.ElideMiddle
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        ListView {
            id: resultList

            objectName: "fuzzyFindResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: finder.finderModel
            currentIndex: finder.finderModel.currentIndex
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            cacheBuffer: 0
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Current tree search results")

            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                id: resultRow

                required property int index
                required property string name
                required property string entryPath
                required property string relativePath
                required property bool isDirectory
                required property bool selected

                objectName: "fuzzyFindResult-" + index
                width: resultList.width
                height: finder.theme.rowHeight
                color: selected ? finder.theme.selectionBed : "transparent"
                radius: 4
                Accessible.role: Accessible.ListItem
                Accessible.name: name
                Accessible.description: relativePath
                Accessible.selected: selected

                TapHandler {
                    onTapped: {
                        finder.finderModel.selectRow(resultRow.index);
                        finder.finderModel.activate(resultRow.index);
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    VectorIcon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        name: resultRow.isDirectory ? "folder" : "file"
                        highContrast: finder.theme.highContrast
                        ink: finder.theme.iconInk
                    }

                    Text {
                        Layout.preferredWidth: Math.min(220, implicitWidth)
                        text: resultRow.name
                        font.family: finder.theme.chromeFontFamily
                        font.pixelSize: finder.theme.chromeFontPixelSize
                        color: finder.theme.text
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: resultRow.relativePath
                        font.family: finder.theme.captionFontFamily
                        font.pixelSize: finder.theme.captionFontPixelSize
                        color: finder.theme.textMuted
                        elide: Text.ElideMiddle
                    }
                }
            }
        }
    }
}

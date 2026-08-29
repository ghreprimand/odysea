// Entry-kind outline icon. The entry metadata selects both geometry and an
// existing semantic ink; callers do not maintain a parallel type-to-color map.
pragma ComponentBehavior: Bound
import QtQuick

VectorIcon {
    id: icon

    property bool directory: false
    property bool symbolicLink: false
    property color directoryInk: "#808080"
    property color fileInk: "#808080"
    property color symbolicLinkInk: "#808080"

    readonly property string semanticName: symbolicLink ? "symlink" : (directory ? "folder" : "file")
    readonly property color semanticInk: symbolicLink ? symbolicLinkInk : (directory ? directoryInk : fileInk)

    name: semanticName
    ink: semanticInk
    outlineStrokeWidth: highContrast ? 2.2 : 1.45
}

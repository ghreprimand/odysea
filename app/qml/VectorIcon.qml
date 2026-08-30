// Theme-colored vector symbols shared by entries, chrome, and menus.
// Geometry lives in a 24-unit coordinate space and scales through the scene
// graph, keeping the same source crisp at fractional scale and high DPI.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

Item {
    id: icon

    property string name: ""
    property color ink: "#808080"
    property bool highContrast: false
    property real outlineStrokeWidth: highContrast ? 2.2 : 1.7
    readonly property string pathData: {
        switch (name) {
        case "identity":
            return "M12 3 A9 9 0 1 0 12 21 A9 9 0 1 0 12 3 M5 13 C7.25 10 9.5 10 12 13 C14.5 16 16.75 16 19 13";
        case "folder":
            return "M3 7 L9 7 L11 9 L21 9 L21 19 L3 19 Z";
        case "file":
            return "M6 3 L14 3 L19 8 L19 21 L6 21 Z M14 3 L14 8 L19 8";
        case "symlink":
            return "M9 15 L15 9 M13 6 L15 4 C17 2 20 3 21 5 C22 7 21 9 19 11 L17 13 M11 17 L9 19 C7 21 4 20 3 18 C2 16 3 14 5 12 L7 10";
        case "back":
            return "M20 12 L5 12 M11 6 L5 12 L11 18";
        case "forward":
            return "M4 12 L19 12 M13 6 L19 12 L13 18";
        case "up":
            return "M12 20 L12 5 M6 11 L12 5 L18 11";
        case "refresh":
            return "M20 7 L20 3 L16 3 M20 3 L16.5 6.5 A8 8 0 1 0 20 14";
        case "undo":
            return "M9 8 L4 12 L9 16 M4 12 L14 12 C18 12 20 14 20 18";
        case "panes":
            return "M3 5 L21 5 L21 19 L3 19 Z M12 5 L12 19";
        case "list":
            return "M5 6 L6 6 M10 6 L20 6 M5 12 L6 12 M10 12 L20 12 M5 18 L6 18 M10 18 L20 18";
        case "grid":
            return "M4 4 L10 4 L10 10 L4 10 Z M14 4 L20 4 L20 10 L14 10 Z M4 14 L10 14 L10 20 L4 20 Z M14 14 L20 14 L20 20 L14 20 Z";
        case "columns":
            return "M3 4 L8 4 L8 20 L3 20 Z M10 4 L16 4 L16 20 L10 20 Z M18 4 L21 4 L21 20 L18 20 Z";
        case "appearance":
            return "M4 6 L20 6 M8 3 L8 9 M4 12 L20 12 M16 9 L16 15 M4 18 L20 18 M11 15 L11 21";
        case "add":
            return "M12 5 L12 19 M5 12 L19 12";
        case "close":
            return "M6 6 L18 18 M18 6 L6 18";
        case "pause":
            return "M9 5 L9 19 M15 5 L15 19";
        case "play":
            return "M8 5 L19 12 L8 19 Z";
        case "select-all":
            return "M4 8 L4 4 L8 4 M16 4 L20 4 L20 8 M20 16 L20 20 L16 20 M8 20 L4 20 L4 16 M8 12 L11 15 L17 9";
        case "copy":
            return "M8 8 L20 8 L20 20 L8 20 Z M4 16 L4 4 L16 4 L16 8";
        case "move":
            return "M12 3 L12 21 M3 12 L21 12 M12 3 L9 6 M12 3 L15 6 M21 12 L18 9 M21 12 L18 15 M12 21 L9 18 M12 21 L15 18 M3 12 L6 9 M3 12 L6 15";
        case "rename":
            return "M4 20 L8 19 L19 8 L16 5 L5 16 Z M14 7 L17 10 M4 20 L5 16";
        case "trash":
            return "M5 7 L19 7 M9 7 L9 4 L15 4 L15 7 M7 7 L8 20 L16 20 L17 7 M10 11 L10 17 M14 11 L14 17";
        case "open":
            return "M5 5 L12 5 M5 5 L5 19 L19 19 L19 12 M12 12 L20 4 M14 4 L20 4 L20 10";
        case "preview":
            return "M2.5 12 C5.5 7 8.5 5 12 5 C15.5 5 18.5 7 21.5 12 C18.5 17 15.5 19 12 19 C8.5 19 5.5 17 2.5 12 Z M9 12 A3 3 0 1 0 15 12 A3 3 0 1 0 9 12";
        case "commands":
            return "M4 6 L10 12 L4 18 M13 18 L20 18";
        case "search":
            return "M10.5 4 A6.5 6.5 0 1 0 10.5 17 A6.5 6.5 0 1 0 10.5 4 M15.5 15.5 L21 21";
        default:
            return "";
        }
    }

    implicitWidth: 18
    implicitHeight: 18

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: Math.min(icon.width / width, icon.height / height)
        preferredRendererType: Shape.GeometryRenderer

        ShapePath {
            strokeColor: icon.ink
            strokeWidth: icon.outlineStrokeWidth
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            fillColor: "transparent"

            PathSvg {
                path: icon.pathData
            }
        }
    }
}

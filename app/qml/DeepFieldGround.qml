import QtQuick

// The material ground content sits on. At `deepField` zero this is a flat
// sheet — a plain Rectangle path with no extra layers. Above zero, still
// gradients deepen the sheet from the edges inward: a vertical ramp toward
// the deep tone plus feathered side strips, all scaled by the deep-field
// amount. This is a still material treatment, not an emissive effect, so it
// renders identically on every scene-graph backend.
//
// `fillOpacity` is the glass amount: it fades only this ground material.
// Content above it stays opaque, so translucency reads as material depth
// without ever thinning text.
Item {
    id: ground

    property real deepField: 0
    property color sheetColor: "#000000"
    property color deepColor: "#000000"
    property real fillOpacity: 1.0
    property real radius: 0
    property color strokeColor: "transparent"

    Rectangle {
        anchors.fill: parent
        radius: ground.radius
        color: Qt.alpha(ground.sheetColor, ground.fillOpacity)
    }

    Rectangle {
        visible: ground.deepField > 0.001
        anchors.fill: parent
        radius: ground.radius
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: Qt.alpha(ground.deepColor, 0.22 * ground.deepField * ground.fillOpacity)
            }
            GradientStop {
                position: 0.45
                color: "transparent"
            }
            GradientStop {
                position: 1.0
                color: Qt.alpha(ground.deepColor, 0.50 * ground.deepField * ground.fillOpacity)
            }
        }
    }

    Rectangle {
        visible: ground.deepField > 0.001
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.round(parent.width * 0.12)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0.0
                color: Qt.alpha(ground.deepColor, 0.30 * ground.deepField * ground.fillOpacity)
            }
            GradientStop {
                position: 1.0
                color: "transparent"
            }
        }
    }

    Rectangle {
        visible: ground.deepField > 0.001
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.round(parent.width * 0.12)
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0.0
                color: "transparent"
            }
            GradientStop {
                position: 1.0
                color: Qt.alpha(ground.deepColor, 0.30 * ground.deepField * ground.fillOpacity)
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: ground.radius
        color: "transparent"
        border.color: ground.strokeColor
    }
}

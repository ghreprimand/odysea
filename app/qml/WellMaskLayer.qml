pragma ComponentBehavior: Bound
import QtQuick

// Protected-content mask. Views register the items whose pixels must stay
// color-true — thumbnail and preview wells — and this layer mirrors each one
// as a white rectangle at its mapped position. The layer is never shown;
// the presentation pipeline samples it as a mask texture, and both the
// bright pass and the composite exempt every pixel it covers.
//
// Mapped positions are not reactive on their own: an item scrolling inside
// a view moves without changing its own x or y. Registering views call
// `bump()` whenever their viewport moves, which refreshes every mirror.
Item {
    id: maskLayer

    visible: false

    property var wellItems: []
    property int revision: 0

    function registerWell(item) {
        if (item === null || wellItems.indexOf(item) !== -1) {
            return;
        }
        const next = wellItems.slice();
        next.push(item);
        wellItems = next;
    }

    function unregisterWell(item) {
        const at = wellItems.indexOf(item);
        if (at === -1) {
            return;
        }
        const next = wellItems.slice();
        next.splice(at, 1);
        wellItems = next;
    }

    /// Refreshes every mirror. Called by views when their viewport moves.
    function bump() {
        revision += 1;
    }

    Repeater {
        model: maskLayer.wellItems

        Rectangle {
            id: mirror

            required property Item modelData

            readonly property point mapped: {
                void maskLayer.revision;
                if (mirror.modelData === null || mirror.modelData.parent === null) {
                    return Qt.point(-1, -1);
                }
                return mirror.modelData.mapToItem(maskLayer, 0, 0);
            }

            x: mapped.x
            y: mapped.y
            width: mirror.modelData !== null ? mirror.modelData.width : 0
            height: mirror.modelData !== null ? mirror.modelData.height : 0
            visible: mirror.modelData !== null && mirror.modelData.visible
            color: "#FFFFFF"
        }
    }
}

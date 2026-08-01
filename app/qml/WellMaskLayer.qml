pragma ComponentBehavior: Bound
import QtQuick

// Protected-content mask. Views register the items whose pixels must stay
// color-true — thumbnail and preview wells — and this layer mirrors each one
// as a white rectangle at its mapped position. The layer is never shown;
// the presentation pipeline samples it as a mask texture, and both the
// bright pass and the composite exempt every pixel it covers.
//
// Registration is incremental: each well owns exactly one mirror object,
// created on registration and destroyed on unregistration, so adding one
// well never rebuilds the mirrors that already exist. A view realizes
// delegates beyond its visible bounds (cache buffers), so a registered well
// can sit outside the viewport that clips it; each registration therefore
// names the clipping viewport, and the mirror intersects its mapped
// rectangle against it. A well scrolled out of its viewport contributes
// nothing, so the exemption can never land on unrelated chrome.
//
// Mapped positions are not reactive on their own: an item scrolling inside
// a view moves without changing its own x or y. Registering views call
// `bump()` whenever their viewport moves, which refreshes every mirror.
Item {
    id: maskLayer

    visible: false

    /// Number of currently registered wells.
    property int wellCount: 0
    /// Total mirror objects ever created. Registration is incremental, so
    /// this advances by exactly one per newly registered well; tests use it
    /// to reject any rebuild of existing mirrors.
    property int mirrorCreationCount: 0
    property int revision: 0

    // One { well, mirror } record per registration. Mutated in place; the
    // count is published through wellCount.
    property var entries: []

    /// Registers a well, optionally with the viewport item that clips it.
    /// The mirror is suppressed outside the viewport's mapped bounds.
    /// Re-registering an already registered well is a no-op.
    function registerWell(item, viewport) {
        if (item === null || item === undefined) {
            return;
        }
        pruneStaleEntries();
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i].well === item) {
                return;
            }
        }
        const mirror = mirrorComponent.createObject(maskLayer, {
            "well": item,
            "viewport": viewport === undefined ? null : viewport
        });
        entries.push({
            "well": item,
            "mirror": mirror
        });
        wellCount = entries.length;
    }

    function unregisterWell(item) {
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i].well === item) {
                entries[i].mirror.destroy();
                entries.splice(i, 1);
                break;
            }
        }
        pruneStaleEntries();
        wellCount = entries.length;
    }

    /// Returns the mirror object for a registered well, or null.
    function mirrorFor(item) {
        if (item === null || item === undefined) {
            return null;
        }
        for (let i = 0; i < entries.length; ++i) {
            if (entries[i].well === item) {
                return entries[i].mirror;
            }
        }
        return null;
    }

    /// Drops records whose well was destroyed without unregistering. The
    /// registering delegates unregister on destruction, so this is a
    /// defensive sweep, not the primary lifecycle path. The mirror's `well`
    /// property is the authority: a declared Item property resets to null
    /// when its object is destroyed.
    function pruneStaleEntries() {
        for (let i = entries.length - 1; i >= 0; --i) {
            if (entries[i].mirror.well === null) {
                entries[i].mirror.destroy();
                entries.splice(i, 1);
            }
        }
        wellCount = entries.length;
    }

    /// Refreshes every mirror. Called by views when their viewport moves.
    function bump() {
        revision += 1;
    }

    Component {
        id: mirrorComponent

        Rectangle {
            id: mirror

            objectName: "wellMirror"

            property Item well: null
            property Item viewport: null

            // Mapped well rectangle intersected with the mapped viewport.
            // Depends on `revision` so scroll notifications re-evaluate it.
            readonly property rect clipped: {
                void maskLayer.revision;
                if (mirror.well === null || mirror.well.parent === null) {
                    return Qt.rect(0, 0, 0, 0);
                }
                const mapped = mirror.well.mapToItem(maskLayer, 0, 0);
                let left = mapped.x;
                let top = mapped.y;
                let right = left + mirror.well.width;
                let bottom = top + mirror.well.height;
                if (mirror.viewport !== null) {
                    const viewMapped = mirror.viewport.mapToItem(maskLayer, 0, 0);
                    left = Math.max(left, viewMapped.x);
                    top = Math.max(top, viewMapped.y);
                    right = Math.min(right, viewMapped.x + mirror.viewport.width);
                    bottom = Math.min(bottom, viewMapped.y + mirror.viewport.height);
                }
                return Qt.rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top));
            }

            x: clipped.x
            y: clipped.y
            width: clipped.width
            height: clipped.height
            visible: mirror.well !== null && mirror.well.visible && clipped.width > 0 && clipped.height > 0
            color: "#FFFFFF"

            Component.onCompleted: maskLayer.mirrorCreationCount += 1
        }
    }
}

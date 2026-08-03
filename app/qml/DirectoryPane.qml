// A directory pane: the deep-field ground sheet with the list and grid
// views above it. The pane is the single site that maps the theme's
// semantic roles onto the views' granular color and font properties, so a
// scene hosting a pane binds one theme instead of twenty values.
//
// Layout warning: the pane adds no clip and no transform between the grid
// and the presentation layer. The grid names itself as the clipping
// viewport when thumbnails register as protected wells, and the mask layer
// maps well rectangles assuming that single clip level and identity
// transforms; wrapping the views in a clipped or scaled container would
// break that mapping silently.
import QtQuick

FocusScope {
    id: pane

    required property var shellModel
    required property var navigationController
    required property var registry
    required property var theme
    /// Whether the grid or the list renders. The mode is workspace state,
    /// so it lives on the shell and arrives here as a binding.
    property bool gridMode: false
    /// How long the current-entry ring persists when the cursor moves away.
    /// The shell binds the presentation layer's motion token; zero renders
    /// instantly under reduced motion.
    property int persistenceDurationMs: 0
    /// Optional protected-content mask layer for grid thumbnails.
    property WellMaskLayer wellLayer: null

    /// The pane's one shared context menu. Both views open it for entry,
    /// selection, and blank-canvas targets; the menu builds its items from
    /// the registry for whichever context each invocation passes.
    readonly property ActionMenu actionMenu: paneActionMenu

    function focusCurrentView() {
        if (pane.gridMode) {
            directoryGrid.forceViewFocus();
        } else {
            directoryList.focusView();
        }
    }

    function revealCurrent() {
        if (pane.gridMode) {
            directoryGrid.revealCurrent();
        } else {
            directoryList.revealCurrent();
        }
    }

    // The pane ground is the material sheet content sits on: it carries
    // the deep field and is the only surface the glass amount fades, so
    // translucency reads as depth without thinning any text above it.
    DeepFieldGround {
        anchors.fill: parent
        deepField: pane.theme.effectiveDeepField
        sheetColor: pane.theme.background
        deepColor: pane.theme.backgroundDeep
        fillOpacity: pane.theme.glassOpacity
        radius: 6
        strokeColor: directoryList.activeFocus || directoryGrid.activeFocus ? pane.theme.accent : pane.theme.border
    }

    DirectoryListView {
        id: directoryList

        anchors.fill: parent
        visible: !pane.gridMode
        shellModel: pane.shellModel
        navigationController: pane.navigationController
        rowHeight: pane.theme.rowHeight
        accentColor: pane.theme.accent
        primaryTextColor: pane.theme.text
        secondaryTextColor: pane.theme.textMuted
        selectionColor: pane.theme.selectionBed
        dirInkColor: pane.theme.dirInk
        fileInkColor: pane.theme.textFaint
        linkInkColor: pane.theme.linkInk
        iconInkColor: pane.theme.iconInk
        dangerColor: pane.theme.danger
        hoverColor: pane.theme.hover
        rubberBandColor: pane.theme.rubberBand
        entryFontFamily: pane.theme.contentFontFamily
        entryFontPixelSize: pane.theme.contentFontPixelSize
        captionFontFamily: pane.theme.captionFontFamily
        captionFontPixelSize: pane.theme.captionFontPixelSize
        highContrast: pane.theme.highContrast
        persistenceDurationMs: pane.persistenceDurationMs
        actionMenu: pane.actionMenu
    }

    DirectoryGridView {
        id: directoryGrid

        anchors.fill: parent
        visible: pane.gridMode
        shellModel: pane.shellModel
        navigationController: pane.navigationController
        backgroundColor: pane.theme.background
        panelColor: pane.theme.panel
        borderColor: pane.theme.border
        primaryTextColor: pane.theme.text
        secondaryTextColor: pane.theme.textMuted
        accentColor: pane.theme.accent
        selectionColor: pane.theme.selectionBed
        dirInkColor: pane.theme.dirInk
        fileInkColor: pane.theme.textFaint
        linkInkColor: pane.theme.linkInk
        iconInkColor: pane.theme.iconInk
        dangerColor: pane.theme.danger
        hoverColor: pane.theme.hover
        rubberBandColor: pane.theme.rubberBand
        entryFontFamily: pane.theme.contentFontFamily
        entryFontPixelSize: pane.theme.contentFontPixelSize
        captionFontFamily: pane.theme.captionFontFamily
        captionFontPixelSize: pane.theme.captionFontPixelSize
        highContrast: pane.theme.highContrast
        cellWidth: pane.theme.gridCellWidth
        cellHeight: pane.theme.gridCellHeight
        persistenceDurationMs: pane.persistenceDurationMs
        wellLayer: pane.wellLayer
        actionMenu: pane.actionMenu
    }

    ActionMenu {
        id: paneActionMenu

        objectName: "paneActionMenu"
        registry: pane.registry
        theme: pane.theme
    }
}

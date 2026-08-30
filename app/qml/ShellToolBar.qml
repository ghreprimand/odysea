// The navigation toolbar: history and refresh actions plus workspace toggles
// for panes, view mode, and appearance. Path orientation and direct entry live
// in PathNavigator. Every button renders from the shared action registry, so
// labels, icons, enablement, and shortcut hints share one declaration site.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ToolBar {
    id: bar

    required property var shellModel
    required property ActionRegistry registry
    required property var theme

    readonly property real chromeMargin: 6
    /// The full-label row is measured separately, so compact mode follows
    /// the controls that are actually present rather than a stale width
    /// guess. The measure lives in the background item because ToolBar, like
    /// every Control, owns one content child only.
    readonly property real labeledWidthRequirement: (2 * bar.chromeMargin) + labeledMeasureRow.implicitWidth
    readonly property bool compact: bar.width < Math.ceil(bar.labeledWidthRequirement)

    objectName: "navigationToolBar"
    implicitHeight: Math.max(44, toolbarRow.implicitHeight + (2 * bar.chromeMargin))
    leftPadding: 0
    rightPadding: 0

    background: ChromeStrip {
        id: toolbarBackground

        theme: bar.theme

        RowLayout {
            id: labeledMeasureRow

            // This is an intrinsic-size probe, not visible chrome. Keeping
            // it under the background avoids becoming a second ToolBar
            // content child, which would displace the live row.
            visible: false
            spacing: toolbarRow.spacing

            ApplicationMark {
                theme: bar.theme
                Layout.leftMargin: 2
            }

            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "nav.back"
                showLabel: false
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "nav.forward"
                showLabel: false
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "nav.up"
                showLabel: false
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "nav.refresh"
                showLabel: false
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "edit.undo"
                showLabel: false
            }

            Item {
                Layout.fillWidth: true
            }

            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "pane.toggleDual"
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "view.list"
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "view.grid"
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "view.columns"
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "find.tree"
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "palette.open"
            }
            ActionButton {
                theme: bar.theme
                registry: bar.registry
                actionId: "appearance.open"
            }
        }
    }

    // Below the full-row width, workspace toggles render as icons
    // only. Their accessible names and tooltips retain the action labels.
    RowLayout {
        id: toolbarRow

        objectName: "toolbarVisibleRow"
        anchors.fill: parent
        anchors.margins: bar.chromeMargin
        spacing: 6

        ApplicationMark {
            objectName: "applicationIdentityMark"
            theme: bar.theme
            Layout.leftMargin: 2
            Accessible.ignored: true
        }

        ActionButton {
            objectName: "backButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "nav.back"
            showLabel: false
        }
        ActionButton {
            objectName: "forwardButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "nav.forward"
            showLabel: false
        }
        ActionButton {
            objectName: "upButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "nav.up"
            showLabel: false
        }
        ActionButton {
            objectName: "refreshButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "nav.refresh"
            showLabel: false
        }
        ActionButton {
            objectName: "undoButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "edit.undo"
            showLabel: false
        }

        Item {
            Layout.fillWidth: true
        }

        ActionButton {
            objectName: "paneToggleButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "pane.toggleDual"
            showLabel: !bar.compact
        }
        ActionButton {
            objectName: "listViewButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "view.list"
            showLabel: !bar.compact
        }
        ActionButton {
            objectName: "gridViewButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "view.grid"
            showLabel: !bar.compact
        }
        ActionButton {
            objectName: "columnsViewButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "view.columns"
            showLabel: !bar.compact
        }
        ActionButton {
            objectName: "treeSearchButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "find.tree"
            showLabel: !bar.compact
        }
        ActionButton {
            objectName: "paletteButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "palette.open"
            showLabel: !bar.compact
        }
        ActionButton {
            objectName: "appearanceButton"
            theme: bar.theme
            registry: bar.registry
            actionId: "appearance.open"
            showLabel: !bar.compact
        }
    }
}

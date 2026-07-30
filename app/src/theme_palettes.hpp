// The curated color families the shell ships with.
//
// Each family is expressed directly in file-manager roles: what the window
// ground, entry text, directory names, selection surfaces, and status inks
// look like. Nothing here follows terminal color conventions; the roles are
// this application's own.
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

namespace odysea::app {

/// One color family resolved into application roles.
struct ShellPalette {
    QString id;
    /// True when the family renders dark ink on a light ground.
    bool light = false;

    /// Window ground.
    QColor sheet;
    /// Darkest ground, behind recessed regions.
    QColor deep;
    /// Recessed content wells such as preview or thumbnail beds.
    QColor well;
    /// Raised panels: toolbars, bars, dialogs.
    QColor inset;
    /// Hairline borders between regions.
    QColor frame;

    /// Primary entry text.
    QColor text;
    /// Secondary text: sizes, dates, hints.
    QColor muted;
    /// Faintest legible ink: disabled text, generic file marks.
    QColor faint;

    /// Directory names and directory icon strokes.
    QColor dir;
    /// Symbolic-link indicators.
    QColor link;
    /// Metadata columns beside entry names.
    QColor meta;
    /// Attention accents: the active tab, the current breadcrumb node.
    QColor accent;

    /// Solid bed under selected entries.
    QColor selectionBed;
    /// Entry ink on top of the selection bed.
    QColor selectionInk;
    /// Bed under filter or search matches.
    QColor match;
    /// Keyboard focus and caret ink.
    QColor focus;

    QColor danger;
    QColor warning;
    QColor success;
};

/// The identifier of the shipped default family.
[[nodiscard]] QString defaultShellPaletteId();

/// Every shipped family identifier, default first.
[[nodiscard]] QStringList shellPaletteIds();

/// The family for `id`. An unknown identifier resolves to the shipped
/// default, so a settings file from a build with more families still loads.
[[nodiscard]] const ShellPalette& shellPalette(const QString& id);

} // namespace odysea::app

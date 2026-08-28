// The curated color families the shell ships with.
//
// Each family is expressed directly in file-manager roles: what the window
// ground, entry text, directory names, selection surfaces, and status inks
// look like. Nothing here follows terminal color conventions; the roles are
// this application's own.
#pragma once

#include <QColor>
#include <QList>
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

    /// Toolbar and control symbol ink before the emission cap. Curated per
    /// family rather than derived from the faint ink: the cap limits the
    /// brightest channel, so a family whose faint ink peaks in a
    /// low-luminance channel would land its symbols below the measured
    /// pressed-bed floor. Dark families choose a hue whose capped result
    /// clears that floor; light families render symbols in primary text.
    QColor icon;

    QColor danger;
    /// Danger ink under the high-contrast override: the same hue moved to
    /// clear 4.5:1 on every bed the role renders on, including the
    /// selection and hover beds the base danger ink cannot clear there.
    QColor dangerHigh;
    QColor warning;
    QColor success;
};

/// A stable accent choice with a separately resolved display name.
///
/// The color is the only authored value in a preset. The controller derives
/// its focus, chrome, and emission-facing tokens from this value at runtime.
struct AccentPreset {
    QString id;
    QString name;
    QColor accent;
};

/// The identifier of the shipped default family.
[[nodiscard]] QString defaultShellPaletteId();

/// Every shipped family identifier, default first.
[[nodiscard]] QStringList shellPaletteIds();

/// The family for `id`. An unknown identifier resolves to the shipped
/// default, so a settings file from a build with more families still loads.
[[nodiscard]] const ShellPalette& shellPalette(const QString& id);

/// The identifier of the shipped accent preset.
[[nodiscard]] QString defaultAccentPresetId();

/// Every shipped accent preset, default first.
[[nodiscard]] QList<AccentPreset> shellAccentPresets();

/// The preset for `id`. An unknown identifier resolves to the shipped
/// default, so stored choices remain forward-compatible.
[[nodiscard]] const AccentPreset& shellAccentPreset(const QString& id);

} // namespace odysea::app

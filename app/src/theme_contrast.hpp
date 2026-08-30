// Shared render-site contrast measurement for shell colors.
#pragma once

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace odysea::app {

/// One foreground/background pair at a surface the shell renders.
struct ThemeContrastSample {
    QString role;
    QString renderSite;
    QColor foreground;
    QColor background;
    qreal floor = 1.0;
};

/// WCAG relative-luminance contrast ratio for opaque colors.
[[nodiscard]] qreal themeContrastRatio(const QColor& first, const QColor& second);

/// Returns `color` with the relative luminance of `reference`, retaining its
/// hue direction where the target luminance permits it.
[[nodiscard]] QColor themeColorAtReferenceLuminance(const QColor& color, const QColor& reference);

/// Accent render sites shared by the resolver and acceptance tests.
[[nodiscard]] QList<ThemeContrastSample>
themeAccentContrastSamples(const QColor& accent, const QColor& windowGround,
                           const QColor& selectionBed, const QColor& hoveredSurface,
                           const QColor& pressedSurface, const QColor& panel);

/// Adjusts an accent toward the high-contrast endpoint until the supplied
/// render-site samples clear their floors. Measurement stays in
/// `themeContrastFailures`, so the resolver and warning use one definition.
[[nodiscard]] QColor themeColorClearingContrastSamples(const QColor& color,
                                                       const QList<ThemeContrastSample>& samples,
                                                       bool darkGround);

/// Resolves an authored accent into the displayed token. The active palette
/// supplies the reference luminance and the shared render-site samples repair
/// the color before it can reach a shell surface.
[[nodiscard]] QColor themeResolvedAccentForRenderSites(const QColor& authoredColor,
                                                       const QColor& paletteReference,
                                                       const QList<ThemeContrastSample>& samples,
                                                       bool darkGround);

/// Returns one diagnostic for every render-site sample below its floor.
[[nodiscard]] QStringList themeContrastFailures(const QList<ThemeContrastSample>& samples);

} // namespace odysea::app

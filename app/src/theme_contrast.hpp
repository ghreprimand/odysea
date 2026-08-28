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

/// Returns one diagnostic for every render-site sample below its floor.
[[nodiscard]] QStringList themeContrastFailures(const QList<ThemeContrastSample>& samples);

} // namespace odysea::app

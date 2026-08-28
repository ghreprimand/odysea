// Shared render-site contrast measurement for shell colors.
#include "theme_contrast.hpp"

#include <algorithm>
#include <cmath>

namespace odysea::app {

namespace {

[[nodiscard]] qreal linearChannel(qreal channel) {
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

[[nodiscard]] qreal luminance(const QColor& color) {
    return (0.2126 * linearChannel(static_cast<qreal>(color.redF()))) +
           (0.7152 * linearChannel(static_cast<qreal>(color.greenF()))) +
           (0.0722 * linearChannel(static_cast<qreal>(color.blueF())));
}

} // namespace

qreal themeContrastRatio(const QColor& first, const QColor& second) {
    const qreal brighter = std::max(luminance(first), luminance(second));
    const qreal darker = std::min(luminance(first), luminance(second));
    return (brighter + 0.05) / (darker + 0.05);
}

QStringList themeContrastFailures(const QList<ThemeContrastSample>& samples) {
    QStringList failures;
    for (const ThemeContrastSample& sample : samples) {
        const qreal measured = themeContrastRatio(sample.foreground, sample.background);
        if (measured < sample.floor) {
            failures.append(QStringLiteral("%1 on %2: %3 < %4")
                                .arg(sample.role, sample.renderSite,
                                     QString::number(measured, 'f', 2),
                                     QString::number(sample.floor, 'f', 2)));
        }
    }
    return failures;
}

} // namespace odysea::app

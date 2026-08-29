// Shared render-site contrast measurement for shell colors.
#include "theme_contrast.hpp"

#include <algorithm>
#include <cmath>

namespace odysea::app {

namespace {

[[nodiscard]] qreal linearChannel(qreal channel) {
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

[[nodiscard]] qreal encodedChannel(qreal channel) {
    return channel <= 0.0031308 ? channel * 12.92 : (1.055 * std::pow(channel, 1.0 / 2.4)) - 0.055;
}

[[nodiscard]] qreal luminance(const QColor& color) {
    return (0.2126 * linearChannel(static_cast<qreal>(color.redF()))) +
           (0.7152 * linearChannel(static_cast<qreal>(color.greenF()))) +
           (0.0722 * linearChannel(static_cast<qreal>(color.blueF())));
}

[[nodiscard]] QColor mixLinear(const QColor& first, const QColor& second, qreal amount) {
    const qreal inverse = 1.0 - amount;
    const qreal red = (linearChannel(static_cast<qreal>(first.redF())) * inverse) +
                      (linearChannel(static_cast<qreal>(second.redF())) * amount);
    const qreal green = (linearChannel(static_cast<qreal>(first.greenF())) * inverse) +
                        (linearChannel(static_cast<qreal>(second.greenF())) * amount);
    const qreal blue = (linearChannel(static_cast<qreal>(first.blueF())) * inverse) +
                       (linearChannel(static_cast<qreal>(second.blueF())) * amount);
    return QColor::fromRgbF(static_cast<float>(encodedChannel(red)),
                            static_cast<float>(encodedChannel(green)),
                            static_cast<float>(encodedChannel(blue)), first.alphaF());
}

[[nodiscard]] bool clearsContrastSamples(const QColor& color,
                                         const QList<ThemeContrastSample>& samples) {
    QList<ThemeContrastSample> measured = samples;
    for (ThemeContrastSample& sample : measured) {
        sample.foreground = color;
    }
    return themeContrastFailures(measured).isEmpty();
}

} // namespace

qreal themeContrastRatio(const QColor& first, const QColor& second) {
    const qreal brighter = std::max(luminance(first), luminance(second));
    const qreal darker = std::min(luminance(first), luminance(second));
    return (brighter + 0.05) / (darker + 0.05);
}

QColor themeColorAtReferenceLuminance(const QColor& color, const QColor& reference) {
    const qreal source = luminance(color);
    const qreal target = luminance(reference);
    if (qFuzzyCompare(source, target)) {
        return color;
    }
    if (source <= 0.0) {
        return reference;
    }

    const qreal red = linearChannel(static_cast<qreal>(color.redF()));
    const qreal green = linearChannel(static_cast<qreal>(color.greenF()));
    const qreal blue = linearChannel(static_cast<qreal>(color.blueF()));
    if (target < source) {
        const qreal multiplier = target / source;
        return QColor::fromRgbF(static_cast<float>(encodedChannel(red * multiplier)),
                                static_cast<float>(encodedChannel(green * multiplier)),
                                static_cast<float>(encodedChannel(blue * multiplier)),
                                color.alphaF());
    }

    const qreal mix = (target - source) / (1.0 - source);
    return QColor::fromRgbF(static_cast<float>(encodedChannel(red + ((1.0 - red) * mix))),
                            static_cast<float>(encodedChannel(green + ((1.0 - green) * mix))),
                            static_cast<float>(encodedChannel(blue + ((1.0 - blue) * mix))),
                            color.alphaF());
}

QList<ThemeContrastSample>
themeAccentContrastSamples(const QColor& accent, const QColor& windowGround,
                           const QColor& selectionBed, const QColor& hoveredSurface,
                           const QColor& pressedSurface, const QColor& panel) {
    return {{.role = QStringLiteral("Accent"),
             .renderSite = QStringLiteral("window ground"),
             .foreground = accent,
             .background = windowGround,
             .floor = 3.0},
            {.role = QStringLiteral("Accent"),
             .renderSite = QStringLiteral("selected entry"),
             .foreground = accent,
             .background = selectionBed,
             .floor = 3.0},
            {.role = QStringLiteral("Accent"),
             .renderSite = QStringLiteral("hovered surface"),
             .foreground = accent,
             .background = hoveredSurface,
             .floor = 3.0},
            {.role = QStringLiteral("Accent"),
             .renderSite = QStringLiteral("pressed surface"),
             .foreground = accent,
             .background = pressedSurface,
             .floor = 3.0},
            {.role = QStringLiteral("Accent"),
             .renderSite = QStringLiteral("panel"),
             .foreground = accent,
             .background = panel,
             .floor = 3.0}};
}

QColor themeColorClearingContrastSamples(const QColor& color,
                                         const QList<ThemeContrastSample>& samples,
                                         bool darkGround) {
    if (clearsContrastSamples(color, samples)) {
        return color;
    }

    const QColor endpoint = darkGround ? QColor(Qt::white) : QColor(Qt::black);
    if (!clearsContrastSamples(endpoint, samples)) {
        return endpoint;
    }

    qreal lower = 0.0;
    qreal upper = 1.0;
    QColor result = endpoint;
    for (int step = 0; step < 18; ++step) {
        const qreal middle = (lower + upper) / 2.0;
        const QColor candidate = mixLinear(color, endpoint, middle);
        if (clearsContrastSamples(candidate, samples)) {
            upper = middle;
            result = candidate;
        } else {
            lower = middle;
        }
    }
    return result;
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

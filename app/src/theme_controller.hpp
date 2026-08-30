// The shell's appearance state, live and persistent.
//
// One object owns everything the appearance controls change: the active color
// family resolved into file-manager roles, the typography roles, the metric
// scale, and the screen-effect levels. Every setter applies immediately —
// QML bindings observe the change in the same event — and persists through
// the core appearance model when a storage path is configured.
//
// The type is registered with the shell module as `ShellTheme`, so scenes and
// tests instantiate the same implementation the application configures.
#pragma once

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "odysea/core/appearance.hpp"

namespace odysea::app {

class ThemeController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ShellTheme)

    /// File the state persists in. Empty (the default) keeps the state in
    /// memory only, which is what scene tests want. Setting a path loads it.
    Q_PROPERTY(QString storagePath READ storagePath WRITE setStoragePath NOTIFY storagePathChanged)

    Q_PROPERTY(QStringList availablePalettes READ availablePalettes CONSTANT)
    Q_PROPERTY(QString paletteId READ paletteId WRITE setPaletteId NOTIFY appearanceChanged)
    Q_PROPERTY(bool lightPalette READ lightPalette NOTIFY appearanceChanged)
    Q_PROPERTY(QVariantList accentPresets READ accentPresets CONSTANT)
    Q_PROPERTY(
        QString accentPresetId READ accentPresetId WRITE setAccentPresetId NOTIFY appearanceChanged)
    Q_PROPERTY(int accentPresetIndex READ accentPresetIndex WRITE setAccentPresetIndex NOTIFY
                   appearanceChanged)

    Q_PROPERTY(Profile profile READ profile WRITE setProfile NOTIFY appearanceChanged)
    Q_PROPERTY(Source fontSource READ fontSource WRITE setFontSource NOTIFY appearanceChanged)
    Q_PROPERTY(QString namedFontFamily READ namedFontFamily WRITE setNamedFontFamily NOTIFY
                   appearanceChanged)
    Q_PROPERTY(bool bundledFontAvailable READ bundledFontAvailable CONSTANT)
    Q_PROPERTY(QString contentFontFamily READ contentFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(QString chromeFontFamily READ chromeFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(QString pathFontFamily READ pathFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(QString captionFontFamily READ captionFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(QString longFormFontFamily READ longFormFontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(Densities density READ density WRITE setDensity NOTIFY appearanceChanged)
    Q_PROPERTY(qreal uiScale READ uiScale WRITE setUiScale NOTIFY appearanceChanged)

    Q_PROPERTY(qreal glassOpacity READ glassOpacity WRITE setGlassOpacity NOTIFY appearanceChanged)
    Q_PROPERTY(
        qreal surfaceOpacity READ surfaceOpacity WRITE setSurfaceOpacity NOTIFY appearanceChanged)
    Q_PROPERTY(
        bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY appearanceChanged)
    Q_PROPERTY(bool highContrast READ highContrast WRITE setHighContrast NOTIFY appearanceChanged)

    // Effect levels, in two views with distinct consumers.
    //
    // The plain properties are the stored preference: reads resolve the
    // active profile (preset table, or the stored custom levels) without the
    // accessibility overrides, and writes adjust the custom profile and make
    // it active, which is what a user dragging a slider means. The controls
    // bind these, so a slider keeps showing and accepting the user's value
    // even while an override is pinning what gets rendered.
    Q_PROPERTY(qreal bloomCore READ bloomCore WRITE setBloomCore NOTIFY appearanceChanged)
    Q_PROPERTY(qreal bloomWide READ bloomWide WRITE setBloomWide NOTIFY appearanceChanged)
    Q_PROPERTY(qreal scanline READ scanline WRITE setScanline NOTIFY appearanceChanged)
    Q_PROPERTY(qreal vignette READ vignette WRITE setVignette NOTIFY appearanceChanged)
    Q_PROPERTY(qreal persistence READ persistence WRITE setPersistence NOTIFY appearanceChanged)
    Q_PROPERTY(qreal deepField READ deepField WRITE setDeepField NOTIFY appearanceChanged)
    Q_PROPERTY(qreal textLift READ textLift WRITE setTextLift NOTIFY appearanceChanged)

    // The effective properties are what the rendering pipeline consumes: the
    // stored levels after the reduced-motion and high-contrast overrides.
    // Rendering surfaces bind these and nothing writes them.
    Q_PROPERTY(qreal effectiveBloomCore READ effectiveBloomCore NOTIFY appearanceChanged)
    Q_PROPERTY(qreal effectiveBloomWide READ effectiveBloomWide NOTIFY appearanceChanged)
    Q_PROPERTY(qreal effectiveScanline READ effectiveScanline NOTIFY appearanceChanged)
    Q_PROPERTY(qreal effectiveVignette READ effectiveVignette NOTIFY appearanceChanged)
    Q_PROPERTY(qreal effectivePersistence READ effectivePersistence NOTIFY appearanceChanged)
    Q_PROPERTY(qreal effectiveDeepField READ effectiveDeepField NOTIFY appearanceChanged)
    Q_PROPERTY(qreal effectiveTextLift READ effectiveTextLift NOTIFY appearanceChanged)

    // Metrics derived from density and scale.
    Q_PROPERTY(int rowHeight READ rowHeight NOTIFY appearanceChanged)
    Q_PROPERTY(int gridCellWidth READ gridCellWidth NOTIFY appearanceChanged)
    Q_PROPERTY(int gridCellHeight READ gridCellHeight NOTIFY appearanceChanged)
    Q_PROPERTY(int chromeFontPixelSize READ chromeFontPixelSize NOTIFY appearanceChanged)
    Q_PROPERTY(int contentFontPixelSize READ contentFontPixelSize NOTIFY appearanceChanged)
    Q_PROPERTY(int pathFontPixelSize READ pathFontPixelSize NOTIFY appearanceChanged)
    Q_PROPERTY(int captionFontPixelSize READ captionFontPixelSize NOTIFY appearanceChanged)
    Q_PROPERTY(int longFormFontPixelSize READ longFormFontPixelSize NOTIFY appearanceChanged)
    Q_PROPERTY(qreal longFormLineHeight READ longFormLineHeight NOTIFY appearanceChanged)
    Q_PROPERTY(int longFormMeasure READ longFormMeasure NOTIFY appearanceChanged)

    // Navigation preferences share the same versioned settings object and
    // file as appearance. This prevents two independent writers from
    // clobbering one another while keeping the filesystem core Qt-free.
    Q_PROPERTY(QVariantList places READ places NOTIFY navigationSettingsChanged)
    Q_PROPERTY(
        QStringList recentDestinations READ recentDestinations NOTIFY navigationSettingsChanged)
    Q_PROPERTY(bool dualPaneEnabled READ dualPaneEnabled WRITE setDualPaneEnabled NOTIFY
                   navigationSettingsChanged)
    Q_PROPERTY(
        qreal splitRatio READ splitRatio WRITE setSplitRatio NOTIFY navigationSettingsChanged)

    // Color roles, resolved from the active family and the high-contrast
    // override.
    Q_PROPERTY(QColor background READ background NOTIFY appearanceChanged)
    Q_PROPERTY(QColor backgroundDeep READ backgroundDeep NOTIFY appearanceChanged)
    Q_PROPERTY(QColor well READ well NOTIFY appearanceChanged)
    Q_PROPERTY(QColor panel READ panel NOTIFY appearanceChanged)
    Q_PROPERTY(QColor border READ border NOTIFY appearanceChanged)
    Q_PROPERTY(QColor text READ text NOTIFY appearanceChanged)
    Q_PROPERTY(QColor longFormInk READ longFormInk NOTIFY appearanceChanged)
    Q_PROPERTY(QColor iconInk READ iconInk NOTIFY appearanceChanged)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY appearanceChanged)
    Q_PROPERTY(QColor textFaint READ textFaint NOTIFY appearanceChanged)
    Q_PROPERTY(QColor dirInk READ dirInk NOTIFY appearanceChanged)
    Q_PROPERTY(QColor linkInk READ linkInk NOTIFY appearanceChanged)
    Q_PROPERTY(QColor metaInk READ metaInk NOTIFY appearanceChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY appearanceChanged)
    Q_PROPERTY(QColor selectionBed READ selectionBed NOTIFY appearanceChanged)
    Q_PROPERTY(QColor selectionInk READ selectionInk NOTIFY appearanceChanged)
    Q_PROPERTY(QColor matchBed READ matchBed NOTIFY appearanceChanged)
    Q_PROPERTY(QColor focus READ focus NOTIFY appearanceChanged)
    Q_PROPERTY(QColor hover READ hover NOTIFY appearanceChanged)
    Q_PROPERTY(QColor pressed READ pressed NOTIFY appearanceChanged)
    Q_PROPERTY(QColor rubberBand READ rubberBand NOTIFY appearanceChanged)
    Q_PROPERTY(QColor danger READ danger NOTIFY appearanceChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY appearanceChanged)
    Q_PROPERTY(QColor success READ success NOTIFY appearanceChanged)

  public:
    /// Mirrors `core::EffectProfile`; declared here so QML can name values as
    /// `ShellTheme.Balanced`.
    enum Profile { Off, Minimal, Balanced, Strong, Custom };
    Q_ENUM(Profile)

    /// Mirrors `core::FontSource`.
    enum Source { Bundled, System, Named };
    Q_ENUM(Source)

    /// Mirrors `core::Density`.
    enum Densities { Compact, Cozy, Comfortable };
    Q_ENUM(Densities)

    explicit ThemeController(QObject* parent = nullptr);

    [[nodiscard]] QString storagePath() const;
    void setStoragePath(const QString& path);

    [[nodiscard]] QStringList availablePalettes() const;
    [[nodiscard]] QString paletteId() const;
    void setPaletteId(const QString& id);
    [[nodiscard]] bool lightPalette() const;
    [[nodiscard]] static QVariantList accentPresets();
    [[nodiscard]] QString accentPresetId() const;
    void setAccentPresetId(const QString& id);
    [[nodiscard]] int accentPresetIndex() const;
    void setAccentPresetIndex(int index);

    [[nodiscard]] Profile profile() const;
    void setProfile(Profile profile);

    [[nodiscard]] Source fontSource() const;
    void setFontSource(Source source);
    [[nodiscard]] QString namedFontFamily() const;
    void setNamedFontFamily(const QString& family);
    [[nodiscard]] bool bundledFontAvailable() const;
    [[nodiscard]] QString contentFontFamily() const;
    [[nodiscard]] QString chromeFontFamily() const;
    [[nodiscard]] QString pathFontFamily() const;
    [[nodiscard]] QString captionFontFamily() const;
    [[nodiscard]] QString longFormFontFamily() const;

    [[nodiscard]] Densities density() const;
    void setDensity(Densities density);
    [[nodiscard]] qreal uiScale() const;
    void setUiScale(qreal scale);

    [[nodiscard]] qreal glassOpacity() const;
    void setGlassOpacity(qreal opacity);
    [[nodiscard]] qreal surfaceOpacity() const;
    void setSurfaceOpacity(qreal opacity);
    [[nodiscard]] bool reducedMotion() const;
    void setReducedMotion(bool reduced);
    [[nodiscard]] bool highContrast() const;
    void setHighContrast(bool high);

    [[nodiscard]] qreal bloomCore() const;
    void setBloomCore(qreal value);
    [[nodiscard]] qreal bloomWide() const;
    void setBloomWide(qreal value);
    [[nodiscard]] qreal scanline() const;
    void setScanline(qreal value);
    [[nodiscard]] qreal vignette() const;
    void setVignette(qreal value);
    [[nodiscard]] qreal persistence() const;
    void setPersistence(qreal value);
    [[nodiscard]] qreal deepField() const;
    void setDeepField(qreal value);
    [[nodiscard]] qreal textLift() const;
    void setTextLift(qreal value);

    [[nodiscard]] qreal effectiveBloomCore() const;
    [[nodiscard]] qreal effectiveBloomWide() const;
    [[nodiscard]] qreal effectiveScanline() const;
    [[nodiscard]] qreal effectiveVignette() const;
    [[nodiscard]] qreal effectivePersistence() const;
    [[nodiscard]] qreal effectiveDeepField() const;
    [[nodiscard]] qreal effectiveTextLift() const;

    [[nodiscard]] int rowHeight() const;
    [[nodiscard]] int gridCellWidth() const;
    [[nodiscard]] int gridCellHeight() const;
    [[nodiscard]] int chromeFontPixelSize() const;
    [[nodiscard]] int contentFontPixelSize() const;
    [[nodiscard]] int pathFontPixelSize() const;
    [[nodiscard]] int captionFontPixelSize() const;
    [[nodiscard]] int longFormFontPixelSize() const;
    [[nodiscard]] qreal longFormLineHeight() const;
    [[nodiscard]] int longFormMeasure() const;

    [[nodiscard]] QVariantList places() const;
    [[nodiscard]] QStringList recentDestinations() const;
    Q_INVOKABLE bool addPlace(const QVariantMap& place);
    Q_INVOKABLE bool removePlace(int index);
    Q_INVOKABLE bool movePlace(int from, int to);
    Q_INVOKABLE bool recordRecentDestination(const QString& path);
    Q_INVOKABLE bool clearRecentDestinations();
    [[nodiscard]] bool dualPaneEnabled() const noexcept;
    void setDualPaneEnabled(bool enabled);
    [[nodiscard]] qreal splitRatio() const noexcept;
    void setSplitRatio(qreal ratio);

    [[nodiscard]] QColor background() const;
    [[nodiscard]] QColor backgroundDeep() const;
    [[nodiscard]] QColor well() const;
    [[nodiscard]] QColor panel() const;
    [[nodiscard]] QColor border() const;
    [[nodiscard]] QColor text() const;
    [[nodiscard]] QColor longFormInk() const;
    [[nodiscard]] QColor iconInk() const;
    [[nodiscard]] QColor textMuted() const;
    [[nodiscard]] QColor textFaint() const;
    [[nodiscard]] QColor dirInk() const;
    [[nodiscard]] QColor linkInk() const;
    [[nodiscard]] QColor metaInk() const;
    [[nodiscard]] QColor accent() const;
    [[nodiscard]] QColor selectionBed() const;
    [[nodiscard]] QColor selectionInk() const;
    [[nodiscard]] QColor matchBed() const;
    [[nodiscard]] QColor focus() const;
    [[nodiscard]] QColor hover() const;
    [[nodiscard]] QColor pressed() const;
    [[nodiscard]] QColor rubberBand() const;
    [[nodiscard]] QColor danger() const;
    [[nodiscard]] QColor warning() const;
    [[nodiscard]] QColor success() const;

    /// Restores the shipped configuration and persists it. The settings file
    /// is rewritten even when the live state already equals the defaults, so
    /// resetting also repairs a damaged file that parsed to the defaults.
    Q_INVOKABLE void resetToDefaults();

  signals:
    void appearanceChanged();
    void navigationSettingsChanged();
    void storagePathChanged();

  private:
    /// Runs `change` against the settings, clamps, persists, and notifies.
    template <typename Change>
    void mutate(Change&& change);
    void persist();
    [[nodiscard]] const struct ShellPalette& activePalette() const;
    [[nodiscard]] const struct AccentPreset& activeAccentPreset() const;
    [[nodiscard]] QColor resolvedAccent() const;
    [[nodiscard]] core::EffectLevels stored() const;
    [[nodiscard]] core::EffectLevels effective() const;
    /// Applies the effective text lift to a chromatic ink: multiplies the
    /// channels toward white, clamped. Identity at lift one.
    [[nodiscard]] QColor lifted(const QColor& ink) const;
    [[nodiscard]] qreal metricScale() const;

    core::AppearanceSettings settings_;
    QString storagePath_;
};

} // namespace odysea::app

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

    Q_PROPERTY(Profile profile READ profile WRITE setProfile NOTIFY appearanceChanged)
    Q_PROPERTY(Source fontSource READ fontSource WRITE setFontSource NOTIFY appearanceChanged)
    Q_PROPERTY(QString namedFontFamily READ namedFontFamily WRITE setNamedFontFamily NOTIFY
                   appearanceChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY appearanceChanged)
    Q_PROPERTY(Densities density READ density WRITE setDensity NOTIFY appearanceChanged)
    Q_PROPERTY(qreal uiScale READ uiScale WRITE setUiScale NOTIFY appearanceChanged)

    Q_PROPERTY(qreal glassOpacity READ glassOpacity WRITE setGlassOpacity NOTIFY appearanceChanged)
    Q_PROPERTY(
        qreal surfaceOpacity READ surfaceOpacity WRITE setSurfaceOpacity NOTIFY appearanceChanged)
    Q_PROPERTY(
        bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY appearanceChanged)
    Q_PROPERTY(bool highContrast READ highContrast WRITE setHighContrast NOTIFY appearanceChanged)

    // Effect levels. Reads give the levels the active profile renders; writes
    // adjust the custom profile and make it active, which is what a user
    // dragging a slider means.
    Q_PROPERTY(qreal bloomCore READ bloomCore WRITE setBloomCore NOTIFY appearanceChanged)
    Q_PROPERTY(qreal bloomWide READ bloomWide WRITE setBloomWide NOTIFY appearanceChanged)
    Q_PROPERTY(qreal scanline READ scanline WRITE setScanline NOTIFY appearanceChanged)
    Q_PROPERTY(qreal vignette READ vignette WRITE setVignette NOTIFY appearanceChanged)
    Q_PROPERTY(qreal persistence READ persistence WRITE setPersistence NOTIFY appearanceChanged)
    Q_PROPERTY(qreal deepField READ deepField WRITE setDeepField NOTIFY appearanceChanged)
    Q_PROPERTY(qreal textLift READ textLift WRITE setTextLift NOTIFY appearanceChanged)

    // Metrics derived from density and scale.
    Q_PROPERTY(int rowHeight READ rowHeight NOTIFY appearanceChanged)
    Q_PROPERTY(int gridCellWidth READ gridCellWidth NOTIFY appearanceChanged)
    Q_PROPERTY(int gridCellHeight READ gridCellHeight NOTIFY appearanceChanged)
    Q_PROPERTY(int fontPixelSize READ fontPixelSize NOTIFY appearanceChanged)
    Q_PROPERTY(int contentFontPixelSize READ contentFontPixelSize NOTIFY appearanceChanged)
    Q_PROPERTY(int metaFontPixelSize READ metaFontPixelSize NOTIFY appearanceChanged)

    // Color roles, resolved from the active family and the high-contrast
    // override.
    Q_PROPERTY(QColor background READ background NOTIFY appearanceChanged)
    Q_PROPERTY(QColor backgroundDeep READ backgroundDeep NOTIFY appearanceChanged)
    Q_PROPERTY(QColor well READ well NOTIFY appearanceChanged)
    Q_PROPERTY(QColor panel READ panel NOTIFY appearanceChanged)
    Q_PROPERTY(QColor border READ border NOTIFY appearanceChanged)
    Q_PROPERTY(QColor text READ text NOTIFY appearanceChanged)
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

    [[nodiscard]] Profile profile() const;
    void setProfile(Profile profile);

    [[nodiscard]] Source fontSource() const;
    void setFontSource(Source source);
    [[nodiscard]] QString namedFontFamily() const;
    void setNamedFontFamily(const QString& family);
    [[nodiscard]] QString fontFamily() const;

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

    [[nodiscard]] int rowHeight() const;
    [[nodiscard]] int gridCellWidth() const;
    [[nodiscard]] int gridCellHeight() const;
    [[nodiscard]] int fontPixelSize() const;
    [[nodiscard]] int contentFontPixelSize() const;
    [[nodiscard]] int metaFontPixelSize() const;

    [[nodiscard]] QColor background() const;
    [[nodiscard]] QColor backgroundDeep() const;
    [[nodiscard]] QColor well() const;
    [[nodiscard]] QColor panel() const;
    [[nodiscard]] QColor border() const;
    [[nodiscard]] QColor text() const;
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

    /// Restores the shipped configuration and persists it.
    Q_INVOKABLE void resetToDefaults();

  signals:
    void appearanceChanged();
    void storagePathChanged();

  private:
    /// Runs `change` against the settings, clamps, persists, and notifies.
    template <typename Change>
    void mutate(Change&& change);
    void persist();
    [[nodiscard]] const struct ShellPalette& activePalette() const;
    [[nodiscard]] core::EffectLevels effective() const;
    [[nodiscard]] qreal metricScale() const;

    core::AppearanceSettings settings_;
    QString storagePath_;
};

} // namespace odysea::app

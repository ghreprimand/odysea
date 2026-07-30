// The shell's appearance state, live and persistent.
//
// See the header for the contract. State validity, profile semantics, and the
// persisted form belong to the core appearance model; this class binds that
// model to Qt types, resolves fonts against what the platform actually has,
// and turns the active color family into concrete rendering roles.
#include "theme_controller.hpp"

#include "theme_palettes.hpp"

#include <QFontDatabase>

#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace odysea::app {

namespace {

/// Base row height per density; the cozy value is the shell's original metric.
[[nodiscard]] int densityRowHeight(core::Density density) noexcept {
    switch (density) {
    case core::Density::Compact:
        return 28;
    case core::Density::Comfortable:
        return 40;
    case core::Density::Cozy:
        break;
    }
    return 34;
}

[[nodiscard]] int scaled(int base, qreal factor) noexcept {
    return std::max(1, static_cast<int>(std::lround(base * factor)));
}

/// The preferred fixed-width families, tried in order. The first entry is the
/// bundled default face; the rest keep metrics stable when it is absent.
[[nodiscard]] QStringList bundledFontCandidates() {
    return {QStringLiteral("Victor Mono"), QStringLiteral("JetBrains Mono"),
            QStringLiteral("Source Code Pro"), QStringLiteral("DejaVu Sans Mono")};
}

[[nodiscard]] QString firstAvailableFamily(const QStringList& candidates) {
    for (const QString& family : candidates) {
        if (QFontDatabase::hasFamily(family)) {
            return family;
        }
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

} // namespace

ThemeController::ThemeController(QObject* parent) : QObject(parent) {
    settings_.palette = defaultShellPaletteId().toStdString();
}

QString ThemeController::storagePath() const {
    return storagePath_;
}

void ThemeController::setStoragePath(const QString& path) {
    if (storagePath_ == path) {
        return;
    }
    storagePath_ = path;
    if (!storagePath_.isEmpty()) {
        std::error_code ec;
        settings_ = core::load_appearance(std::filesystem::path(storagePath_.toStdString()), ec);
        // A read failure keeps the shipped defaults; the next change writes a
        // fresh file. Appearance state is never worth failing startup over.
        emit appearanceChanged();
    }
    emit storagePathChanged();
}

QStringList ThemeController::availablePalettes() const {
    return shellPaletteIds();
}

const ShellPalette& ThemeController::activePalette() const {
    // The lookup key is materialized as an lvalue: the returned reference has
    // static storage, and an lvalue argument keeps that visible to compilers.
    const QString id = QString::fromStdString(settings_.palette);
    return shellPalette(id);
}

QString ThemeController::paletteId() const {
    return activePalette().id;
}

void ThemeController::setPaletteId(const QString& id) {
    const QString resolved = shellPalette(id).id;
    if (paletteId() == resolved) {
        return;
    }
    mutate([&](core::AppearanceSettings& s) { s.palette = resolved.toStdString(); });
}

bool ThemeController::lightPalette() const {
    return activePalette().light;
}

ThemeController::Profile ThemeController::profile() const {
    return static_cast<Profile>(settings_.profile);
}

void ThemeController::setProfile(Profile profile) {
    if (this->profile() == profile) {
        return;
    }
    mutate([&](core::AppearanceSettings& s) {
        s.profile = static_cast<core::EffectProfile>(profile);
    });
}

ThemeController::Source ThemeController::fontSource() const {
    return static_cast<Source>(settings_.font_source);
}

void ThemeController::setFontSource(Source source) {
    if (fontSource() == source) {
        return;
    }
    mutate([&](core::AppearanceSettings& s) {
        s.font_source = static_cast<core::FontSource>(source);
    });
}

QString ThemeController::namedFontFamily() const {
    return QString::fromStdString(settings_.font_family);
}

void ThemeController::setNamedFontFamily(const QString& family) {
    if (namedFontFamily() == family) {
        return;
    }
    mutate([&](core::AppearanceSettings& s) { s.font_family = family.toStdString(); });
}

QString ThemeController::fontFamily() const {
    switch (settings_.font_source) {
    case core::FontSource::System:
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    case core::FontSource::Named: {
        const QString named = namedFontFamily();
        if (!named.isEmpty() && QFontDatabase::hasFamily(named)) {
            return named;
        }
        return firstAvailableFamily(bundledFontCandidates());
    }
    case core::FontSource::Bundled:
        break;
    }
    return firstAvailableFamily(bundledFontCandidates());
}

ThemeController::Densities ThemeController::density() const {
    return static_cast<Densities>(settings_.density);
}

void ThemeController::setDensity(Densities density) {
    if (this->density() == density) {
        return;
    }
    mutate([&](core::AppearanceSettings& s) { s.density = static_cast<core::Density>(density); });
}

qreal ThemeController::uiScale() const {
    return settings_.scale;
}

void ThemeController::setUiScale(qreal scale) {
    mutate([&](core::AppearanceSettings& s) { s.scale = scale; });
}

qreal ThemeController::glassOpacity() const {
    return settings_.glass_opacity;
}

void ThemeController::setGlassOpacity(qreal opacity) {
    mutate([&](core::AppearanceSettings& s) { s.glass_opacity = opacity; });
}

qreal ThemeController::surfaceOpacity() const {
    return settings_.surface_opacity;
}

void ThemeController::setSurfaceOpacity(qreal opacity) {
    mutate([&](core::AppearanceSettings& s) { s.surface_opacity = opacity; });
}

bool ThemeController::reducedMotion() const {
    return settings_.reduced_motion;
}

void ThemeController::setReducedMotion(bool reduced) {
    if (settings_.reduced_motion == reduced) {
        return;
    }
    mutate([&](core::AppearanceSettings& s) { s.reduced_motion = reduced; });
}

bool ThemeController::highContrast() const {
    return settings_.high_contrast;
}

void ThemeController::setHighContrast(bool high) {
    if (settings_.high_contrast == high) {
        return;
    }
    mutate([&](core::AppearanceSettings& s) { s.high_contrast = high; });
}

core::EffectLevels ThemeController::stored() const {
    // The stored view: what the active profile says, before the accessibility
    // overrides. This is what the controls display and edit, so an override
    // never makes an enabled slider discard or misreport a write.
    return settings_.profile == core::EffectProfile::Custom
               ? core::clamp_effect_levels(settings_.custom)
               : core::effect_profile_levels(settings_.profile);
}

core::EffectLevels ThemeController::effective() const {
    return core::effective_effect_levels(settings_);
}

qreal ThemeController::bloomCore() const {
    return stored().bloom_core;
}

void ThemeController::setBloomCore(qreal value) {
    mutate([&](core::AppearanceSettings& s) {
        s.custom.bloom_core = value;
        s.profile = core::EffectProfile::Custom;
    });
}

qreal ThemeController::bloomWide() const {
    return stored().bloom_wide;
}

void ThemeController::setBloomWide(qreal value) {
    mutate([&](core::AppearanceSettings& s) {
        s.custom.bloom_wide = value;
        s.profile = core::EffectProfile::Custom;
    });
}

qreal ThemeController::scanline() const {
    return stored().scanline;
}

void ThemeController::setScanline(qreal value) {
    mutate([&](core::AppearanceSettings& s) {
        s.custom.scanline = value;
        s.profile = core::EffectProfile::Custom;
    });
}

qreal ThemeController::vignette() const {
    return stored().vignette;
}

void ThemeController::setVignette(qreal value) {
    mutate([&](core::AppearanceSettings& s) {
        s.custom.vignette = value;
        s.profile = core::EffectProfile::Custom;
    });
}

qreal ThemeController::persistence() const {
    return stored().persistence;
}

void ThemeController::setPersistence(qreal value) {
    mutate([&](core::AppearanceSettings& s) {
        s.custom.persistence = value;
        s.profile = core::EffectProfile::Custom;
    });
}

qreal ThemeController::deepField() const {
    return stored().deep_field;
}

void ThemeController::setDeepField(qreal value) {
    mutate([&](core::AppearanceSettings& s) {
        s.custom.deep_field = value;
        s.profile = core::EffectProfile::Custom;
    });
}

qreal ThemeController::textLift() const {
    return stored().text_lift;
}

void ThemeController::setTextLift(qreal value) {
    mutate([&](core::AppearanceSettings& s) {
        s.custom.text_lift = value;
        s.profile = core::EffectProfile::Custom;
    });
}

qreal ThemeController::effectiveBloomCore() const {
    return effective().bloom_core;
}

qreal ThemeController::effectiveBloomWide() const {
    return effective().bloom_wide;
}

qreal ThemeController::effectiveScanline() const {
    return effective().scanline;
}

qreal ThemeController::effectiveVignette() const {
    return effective().vignette;
}

qreal ThemeController::effectivePersistence() const {
    return effective().persistence;
}

qreal ThemeController::effectiveDeepField() const {
    return effective().deep_field;
}

qreal ThemeController::effectiveTextLift() const {
    return effective().text_lift;
}

qreal ThemeController::metricScale() const {
    return settings_.scale * (densityRowHeight(settings_.density) / 34.0);
}

int ThemeController::rowHeight() const {
    return scaled(densityRowHeight(settings_.density), settings_.scale);
}

int ThemeController::gridCellWidth() const {
    return scaled(144, metricScale());
}

int ThemeController::gridCellHeight() const {
    return scaled(154, metricScale());
}

int ThemeController::fontPixelSize() const {
    return scaled(13, settings_.scale);
}

int ThemeController::contentFontPixelSize() const {
    return scaled(14, settings_.scale);
}

int ThemeController::metaFontPixelSize() const {
    return scaled(12, settings_.scale);
}

QColor ThemeController::background() const {
    return activePalette().sheet;
}

QColor ThemeController::backgroundDeep() const {
    return activePalette().deep;
}

QColor ThemeController::well() const {
    return activePalette().well;
}

QColor ThemeController::panel() const {
    return activePalette().inset;
}

QColor ThemeController::border() const {
    const ShellPalette& p = activePalette();
    // High contrast promotes hairlines to the faint ink, which every family
    // guarantees to be legible against its grounds.
    return settings_.high_contrast ? p.faint : p.frame;
}

QColor ThemeController::text() const {
    return activePalette().text;
}

QColor ThemeController::textMuted() const {
    const ShellPalette& p = activePalette();
    return settings_.high_contrast ? p.text : p.muted;
}

QColor ThemeController::textFaint() const {
    const ShellPalette& p = activePalette();
    return settings_.high_contrast ? p.muted : p.faint;
}

QColor ThemeController::dirInk() const {
    return activePalette().dir;
}

QColor ThemeController::linkInk() const {
    return activePalette().link;
}

QColor ThemeController::metaInk() const {
    const ShellPalette& p = activePalette();
    return settings_.high_contrast ? p.text : p.meta;
}

QColor ThemeController::accent() const {
    return activePalette().accent;
}

QColor ThemeController::selectionBed() const {
    return activePalette().selectionBed;
}

QColor ThemeController::selectionInk() const {
    return activePalette().selectionInk;
}

QColor ThemeController::matchBed() const {
    return activePalette().match;
}

QColor ThemeController::focus() const {
    return activePalette().focus;
}

QColor ThemeController::hover() const {
    const ShellPalette& p = activePalette();
    return p.light ? p.inset.darker(106) : p.inset.lighter(135);
}

QColor ThemeController::pressed() const {
    const ShellPalette& p = activePalette();
    return p.light ? p.inset.darker(114) : p.inset.lighter(165);
}

QColor ThemeController::rubberBand() const {
    QColor band = focus();
    band.setAlphaF(0.20F);
    return band;
}

QColor ThemeController::danger() const {
    return activePalette().danger;
}

QColor ThemeController::warning() const {
    return activePalette().warning;
}

QColor ThemeController::success() const {
    return activePalette().success;
}

void ThemeController::resetToDefaults() {
    core::AppearanceSettings defaults;
    const bool changed = !(settings_ == defaults);
    settings_ = defaults;
    // The write is unconditional: a damaged settings file parses to the
    // defaults, so the live state can already equal them while the file on
    // disk is still wrong. Reset promises a clean file either way.
    persist();
    if (changed) {
        emit appearanceChanged();
    }
}

template <typename Change>
void ThemeController::mutate(Change&& change) {
    core::AppearanceSettings next = settings_;
    std::forward<Change>(change)(next);
    next = core::clamp_appearance(next);
    if (next == settings_) {
        return;
    }
    settings_ = std::move(next);
    persist();
    emit appearanceChanged();
}

void ThemeController::persist() {
    if (storagePath_.isEmpty()) {
        return;
    }
    std::error_code ec;
    core::save_appearance(std::filesystem::path(storagePath_.toStdString()), settings_, ec);
    // A failed write leaves the live state authoritative; the next successful
    // save catches up. Appearance persistence must never interrupt use.
}

} // namespace odysea::app

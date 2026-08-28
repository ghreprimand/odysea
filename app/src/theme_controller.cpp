// The shell's appearance state, live and persistent.
//
// See the header for the contract. State validity, profile semantics, and the
// persisted form belong to the core appearance model; this class binds that
// model to Qt types, resolves fonts against what the platform actually has,
// and turns the active color family into concrete rendering roles.
#include "theme_controller.hpp"

#include "theme_contrast.hpp"
#include "theme_palettes.hpp"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QList>
#include <QVariantMap>

#include <algorithm>
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

[[nodiscard]] int densityTypeOffset(core::Density density) noexcept {
    switch (density) {
    case core::Density::Compact:
        return -1;
    case core::Density::Comfortable:
        return 1;
    case core::Density::Cozy:
        break;
    }
    return 0;
}

[[nodiscard]] int densityLongFormMeasure(core::Density density) noexcept {
    switch (density) {
    case core::Density::Compact:
        return 520;
    case core::Density::Comfortable:
        return 600;
    case core::Density::Cozy:
        break;
    }
    return 560;
}

[[nodiscard]] int scaled(int base, qreal factor) noexcept {
    return std::max(1, static_cast<int>(std::lround(base * factor)));
}

[[nodiscard]] QString cleanNavigationPath(const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() || !QDir::isAbsolutePath(trimmed)) {
        return {};
    }
    return QDir::cleanPath(trimmed);
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

struct BundledFontRegistration {
    bool available{false};
    QString family;
};

[[nodiscard]] BundledFontRegistration registerBundledFonts() {
    static const QString family = QStringLiteral("Victor Mono");
    static const QStringList resources{
        QStringLiteral(":/qt/qml/OdySea/third_party/victor-mono/VictorMono-Regular.otf"),
        QStringLiteral(":/qt/qml/OdySea/third_party/victor-mono/VictorMono-Italic.otf"),
        QStringLiteral(":/qt/qml/OdySea/third_party/victor-mono/VictorMono-Bold.otf"),
        QStringLiteral(":/qt/qml/OdySea/third_party/victor-mono/VictorMono-BoldItalic.otf")};

    QList<int> registeredIds;
    registeredIds.reserve(resources.size());
    for (const QString& resource : resources) {
        const int id = QFontDatabase::addApplicationFont(resource);
        if (id < 0 || !QFontDatabase::applicationFontFamilies(id).contains(family)) {
            if (id >= 0) {
                QFontDatabase::removeApplicationFont(id);
            }
            for (const int registeredId : registeredIds) {
                QFontDatabase::removeApplicationFont(registeredId);
            }
            return {};
        }
        registeredIds.push_back(id);
    }
    return {.available = true, .family = family};
}

[[nodiscard]] const BundledFontRegistration& bundledFontRegistration() {
    static const BundledFontRegistration registration = registerBundledFonts();
    return registration;
}

[[nodiscard]] QColor restingHover(const ShellPalette& palette) {
    return palette.light ? palette.inset.darker(106) : palette.inset.lighter(135);
}

[[nodiscard]] QColor restingPressed(const ShellPalette& palette) {
    return palette.light ? palette.inset.darker(114) : palette.inset.lighter(165);
}

} // namespace

ThemeController::ThemeController(QObject* parent) : QObject(parent) {
    // Registration is process-wide and idempotent. Keeping it here means the
    // application and every module-linked scene test resolve the same face.
    static_cast<void>(bundledFontRegistration());
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
        emit navigationSettingsChanged();
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

const AccentPreset& ThemeController::activeAccentPreset() const {
    const QString id = QString::fromStdString(settings_.accent_preset);
    return shellAccentPreset(id);
}

QColor ThemeController::resolvedAccent() const {
    if (accentPresetId() == defaultAccentPresetId()) {
        return activePalette().accent;
    }
    return activeAccentPreset().accent;
}

QVariantList ThemeController::accentPresets() {
    QVariantList result;
    const QList<AccentPreset> presets = shellAccentPresets();
    result.reserve(presets.size());
    for (const AccentPreset& preset : presets) {
        result.append(QVariantMap{{QStringLiteral("id"), preset.id},
                                  {QStringLiteral("name"), preset.name},
                                  {QStringLiteral("color"), preset.accent}});
    }
    return result;
}

QString ThemeController::accentPresetId() const {
    return activeAccentPreset().id;
}

void ThemeController::setAccentPresetId(const QString& id) {
    const QString resolved = shellAccentPreset(id).id;
    if (accentPresetId() == resolved) {
        return;
    }
    mutate([&](core::AppearanceSettings& s) { s.accent_preset = resolved.toStdString(); });
}

int ThemeController::accentPresetIndex() const {
    const QList<AccentPreset> presets = shellAccentPresets();
    for (int index = 0; index < presets.size(); ++index) {
        if (presets.at(index).id == accentPresetId()) {
            return index;
        }
    }
    return 0;
}

void ThemeController::setAccentPresetIndex(int index) {
    const QList<AccentPreset> presets = shellAccentPresets();
    if (index < 0 || index >= presets.size()) {
        return;
    }
    setAccentPresetId(presets.at(index).id);
}

QString ThemeController::accentContrastWarning() const {
    const QList<ThemeContrastSample> samples{{.role = QStringLiteral("Accent"),
                                              .renderSite = QStringLiteral("window ground"),
                                              .foreground = accent(),
                                              .background = background(),
                                              .floor = 3.0},
                                             {.role = QStringLiteral("Accent"),
                                              .renderSite = QStringLiteral("selected entry"),
                                              .foreground = accent(),
                                              .background = selectionBed(),
                                              .floor = 3.0},
                                             {.role = QStringLiteral("Accent"),
                                              .renderSite = QStringLiteral("hovered surface"),
                                              .foreground = accent(),
                                              .background = hover(),
                                              .floor = 3.0},
                                             {.role = QStringLiteral("Accent"),
                                              .renderSite = QStringLiteral("pressed surface"),
                                              .foreground = accent(),
                                              .background = pressed(),
                                              .floor = 3.0},
                                             {.role = QStringLiteral("Accent"),
                                              .renderSite = QStringLiteral("panel"),
                                              .foreground = accent(),
                                              .background = panel(),
                                              .floor = 3.0}};
    const QStringList failures = themeContrastFailures(samples);
    if (failures.isEmpty()) {
        return {};
    }
    return QStringLiteral("Accent contrast needs attention: %1")
        .arg(failures.join(QStringLiteral("; ")));
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

QString ThemeController::contentFontFamily() const {
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
    case core::FontSource::Bundled: {
        const BundledFontRegistration& bundled = bundledFontRegistration();
        if (bundled.available) {
            return bundled.family;
        }
        break;
    }
    }
    return firstAvailableFamily(bundledFontCandidates());
}

bool ThemeController::bundledFontAvailable() const {
    return bundledFontRegistration().available;
}

QString ThemeController::chromeFontFamily() const {
    return contentFontFamily();
}

QString ThemeController::pathFontFamily() const {
    return contentFontFamily();
}

QString ThemeController::captionFontFamily() const {
    return contentFontFamily();
}

QString ThemeController::longFormFontFamily() const {
    return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
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

int ThemeController::chromeFontPixelSize() const {
    return scaled(13 + densityTypeOffset(settings_.density), settings_.scale);
}

int ThemeController::contentFontPixelSize() const {
    return scaled(14 + densityTypeOffset(settings_.density), settings_.scale);
}

int ThemeController::pathFontPixelSize() const {
    return scaled(13 + densityTypeOffset(settings_.density), settings_.scale);
}

int ThemeController::captionFontPixelSize() const {
    return scaled(12 + densityTypeOffset(settings_.density), settings_.scale);
}

int ThemeController::longFormFontPixelSize() const {
    return scaled(15 + densityTypeOffset(settings_.density), settings_.scale);
}

qreal ThemeController::longFormLineHeight() const {
    return 1.45;
}

int ThemeController::longFormMeasure() const {
    return scaled(densityLongFormMeasure(settings_.density), settings_.scale);
}

QVariantList ThemeController::places() const {
    QVariantList result;
    result.reserve(static_cast<qsizetype>(settings_.places.size()));
    for (const core::NavigationPlace& place : settings_.places) {
        QVariantMap value;
        value.insert(QStringLiteral("label"), QString::fromStdString(place.label));
        value.insert(QStringLiteral("path"), QString::fromStdString(place.path));
        result.push_back(value);
    }
    return result;
}

QStringList ThemeController::recentDestinations() const {
    QStringList result;
    result.reserve(static_cast<qsizetype>(settings_.recent_destinations.size()));
    for (const std::string& path : settings_.recent_destinations) {
        result.push_back(QString::fromStdString(path));
    }
    return result;
}

bool ThemeController::addPlace(const QVariantMap& place) {
    const QString cleanPath = cleanNavigationPath(place.value(QStringLiteral("path")).toString());
    if (cleanPath.isEmpty()) {
        return false;
    }
    const std::string storedPath = cleanPath.toStdString();
    if (std::ranges::any_of(settings_.places, [&storedPath](const core::NavigationPlace& stored) {
            return stored.path == storedPath;
        })) {
        return false;
    }

    QString cleanLabel = place.value(QStringLiteral("label")).toString().trimmed();
    if (cleanLabel.isEmpty()) {
        cleanLabel = QFileInfo(cleanPath).fileName();
        if (cleanLabel.isEmpty()) {
            cleanLabel = tr("Filesystem");
        }
    }
    core::AppearanceSettings next = settings_;
    next.places.push_back(
        core::NavigationPlace{.label = cleanLabel.toStdString(), .path = storedPath});
    next = core::clamp_appearance(next);
    if (next == settings_) {
        return false;
    }
    settings_ = std::move(next);
    persist();
    emit navigationSettingsChanged();
    return true;
}

bool ThemeController::removePlace(int index) {
    if (index < 0 || std::cmp_greater_equal(index, settings_.places.size())) {
        return false;
    }
    settings_.places.erase(settings_.places.begin() + index);
    persist();
    emit navigationSettingsChanged();
    return true;
}

bool ThemeController::movePlace(int from, int to) {
    const int count = static_cast<int>(settings_.places.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return false;
    }
    core::NavigationPlace place = std::move(settings_.places.at(static_cast<std::size_t>(from)));
    settings_.places.erase(settings_.places.begin() + from);
    settings_.places.insert(settings_.places.begin() + to, std::move(place));
    persist();
    emit navigationSettingsChanged();
    return true;
}

bool ThemeController::recordRecentDestination(const QString& path) {
    const QString cleanPath = cleanNavigationPath(path);
    if (cleanPath.isEmpty()) {
        return false;
    }
    const std::string storedPath = cleanPath.toStdString();
    core::AppearanceSettings next = settings_;
    std::erase(next.recent_destinations, storedPath);
    next.recent_destinations.insert(next.recent_destinations.begin(), storedPath);
    next = core::clamp_appearance(next);
    if (next == settings_) {
        return false;
    }
    settings_ = std::move(next);
    persist();
    emit navigationSettingsChanged();
    return true;
}

bool ThemeController::clearRecentDestinations() {
    if (settings_.recent_destinations.empty()) {
        return false;
    }
    settings_.recent_destinations.clear();
    persist();
    emit navigationSettingsChanged();
    return true;
}

bool ThemeController::dualPaneEnabled() const noexcept {
    return settings_.dual_pane_enabled;
}

void ThemeController::setDualPaneEnabled(bool enabled) {
    if (settings_.dual_pane_enabled == enabled) {
        return;
    }
    settings_.dual_pane_enabled = enabled;
    persist();
    emit navigationSettingsChanged();
}

qreal ThemeController::splitRatio() const noexcept {
    return settings_.split_ratio;
}

void ThemeController::setSplitRatio(qreal ratio) {
    core::AppearanceSettings next = settings_;
    next.split_ratio = ratio;
    next = core::clamp_appearance(next);
    if (next.split_ratio == settings_.split_ratio) {
        return;
    }
    settings_.split_ratio = next.split_ratio;
    persist();
    emit navigationSettingsChanged();
}

QColor ThemeController::lifted(const QColor& ink) const {
    // Text lift is the palette-side half of the presentation pipeline: it
    // multiplies chromatic inks toward white, which both brightens them and
    // pushes them over the bloom threshold. Neutral body text is exempt so
    // the lift changes emphasis, not legibility. The effective level already
    // folds in the accessibility overrides — high contrast pins it to one.
    //
    // Light families are exempt entirely: their inks are dark marks on
    // bright grounds, so multiplying toward white lowers their contrast
    // instead of raising their emphasis — the emissive metaphor only exists
    // on dark grounds.
    if (activePalette().light) {
        return ink;
    }
    const float lift = static_cast<float>(effective().text_lift);
    if (lift <= 1.0F) {
        return ink;
    }
    return QColor::fromRgbF(std::min(1.0F, ink.redF() * lift), std::min(1.0F, ink.greenF() * lift),
                            std::min(1.0F, ink.blueF() * lift), ink.alphaF());
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

QColor ThemeController::longFormInk() const {
    // Paragraph text uses the strongest neutral ink. It stays crisp under
    // every effect profile and high contrast never weakens it.
    return activePalette().text;
}

QColor ThemeController::iconInk() const {
    const ShellPalette& p = activePalette();
    if (settings_.high_contrast || p.light) {
        return p.text;
    }

    // Toolbar symbols are orientation aids, not emitters. Keep their normal
    // state below the Strong profile's bright-pass threshold; high contrast
    // deliberately promotes them to primary text instead. The base ink is
    // the family's curated icon role: the cap limits the brightest channel,
    // so the hue must carry enough luminance in its capped form to clear
    // the measured floor on the pressed control bed.
    constexpr float maximumChannel = 0.40F;
    const float brightest = std::max({p.icon.redF(), p.icon.greenF(), p.icon.blueF()});
    if (brightest <= maximumChannel) {
        return p.icon;
    }
    const float factor = maximumChannel / brightest;
    return QColor::fromRgbF(p.icon.redF() * factor, p.icon.greenF() * factor,
                            p.icon.blueF() * factor, p.icon.alphaF());
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
    return lifted(activePalette().dir);
}

QColor ThemeController::linkInk() const {
    return lifted(activePalette().link);
}

QColor ThemeController::metaInk() const {
    const ShellPalette& p = activePalette();
    return settings_.high_contrast ? p.text : lifted(p.meta);
}

QColor ThemeController::accent() const {
    return lifted(resolvedAccent());
}

QColor ThemeController::selectionBed() const {
    return activePalette().selectionBed;
}

QColor ThemeController::selectionInk() const {
    return lifted(activePalette().selectionInk);
}

QColor ThemeController::matchBed() const {
    return activePalette().match;
}

QColor ThemeController::focus() const {
    return accent();
}

QColor ThemeController::hover() const {
    return restingHover(activePalette());
}

QColor ThemeController::pressed() const {
    return restingPressed(activePalette());
}

QColor ThemeController::rubberBand() const {
    QColor band = focus();
    band.setAlphaF(0.20F);
    return band;
}

QColor ThemeController::danger() const {
    // High contrast promotes danger to the family's high-contrast variant:
    // the base ink clears its non-text floor everywhere it renders, but on
    // the selection and hover beds it cannot reach the 4.5:1 the override
    // promises for status text.
    const ShellPalette& p = activePalette();
    return settings_.high_contrast ? p.dangerHigh : p.danger;
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
        emit navigationSettingsChanged();
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
    // save catches up. Preference persistence must never interrupt use.
}

} // namespace odysea::app

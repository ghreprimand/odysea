// Tests for the shell's appearance state.
//
// Covers what the appearance surface promises: every setter changes live
// state observably and immediately, profile and reset transitions behave,
// fonts resolve to real families, and state survives a restart through the
// configured storage path. Persistence runs against a temporary directory.
#include "theme_contrast.hpp"
#include "theme_controller.hpp"
#include "theme_palettes.hpp"

#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QIODevice>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtNumeric>
#include <QtTest>

#include <algorithm>
#include <array>
#include <limits>
using odysea::app::themeAccentContrastSamples;
using odysea::app::themeContrastFailures;
using odysea::app::themeContrastRatio;
using odysea::app::ThemeContrastSample;
using odysea::app::ThemeController;

namespace {

/// Source-over composite of `ink` at `alpha` on an opaque `under` bed:
/// the same arithmetic the scene graph applies when a translucent layer
/// sits on an opaque surface.
QColor composite_over(const QColor& under, const QColor& ink, double alpha) {
    const auto a = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    return QColor::fromRgbF((ink.redF() * a) + (under.redF() * (1.0F - a)),
                            (ink.greenF() * a) + (under.greenF() * (1.0F - a)),
                            (ink.blueF() * a) + (under.blueF() * (1.0F - a)));
}

/// One concrete color a bed can present under a glyph. Composited beds
/// carry a variant per gradient extreme, so a floor holds across the whole
/// surface instead of only at its flattest point.
struct BedVariant {
    QString name;
    QColor color;
};

/// The pane ground as the compositor produces it. DirectoryPane paints
/// DeepFieldGround with the background sheet over the window ground — which
/// is the same background, so the glass amount flattens out — and layers
/// backgroundDeep ramps at the alphas DeepFieldGround.qml declares: 0.22 at
/// the top edge, 0.50 at the bottom edge, 0.30 in the side strips, each
/// scaled by the effective deep-field level and the glass amount. An
/// unselected row's own bed is transparent, so every glyph on such a row
/// reads against one of these variants.
QList<BedVariant> ground_beds(const ThemeController& theme) {
    const double amount = theme.effectiveDeepField() * theme.glassOpacity();
    const QColor sheet = theme.background();
    const QColor deep = theme.backgroundDeep();
    return {BedVariant{.name = QStringLiteral("ground"), .color = sheet},
            BedVariant{.name = QStringLiteral("ground bottom edge"),
                       .color = composite_over(sheet, deep, 0.50 * amount)},
            BedVariant{.name = QStringLiteral("ground side strip"),
                       .color = composite_over(sheet, deep, 0.30 * amount)},
            BedVariant{.name = QStringLiteral("ground top edge"),
                       .color = composite_over(sheet, deep, 0.22 * amount)}};
}

/// The chrome panel in both painted forms: opaque (dialogs, the command
/// palette, the appearance panel, ShellButton's own bed) and as a
/// ChromeStrip, which paints the panel at the surface opacity over the
/// window ground.
QList<BedVariant> panel_beds(const ThemeController& theme) {
    QList<BedVariant> beds{BedVariant{.name = QStringLiteral("panel"), .color = theme.panel()}};
    constexpr std::array<double, 6> surfaceOpacities{0.0, 0.2, 0.4, 0.6, 0.8, 1.0};
    for (const double opacity : surfaceOpacities) {
        beds.append(
            BedVariant{.name = QStringLiteral("panel strip at %1 opacity").arg(opacity, 0, 'f', 1),
                       .color = composite_over(theme.background(), theme.panel(), opacity)});
    }
    return beds;
}

QList<BedVariant> selection_beds(const ThemeController& theme) {
    return {BedVariant{.name = QStringLiteral("selection bed"), .color = theme.selectionBed()}};
}

QList<BedVariant> hover_beds(const ThemeController& theme) {
    return {BedVariant{.name = QStringLiteral("hover bed"), .color = theme.hover()}};
}

QList<BedVariant> pressed_beds(const ThemeController& theme) {
    return {BedVariant{.name = QStringLiteral("pressed bed"), .color = theme.pressed()}};
}

/// One measured readability claim: a foreground role on a bed a tracked
/// QML surface actually paints it on, with the floor it must clear in the
/// default state and under the high-contrast override. The bed resolves to
/// every variant the compositor can present there.
struct RolePair {
    const char* description;
    QColor (ThemeController::*foreground)() const;
    QList<BedVariant> (*beds)(const ThemeController&);
    double defaultFloor;
    double highContrastFloor;
};

/// Measures every pair, on every bed variant, against the floor for the
/// theme's current override state and returns a line per miss.
QStringList contrast_failures(ThemeController& theme, const QString& palette,
                              const QList<RolePair>& pairs, bool highContrast) {
    QList<ThemeContrastSample> samples;
    theme.setHighContrast(highContrast);
    for (const RolePair& pair : pairs) {
        const double floor = highContrast ? pair.highContrastFloor : pair.defaultFloor;
        const QColor ink = (theme.*pair.foreground)();
        for (const BedVariant& bed : pair.beds(theme)) {
            samples.append(ThemeContrastSample{.role = QLatin1String(pair.description),
                                               .renderSite = bed.name,
                                               .foreground = ink,
                                               .background = bed.color,
                                               .floor = floor});
        }
    }
    const QString mode = highContrast ? QStringLiteral(" [high contrast]") : QString();
    QStringList failures;
    for (const QString& failure : themeContrastFailures(samples)) {
        failures.append(QStringLiteral("%1 / %2%3").arg(palette, failure, mode));
    }
    return failures;
}

struct SemanticRole {
    const char* name;
    QColor color;
};

QList<SemanticRole> accent_independent_roles(const ThemeController& theme) {
    return {{.name = "generic file", .color = theme.textFaint()},
            {.name = "directory", .color = theme.dirInk()},
            {.name = "symlink", .color = theme.linkInk()},
            {.name = "metadata", .color = theme.metaInk()},
            {.name = "match bed", .color = theme.matchBed()},
            {.name = "icon", .color = theme.iconInk()},
            {.name = "selection ink", .color = theme.selectionInk()},
            {.name = "error", .color = theme.danger()},
            {.name = "warning", .color = theme.warning()},
            {.name = "success", .color = theme.success()}};
}

QList<ThemeController::Profile> effect_profiles() {
    return {ThemeController::Off, ThemeController::Minimal, ThemeController::Balanced,
            ThemeController::Strong, ThemeController::Custom};
}

void verify_accent_independent_roles(ThemeController& theme, const QVariantList& presets,
                                     const QString& palette, ThemeController::Profile profile,
                                     bool highContrast) {
    theme.setProfile(profile);
    theme.setHighContrast(highContrast);
    theme.setPaletteId(palette);
    theme.setAccentPresetIndex(0);
    const QList<SemanticRole> expected = accent_independent_roles(theme);

    for (int index = 0; index < presets.size(); ++index) {
        theme.setAccentPresetIndex(index);
        const QList<SemanticRole> actual = accent_independent_roles(theme);
        for (int role = 0; role < expected.size(); ++role) {
            const QString context =
                QStringLiteral("%1 / profile %2 / high contrast %3 / preset %4 / %5")
                    .arg(palette)
                    .arg(static_cast<int>(profile))
                    .arg(highContrast)
                    .arg(presets.at(index).toMap().value(QStringLiteral("id")).toString())
                    .arg(QLatin1String(expected.at(role).name));
            QVERIFY2(actual.at(role).color == expected.at(role).color, qPrintable(context));
        }
    }
}

} // namespace

class tst_ThemeController : public QObject {
    Q_OBJECT

  private slots:
    void defaults_are_the_shipped_configuration();
    void palettes_resolve_and_restyle();
    void accent_presets_resolve_live_and_keep_stable_ids();
    void accent_presets_preserve_file_type_and_status_roles();
    void shipped_accent_presets_clear_render_site_contrast_matrix();
    void accent_contrast_warning_uses_render_site_measurement();
    void profile_presets_steer_effect_levels();
    void slider_writes_switch_to_custom_and_are_remembered();
    void accessibility_overrides_apply_immediately();
    void stored_levels_stay_editable_under_overrides();
    void non_finite_input_never_corrupts_geometry();
    void named_font_family_cannot_inject_keys();
    void reset_repairs_a_damaged_settings_file();
    void bundled_font_resources_register_every_shipped_face();
    void fonts_resolve_to_available_families();
    void metrics_follow_density_and_scale();
    void typography_roles_bound_fallback_reflow();
    void long_form_role_is_readable();
    void no_op_writes_do_not_notify();
    void places_have_safe_defaults_and_can_be_added();
    void places_can_be_reordered_and_removed();
    void recent_destinations_are_bounded_and_deduplicated();
    void recent_destinations_can_be_cleared();
    void workspace_layout_settings_notify_and_clamp();
    void corrupt_navigation_settings_fall_back_safely();
    void state_persists_across_instances();
    void navigation_settings_persist_across_instances();
    void reset_restores_and_persists_the_defaults();
    void text_lift_brightens_chromatic_inks_from_effective_state();
    void default_icon_ink_pressed_bed_is_the_binding_margin();
    void high_contrast_roles_meet_measured_ratios();
};

void tst_ThemeController::defaults_are_the_shipped_configuration() {
    ThemeController theme;
    QCOMPARE(theme.paletteId(), QStringLiteral("odyssey-default"));
    QCOMPARE(theme.profile(), ThemeController::Balanced);
    QCOMPARE(theme.fontSource(), ThemeController::Bundled);
    QCOMPARE(theme.density(), ThemeController::Cozy);
    QCOMPARE(theme.uiScale(), 1.0);
    QVERIFY(!theme.lightPalette());
    QCOMPARE(theme.availablePalettes().first(), QStringLiteral("odyssey-default"));
    QCOMPARE(theme.availablePalettes().size(), 14);
    QCOMPARE(theme.availablePalettes(),
             QStringList({QStringLiteral("odyssey-default"), QStringLiteral("odyssey"),
                          QStringLiteral("odyssey-midnight"), QStringLiteral("odyssey-harvest"),
                          QStringLiteral("odyssey-lagoon"), QStringLiteral("odyssey-plasma"),
                          QStringLiteral("odyssey-borealis"), QStringLiteral("odyssey-crimson"),
                          QStringLiteral("odyssey-fuchsia"), QStringLiteral("odyssey-amber"),
                          QStringLiteral("odyssey-graphite"), QStringLiteral("odyssey-aurora"),
                          QStringLiteral("odyssey-parchment-light"),
                          QStringLiteral("odyssey-slate-light")}));
    QCOMPARE(theme.accentPresetId(), QStringLiteral("tideglass"));
    QCOMPARE(theme.accentPresets().first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Tideglass"));
    QCOMPARE(theme.rowHeight(), 34);
}

void tst_ThemeController::palettes_resolve_and_restyle() {
    ThemeController theme;
    const QColor defaultGround = theme.background();
    const QColor defaultDir = theme.dirInk();

    QSignalSpy changed(&theme, &ThemeController::appearanceChanged);
    theme.setPaletteId(QStringLiteral("odyssey-slate-light"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(theme.lightPalette());
    QVERIFY(theme.background() != defaultGround);
    QVERIFY(theme.dirInk() != defaultDir);
    QVERIFY(theme.background().lightness() > theme.text().lightness());

    // An unknown identifier resolves to the shipped default instead of
    // rendering nothing.
    theme.setPaletteId(QStringLiteral("no-such-family"));
    QCOMPARE(theme.paletteId(), QStringLiteral("odyssey-default"));
    QCOMPARE(theme.background(), defaultGround);
}

void tst_ThemeController::accent_presets_resolve_live_and_keep_stable_ids() {
    ThemeController theme;
    const QVariantList presets = ThemeController::accentPresets();
    QCOMPARE(presets.size(), 5);

    const QStringList expectedIds{QStringLiteral("tideglass"), QStringLiteral("beacon"),
                                  QStringLiteral("ember"), QStringLiteral("orchid"),
                                  QStringLiteral("verdant")};
    const QStringList expectedNames{QStringLiteral("Tideglass"), QStringLiteral("Beacon"),
                                    QStringLiteral("Ember"), QStringLiteral("Orchid"),
                                    QStringLiteral("Verdant")};
    QStringList ids;
    QStringList names;
    for (const QVariant& value : presets) {
        const QVariantMap preset = value.toMap();
        ids.append(preset.value(QStringLiteral("id")).toString());
        names.append(preset.value(QStringLiteral("name")).toString());
        QVERIFY(preset.value(QStringLiteral("color")).value<QColor>().isValid());
    }
    QCOMPARE(ids, expectedIds);
    QCOMPARE(names, expectedNames);

    const QColor tideglass = theme.accent();
    theme.setAccentPresetId(QStringLiteral("beacon"));
    QCOMPARE(theme.accentPresetId(), QStringLiteral("beacon"));
    QCOMPARE(theme.accentPresetIndex(), 1);
    QVERIFY(theme.accent() != tideglass);
    QCOMPARE(theme.focus(), theme.accent());

    theme.setAccentPresetIndex(2);
    QCOMPARE(theme.accentPresetId(), QStringLiteral("ember"));
    theme.setAccentPresetId(QStringLiteral("not-a-preset"));
    QCOMPARE(theme.accentPresetId(), QStringLiteral("tideglass"));
}

void tst_ThemeController::accent_presets_preserve_file_type_and_status_roles() {
    ThemeController theme;
    const QVariantList presets = ThemeController::accentPresets();
    QVERIFY(!presets.isEmpty());

    // This is a controller-routing invariant, not a claim about the current
    // preset data. It dynamically covers every model entry. The separate
    // roster test intentionally requires an explicit update for a new
    // shipped choice.
    for (const ThemeController::Profile profile : effect_profiles()) {
        for (const bool highContrast : {false, true}) {
            for (const QString& palette : theme.availablePalettes()) {
                verify_accent_independent_roles(theme, presets, palette, profile, highContrast);
            }
        }
    }
}

void tst_ThemeController::shipped_accent_presets_clear_render_site_contrast_matrix() {
    ThemeController theme;
    const QVariantList presets = ThemeController::accentPresets();
    QVERIFY(!presets.isEmpty());

    QStringList failures;
    for (const ThemeController::Profile profile : effect_profiles()) {
        theme.setProfile(profile);
        for (const bool highContrast : {false, true}) {
            theme.setHighContrast(highContrast);
            for (const QString& palette : theme.availablePalettes()) {
                theme.setPaletteId(palette);
                for (int index = 0; index < presets.size(); ++index) {
                    theme.setAccentPresetIndex(index);
                    const QString context =
                        QStringLiteral("%1 / preset %2 / profile %3 / high contrast %4")
                            .arg(palette)
                            .arg(presets.at(index).toMap().value(QStringLiteral("id")).toString())
                            .arg(static_cast<int>(profile))
                            .arg(highContrast);
                    for (const QString& failure : themeContrastFailures(themeAccentContrastSamples(
                             theme.accent(), theme.background(), theme.selectionBed(),
                             theme.hover(), theme.pressed(), theme.panel()))) {
                        failures.append(QStringLiteral("%1 / %2").arg(context, failure));
                    }
                    if (!theme.accentContrastWarning().isEmpty()) {
                        failures.append(QStringLiteral("%1 / warning: %2")
                                            .arg(context, theme.accentContrastWarning()));
                    }
                }
            }
        }
    }

    QVERIFY2(failures.isEmpty(), qPrintable(QStringLiteral("accent contrast floors not met:\n") +
                                            failures.join(QStringLiteral("\n"))));
}

void tst_ThemeController::accent_contrast_warning_uses_render_site_measurement() {
    ThemeController theme;
    theme.setPaletteId(QStringLiteral("odyssey-parchment-light"));
    theme.setAccentPresetId(QStringLiteral("beacon"));

    const QList<ThemeContrastSample> samples =
        themeAccentContrastSamples(theme.accent(), theme.background(), theme.selectionBed(),
                                   theme.hover(), theme.pressed(), theme.panel());
    const QStringList failures = themeContrastFailures(samples);
    QCOMPARE(theme.accentContrastWarning().isEmpty(), failures.isEmpty());
    for (const QString& failure : failures) {
        QVERIFY(theme.accentContrastWarning().contains(failure));
    }

    const auto authoredBeacon = ThemeController::accentPresets()
                                    .at(1)
                                    .toMap()
                                    .value(QStringLiteral("color"))
                                    .value<QColor>();
    QVERIFY(!themeContrastFailures(themeAccentContrastSamples(authoredBeacon, theme.background(),
                                                              theme.selectionBed(), theme.hover(),
                                                              theme.pressed(), theme.panel()))
                 .isEmpty());
}

void tst_ThemeController::profile_presets_steer_effect_levels() {
    ThemeController theme;
    const qreal balancedBloom = theme.bloomCore();
    const qreal balancedScanline = theme.scanline();
    QVERIFY(balancedBloom > 0.0);

    theme.setProfile(ThemeController::Strong);
    QVERIFY(theme.bloomCore() > balancedBloom);
    QVERIFY(theme.scanline() > balancedScanline);

    theme.setProfile(ThemeController::Off);
    QCOMPARE(theme.bloomCore(), 0.0);
    QCOMPARE(theme.scanline(), 0.0);
    QCOMPARE(theme.deepField(), 0.0);

    theme.setProfile(ThemeController::Minimal);
    QCOMPARE(theme.bloomCore(), 0.0);
    QVERIFY(theme.deepField() > 0.0);

    theme.setProfile(ThemeController::Balanced);
    QCOMPARE(theme.bloomCore(), balancedBloom);
}

void tst_ThemeController::slider_writes_switch_to_custom_and_are_remembered() {
    ThemeController theme;
    QCOMPARE(theme.profile(), ThemeController::Balanced);

    theme.setScanline(0.05);
    QCOMPARE(theme.profile(), ThemeController::Custom);
    QCOMPARE(theme.scanline(), 0.05);
    // The other levels carried over from the balanced seed.
    QCOMPARE(theme.bloomCore(),
             odysea::core::effect_profile_levels(odysea::core::EffectProfile::Balanced).bloom_core);

    // Preset selection re-presents the preset...
    theme.setProfile(ThemeController::Strong);
    QVERIFY(theme.scanline() > 0.05);

    // ...and returning to custom restores the remembered adjustment.
    theme.setProfile(ThemeController::Custom);
    QCOMPARE(theme.scanline(), 0.05);

    // Writes clamp to the documented caps.
    theme.setBloomCore(99.0);
    QCOMPARE(theme.bloomCore(), 0.8);
    theme.setTextLift(0.0);
    QCOMPARE(theme.textLift(), 1.0);
}

void tst_ThemeController::accessibility_overrides_apply_immediately() {
    ThemeController theme;
    theme.setProfile(ThemeController::Strong);
    const QColor mutedBefore = theme.textMuted();

    // Overrides shape the effective levels — the rendering contract — while
    // the stored preference the controls edit stays visible and intact.
    theme.setHighContrast(true);
    QCOMPARE(theme.effectiveScanline(), 0.0);
    QCOMPARE(theme.effectiveVignette(), 0.0);
    QCOMPARE(theme.effectiveTextLift(), 1.0);
    QVERIFY(theme.scanline() > 0.0);
    QVERIFY(theme.vignette() > 0.0);
    QVERIFY(theme.textLift() > 1.0);
    QCOMPARE(theme.textMuted(), theme.text());
    QVERIFY(theme.textMuted() != mutedBefore);
    QCOMPARE(theme.effectiveBloomCore(), 0.0);
    QCOMPARE(theme.effectiveBloomWide(), 0.0);
    QCOMPARE(theme.effectivePersistence(), 0.0);

    theme.setHighContrast(false);
    QCOMPARE(theme.textMuted(), mutedBefore);
    QCOMPARE(theme.effectiveScanline(), theme.scanline());

    theme.setReducedMotion(true);
    QCOMPARE(theme.effectivePersistence(), 0.0);
    QVERIFY(theme.persistence() > 0.0);
    QCOMPARE(theme.effectiveBloomCore(), 0.0);
    QCOMPARE(theme.effectiveBloomWide(), 0.0);
    QCOMPARE(theme.effectiveScanline(), 0.0);
    QCOMPARE(theme.effectiveVignette(), 0.0);
}

void tst_ThemeController::stored_levels_stay_editable_under_overrides() {
    ThemeController theme;

    // With high contrast active, an effect slider still displays the stored
    // value, still accepts a write, and never snaps back — the override pins
    // only what gets rendered.
    theme.setHighContrast(true);
    QVERIFY(theme.scanline() > 0.0); // the balanced preset shows through
    theme.setScanline(0.30);
    QCOMPARE(theme.profile(), ThemeController::Custom);
    QCOMPARE(theme.scanline(), 0.30);
    QCOMPARE(theme.effectiveScanline(), 0.0);

    theme.setVignette(0.2);
    QCOMPARE(theme.vignette(), 0.2);
    QCOMPARE(theme.effectiveVignette(), 0.0);

    theme.setTextLift(1.4);
    QCOMPARE(theme.textLift(), 1.4);
    QCOMPARE(theme.effectiveTextLift(), 1.0);

    // Lifting the override renders the adjustments that were made under it.
    theme.setHighContrast(false);
    QCOMPARE(theme.effectiveScanline(), 0.30);
    QCOMPARE(theme.effectiveVignette(), 0.2);
    QCOMPARE(theme.effectiveTextLift(), 1.4);

    // The same holds for persistence under reduced motion.
    theme.setReducedMotion(true);
    theme.setPersistence(0.6);
    QCOMPARE(theme.persistence(), 0.6);
    QCOMPARE(theme.effectivePersistence(), 0.0);
    theme.setReducedMotion(false);
    QCOMPARE(theme.effectivePersistence(), 0.6);

    // Profile transitions under an override: the stored view follows the
    // preset, the effective view stays overridden.
    theme.setHighContrast(true);
    theme.setProfile(ThemeController::Strong);
    QCOMPARE(theme.scanline(),
             odysea::core::effect_profile_levels(odysea::core::EffectProfile::Strong).scanline);
    QCOMPARE(theme.effectiveScanline(), 0.0);
    theme.setProfile(ThemeController::Custom);
    QCOMPARE(theme.scanline(), 0.30);
    QCOMPARE(theme.effectiveScanline(), 0.0);
}

void tst_ThemeController::non_finite_input_never_corrupts_geometry() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));

    // A NaN write clamps to a finite bound; the geometry stays usable and
    // the persisted file never carries a non-finite number.
    ThemeController theme;
    theme.setStoragePath(path);
    theme.setUiScale(qQNaN());
    QVERIFY(qIsFinite(theme.uiScale()));
    QCOMPARE(theme.uiScale(), 0.75);
    QVERIFY(theme.rowHeight() >= 26);
    QVERIFY(theme.chromeFontPixelSize() >= 9);

    QFile written(path);
    QVERIFY(written.open(QIODevice::ReadOnly));
    const QByteArray text = written.readAll();
    QVERIFY(!text.contains("nan"));
    QVERIFY(!text.contains("inf"));

    // A poisoned file on disk parses to the defaults rather than reaching
    // the layout as NaN.
    const QString poisoned = dir.filePath(QStringLiteral("poisoned.conf"));
    QFile poison(poisoned);
    QVERIFY(poison.open(QIODevice::WriteOnly));
    poison.write("scale=nan\ncustom_bloom_core=nan\nglass_opacity=-nan\n");
    poison.close();

    ThemeController restored;
    restored.setStoragePath(poisoned);
    QVERIFY(qIsFinite(restored.uiScale()));
    QCOMPARE(restored.uiScale(), 1.0);
    QCOMPARE(restored.rowHeight(), 34);
    QVERIFY(qIsFinite(restored.glassOpacity()));
    QCOMPARE(restored.glassOpacity(), 1.0);
}

void tst_ThemeController::named_font_family_cannot_inject_keys() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));

    {
        ThemeController theme;
        theme.setStoragePath(path);
        theme.setFontSource(ThemeController::Named);
        theme.setNamedFontFamily(
            QStringLiteral("Fake\npalette=odyssey-amber\nprofile=strong\nhigh_contrast=true"));
        // The live state already shows the sanitized value.
        QVERIFY(!theme.namedFontFamily().contains(QLatin1Char('\n')));
    }

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.paletteId(), QStringLiteral("odyssey-default"));
    QCOMPARE(restored.profile(), ThemeController::Balanced);
    QVERIFY(!restored.highContrast());
    QCOMPARE(restored.fontSource(), ThemeController::Named);
}

void tst_ThemeController::reset_repairs_a_damaged_settings_file() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));

    // A damaged file parses to the defaults, so the live state matches them
    // while the file on disk is still wrong. Reset must rewrite it anyway.
    QFile damaged(path);
    QVERIFY(damaged.open(QIODevice::WriteOnly));
    damaged.write("scale=nan\ncomplete nonsense\n");
    damaged.close();

    ThemeController theme;
    theme.setStoragePath(path);
    QCOMPARE(theme.uiScale(), 1.0);
    theme.resetToDefaults();

    QFile repaired(path);
    QVERIFY(repaired.open(QIODevice::ReadOnly));
    const QByteArray text = repaired.readAll();
    QVERIFY(!text.contains("nan"));
    QVERIFY(!text.contains("nonsense"));
    QVERIFY(text.contains("version=4"));

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.paletteId(), QStringLiteral("odyssey-default"));
    QCOMPARE(restored.uiScale(), 1.0);
}

void tst_ThemeController::fonts_resolve_to_available_families() {
    ThemeController theme;
    QVERIFY(theme.bundledFontAvailable());
    QCOMPARE(theme.contentFontFamily(), QStringLiteral("Victor Mono"));
    QCOMPARE(theme.chromeFontFamily(), QStringLiteral("Victor Mono"));
    QCOMPARE(theme.pathFontFamily(), QStringLiteral("Victor Mono"));
    QCOMPARE(theme.captionFontFamily(), QStringLiteral("Victor Mono"));
    QVERIFY(QFontDatabase::hasFamily(theme.longFormFontFamily()));

    theme.setFontSource(ThemeController::System);
    QCOMPARE(theme.contentFontFamily(),
             QFontDatabase::systemFont(QFontDatabase::FixedFont).family());

    // A named family that does not exist falls back to a real one.
    theme.setFontSource(ThemeController::Named);
    theme.setNamedFontFamily(QStringLiteral("No Such Family 123"));
    QVERIFY(QFontDatabase::hasFamily(theme.contentFontFamily()));

    // A named family that exists is honored.
    const QStringList families = QFontDatabase::families();
    QVERIFY(!families.isEmpty());
    theme.setNamedFontFamily(families.first());
    QCOMPARE(theme.contentFontFamily(), families.first());
}

void tst_ThemeController::bundled_font_resources_register_every_shipped_face() {
    static const QStringList resources{
        QStringLiteral(":/qt/qml/OdySea/third_party/victor-mono/VictorMono-Regular.otf"),
        QStringLiteral(":/qt/qml/OdySea/third_party/victor-mono/VictorMono-Italic.otf"),
        QStringLiteral(":/qt/qml/OdySea/third_party/victor-mono/VictorMono-Bold.otf"),
        QStringLiteral(":/qt/qml/OdySea/third_party/victor-mono/VictorMono-BoldItalic.otf")};

    for (const QString& resource : resources) {
        const int id = QFontDatabase::addApplicationFont(resource);
        QVERIFY2(id >= 0, qPrintable(resource));
        QCOMPARE(QFontDatabase::applicationFontFamilies(id),
                 QStringList{QStringLiteral("Victor Mono")});
        QVERIFY(QFontDatabase::removeApplicationFont(id));
    }
}

void tst_ThemeController::metrics_follow_density_and_scale() {
    ThemeController theme;
    const int cozy = theme.rowHeight();

    theme.setDensity(ThemeController::Compact);
    QVERIFY(theme.rowHeight() < cozy);
    theme.setDensity(ThemeController::Comfortable);
    QVERIFY(theme.rowHeight() > cozy);

    theme.setDensity(ThemeController::Cozy);
    theme.setUiScale(2.0);
    QCOMPARE(theme.rowHeight(), 68);
    QCOMPARE(theme.chromeFontPixelSize(), 26);
    QCOMPARE(theme.contentFontPixelSize(), 28);
    QCOMPARE(theme.pathFontPixelSize(), 26);
    QCOMPARE(theme.captionFontPixelSize(), 24);
    QCOMPARE(theme.longFormFontPixelSize(), 30);
    QCOMPARE(theme.longFormMeasure(), 1120);
    QVERIFY(theme.gridCellWidth() > 144);

    theme.setUiScale(99.0);
    QCOMPARE(theme.uiScale(), 2.0);
}

void tst_ThemeController::typography_roles_bound_fallback_reflow() {
    ThemeController theme;
    const int rowHeight = theme.rowHeight();
    const int cellWidth = theme.gridCellWidth();
    const int cellHeight = theme.gridCellHeight();

    QFont bundled(theme.contentFontFamily());
    bundled.setPixelSize(theme.contentFontPixelSize());
    const QFontMetrics bundledMetrics(bundled);

    theme.setFontSource(ThemeController::System);
    QFont fallback(theme.contentFontFamily());
    fallback.setPixelSize(theme.contentFontPixelSize());
    const QFontMetrics fallbackMetrics(fallback);

    QCOMPARE(theme.rowHeight(), rowHeight);
    QCOMPARE(theme.gridCellWidth(), cellWidth);
    QCOMPARE(theme.gridCellHeight(), cellHeight);
    QVERIFY(bundledMetrics.height() <= rowHeight - 6);
    QVERIFY(fallbackMetrics.height() <= rowHeight - 6);
    QVERIFY(std::abs(bundledMetrics.height() - fallbackMetrics.height()) <= rowHeight / 4);

    const QString sample = QStringLiteral("Archive-2026");
    const int advanceDelta = std::abs(bundledMetrics.horizontalAdvance(sample) -
                                      fallbackMetrics.horizontalAdvance(sample));
    QVERIFY(advanceDelta <= cellWidth / 2);
}

void tst_ThemeController::long_form_role_is_readable() {
    ThemeController theme;
    QVERIFY(QFontDatabase::hasFamily(theme.longFormFontFamily()));
    QVERIFY(theme.longFormFontPixelSize() > theme.chromeFontPixelSize());
    QVERIFY(theme.longFormLineHeight() >= 1.4);
    QVERIFY(theme.longFormLineHeight() <= 1.6);
    QVERIFY(theme.longFormMeasure() >= 500);
    QVERIFY(theme.longFormMeasure() <= 620);
    QCOMPARE(theme.longFormInk(), theme.text());

    for (const QString& palette : theme.availablePalettes()) {
        theme.setPaletteId(palette);
        QVERIFY2(themeContrastRatio(theme.longFormInk(), theme.background()) >= 4.5,
                 qPrintable(QStringLiteral("Long-form contrast failed for %1").arg(palette)));
    }

    theme.setProfile(ThemeController::Strong);
    const QColor icon = theme.iconInk();
    const float brightest = std::max({icon.redF(), icon.greenF(), icon.blueF()});
    QVERIFY(brightest < 0.45F);
    theme.setHighContrast(true);
    QCOMPARE(theme.iconInk(), theme.text());

    theme.setUiScale(2.0);
    QCOMPARE(theme.longFormFontPixelSize(), 30);
    QCOMPARE(theme.longFormMeasure(), 1120);
}

void tst_ThemeController::default_icon_ink_pressed_bed_is_the_binding_margin() {
    ThemeController theme;
    theme.setProfile(ThemeController::Balanced);
    theme.setHighContrast(false);

    double tightestRatio = std::numeric_limits<double>::max();
    QString tightestPalette;
    QString tightestBed;
    for (const QString& palette : theme.availablePalettes()) {
        theme.setPaletteId(palette);
        QList<BedVariant> beds = panel_beds(theme);
        beds.append(hover_beds(theme));
        beds.append(pressed_beds(theme));
        for (const BedVariant& bed : beds) {
            const double ratio = themeContrastRatio(theme.iconInk(), bed.color);
            if (ratio < tightestRatio) {
                tightestRatio = ratio;
                tightestPalette = palette;
                tightestBed = bed.name;
            }
        }
    }

    QCOMPARE(tightestPalette, QStringLiteral("odyssey"));
    QCOMPARE(tightestBed, QStringLiteral("pressed bed"));
    QVERIFY2(tightestRatio >= 2.10 && tightestRatio <= 2.11,
             qPrintable(QStringLiteral("measured ratio: %1").arg(tightestRatio, 0, 'f', 6)));
    QVERIFY2(
        ((tightestRatio / 2.0) - 1.0) >= 0.05 && ((tightestRatio / 2.0) - 1.0) <= 0.055,
        qPrintable(
            QStringLiteral("measured margin: %1").arg((tightestRatio / 2.0) - 1.0, 0, 'f', 6)));
}

void tst_ThemeController::high_contrast_roles_meet_measured_ratios() {
    // Measured readability floors for the semantic roles, on the beds each
    // role actually renders on. High contrast is the accessibility promise:
    // WCAG 2.x ratios — 4.5:1 for text-sized roles, 3.0:1 for non-text
    // indicators — must hold in every shipped family. The default state is
    // held to the same non-text floor plus a 4.5:1 floor on the primary
    // reading pairs, with the effective Balanced text lift applied, so the
    // aesthetic mode cannot silently regress below readable either.
    ThemeController theme;

    // Every pair below is anchored on a tracked render site, and the bed is
    // what the compositor actually presents under that role there. Sites:
    //   ground    — DirectoryPane's DeepFieldGround under transparent
    //               list/grid rows, the window ground, and field surfaces.
    //   panel     — chrome strips, dialogs, the command palette, the
    //               appearance panel, and ShellButton's resting bed.
    //   selection — selected rows and cells, highlighted palette rows.
    //   hover     — hovered rows, cells, and chrome controls.
    //   pressed   — ShellButton, breadcrumb, and splitter active states.
    // Roles with no tracked render site — well, matchBed, selectionInk,
    // metaInk, success — are deliberately absent: a floor asserted on a bed
    // nothing paints can neither fail nor protect anything. When a surface
    // starts painting one of them, its real combinations join this list.
    const QList<RolePair> pairs = {
        // Primary text: entry names in both views on every row state,
        // chrome labels, dialog and palette text, field text.
        RolePair{.description = "text",
                 .foreground = &ThemeController::text,
                 .beds = ground_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "text",
                 .foreground = &ThemeController::text,
                 .beds = selection_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "text",
                 .foreground = &ThemeController::text,
                 .beds = hover_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "text",
                 .foreground = &ThemeController::text,
                 .beds = pressed_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "text",
                 .foreground = &ThemeController::text,
                 .beds = panel_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        // Secondary text: the size column renders on unselected, selected,
        // and hovered rows — DirectoryListView's metadata ink defaults to
        // this role — and status and caption text sit on the panel.
        RolePair{.description = "muted text",
                 .foreground = &ThemeController::textMuted,
                 .beds = ground_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "muted text",
                 .foreground = &ThemeController::textMuted,
                 .beds = selection_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "muted text",
                 .foreground = &ThemeController::textMuted,
                 .beds = hover_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "muted text",
                 .foreground = &ThemeController::textMuted,
                 .beds = panel_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        // Directory names in both views, on every row state.
        RolePair{.description = "directory ink",
                 .foreground = &ThemeController::dirInk,
                 .beds = ground_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "directory ink",
                 .foreground = &ThemeController::dirInk,
                 .beds = selection_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "directory ink",
                 .foreground = &ThemeController::dirInk,
                 .beds = hover_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        RolePair{.description = "long-form ink",
                 .foreground = &ThemeController::longFormInk,
                 .beds = panel_beds,
                 .defaultFloor = 4.5,
                 .highContrastFloor = 4.5},
        // The recovery caption renders on every row state; status errors
        // and destructive menu and palette items sit on the panel and the
        // selection bed.
        RolePair{.description = "danger ink",
                 .foreground = &ThemeController::danger,
                 .beds = ground_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 4.5},
        RolePair{.description = "danger ink",
                 .foreground = &ThemeController::danger,
                 .beds = selection_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 4.5},
        RolePair{.description = "danger ink",
                 .foreground = &ThemeController::danger,
                 .beds = hover_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 4.5},
        RolePair{.description = "danger ink",
                 .foreground = &ThemeController::danger,
                 .beds = panel_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 4.5},
        // The path navigator's warning line on its panel strip; warnings on
        // the window ground.
        RolePair{.description = "warning ink",
                 .foreground = &ThemeController::warning,
                 .beds = panel_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 4.5},
        RolePair{.description = "warning ink",
                 .foreground = &ThemeController::warning,
                 .beds = ground_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 4.5},
        // Entry icons: file marks use the faint ink and symlink marks the
        // link ink, on every row state.
        RolePair{.description = "file icon ink",
                 .foreground = &ThemeController::textFaint,
                 .beds = ground_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "file icon ink",
                 .foreground = &ThemeController::textFaint,
                 .beds = selection_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "file icon ink",
                 .foreground = &ThemeController::textFaint,
                 .beds = hover_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "link ink",
                 .foreground = &ThemeController::linkInk,
                 .beds = ground_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "link ink",
                 .foreground = &ThemeController::linkInk,
                 .beds = selection_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "link ink",
                 .foreground = &ThemeController::linkInk,
                 .beds = hover_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        // Disabled chrome ink: ShellButton's disabled label and icon.
        // Inactive controls are exempt from WCAG contrast minima; this
        // floor is an anti-regression bound so disabled chrome stays
        // perceivable rather than a conformance claim.
        RolePair{.description = "disabled chrome ink",
                 .foreground = &ThemeController::textFaint,
                 .beds = panel_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        // Accent indicators: the current-row ring on every row state, the
        // checked tab label on the window ground, and focus borders on
        // buttons and fields.
        RolePair{.description = "accent",
                 .foreground = &ThemeController::accent,
                 .beds = ground_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "accent",
                 .foreground = &ThemeController::accent,
                 .beds = selection_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "accent",
                 .foreground = &ThemeController::accent,
                 .beds = hover_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "accent",
                 .foreground = &ThemeController::accent,
                 .beds = panel_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        // Keyboard focus ink: the split grip on the window ground and
        // focus borders against the panel.
        RolePair{.description = "focus",
                 .foreground = &ThemeController::focus,
                 .beds = ground_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        RolePair{.description = "focus",
                 .foreground = &ThemeController::focus,
                 .beds = panel_beds,
                 .defaultFloor = 3.0,
                 .highContrastFloor = 3.0},
        // Icon ink is deliberately subdued in the default state: its
        // channels are capped below the Strong profile's bright-pass
        // threshold so toolbar symbols stay orientation aids instead of
        // emitters. The default floor is therefore an anti-regression bound
        // on the accepted appearance, and the 4.5 high-contrast floor is
        // the accessibility path — the override promotes icon ink to
        // primary text. The role renders on the resting, hovered, and
        // pressed control beds.
        RolePair{.description = "icon ink",
                 .foreground = &ThemeController::iconInk,
                 .beds = panel_beds,
                 .defaultFloor = 2.0,
                 .highContrastFloor = 4.5},
        RolePair{.description = "icon ink",
                 .foreground = &ThemeController::iconInk,
                 .beds = hover_beds,
                 .defaultFloor = 2.0,
                 .highContrastFloor = 4.5},
        RolePair{.description = "icon ink",
                 .foreground = &ThemeController::iconInk,
                 .beds = pressed_beds,
                 .defaultFloor = 2.0,
                 .highContrastFloor = 4.5},
    };

    const QVariantList presets = ThemeController::accentPresets();
    QVERIFY(!presets.isEmpty());

    QStringList failures;
    for (const QString& palette : theme.availablePalettes()) {
        theme.setPaletteId(palette);
        for (int index = 0; index < presets.size(); ++index) {
            theme.setAccentPresetIndex(index);
            const QString context =
                QStringLiteral("%1 / preset %2")
                    .arg(palette, presets.at(index).toMap().value(QStringLiteral("id")).toString());
            failures.append(contrast_failures(theme, context, pairs, false));
            failures.append(contrast_failures(theme, context, pairs, true));
        }

        // The high-contrast hairline promotion is itself a measured claim.
        theme.setHighContrast(true);
        const double hairline = themeContrastRatio(theme.border(), theme.panel());
        if (hairline < 3.0) {
            failures.append(QStringLiteral("%1 / hairline on panel [high contrast]: %2 < 3.0")
                                .arg(palette, QString::number(hairline, 'f', 2)));
        }
        theme.setHighContrast(false);
    }

    QVERIFY2(failures.isEmpty(), qPrintable(QStringLiteral("contrast floors not met:\n") +
                                            failures.join(QStringLiteral("\n"))));
}

void tst_ThemeController::no_op_writes_do_not_notify() {
    ThemeController theme;
    QSignalSpy changed(&theme, &ThemeController::appearanceChanged);
    theme.setProfile(ThemeController::Balanced);
    theme.setPaletteId(QStringLiteral("odyssey-default"));
    theme.setUiScale(1.0);
    theme.setHighContrast(false);
    QCOMPARE(changed.count(), 0);

    theme.setUiScale(1.5);
    QCOMPARE(changed.count(), 1);
}

void tst_ThemeController::places_have_safe_defaults_and_can_be_added() {
    ThemeController theme;
    QSignalSpy changed(&theme, &ThemeController::navigationSettingsChanged);

    QCOMPARE(theme.places().size(), 1);
    QCOMPARE(theme.places().front().toMap().value(QStringLiteral("path")).toString(),
             QStringLiteral("/"));
    QVERIFY(theme.addPlace({{QStringLiteral("label"), QStringLiteral("Projects")},
                            {QStringLiteral("path"), QStringLiteral("/synthetic/projects")}}));
    QVERIFY(theme.addPlace({{QStringLiteral("path"), QStringLiteral("/synthetic/archive")}}));
    QVERIFY(!theme.addPlace({{QStringLiteral("label"), QStringLiteral("Duplicate")},
                             {QStringLiteral("path"), QStringLiteral("/synthetic/projects")}}));
    QCOMPARE(theme.places().size(), 3);
    QCOMPARE(theme.places().at(2).toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("archive"));
    QCOMPARE(changed.count(), 2);
}

void tst_ThemeController::places_can_be_reordered_and_removed() {
    ThemeController theme;
    QVERIFY(theme.addPlace({{QStringLiteral("label"), QStringLiteral("Projects")},
                            {QStringLiteral("path"), QStringLiteral("/synthetic/projects")}}));
    QVERIFY(theme.addPlace({{QStringLiteral("path"), QStringLiteral("/synthetic/archive")}}));

    QVERIFY(theme.movePlace(2, 1));
    QCOMPARE(theme.places().at(1).toMap().value(QStringLiteral("path")).toString(),
             QStringLiteral("/synthetic/archive"));
    QVERIFY(theme.removePlace(1));
    QCOMPARE(theme.places().size(), 2);
}

void tst_ThemeController::recent_destinations_are_bounded_and_deduplicated() {
    ThemeController theme;
    for (int index = 0;
         index <
         static_cast<int>(odysea::core::AppearanceSettings::maximum_recent_destinations) + 3;
         ++index) {
        QVERIFY(theme.recordRecentDestination(QStringLiteral("/synthetic/recent-%1").arg(index)));
    }
    QCOMPARE(theme.recentDestinations().size(),
             static_cast<qsizetype>(odysea::core::AppearanceSettings::maximum_recent_destinations));
    QCOMPARE(theme.recentDestinations().front(), QStringLiteral("/synthetic/recent-14"));
    QVERIFY(theme.recordRecentDestination(QStringLiteral("/synthetic/recent-6")));
    QCOMPARE(theme.recentDestinations().front(), QStringLiteral("/synthetic/recent-6"));
}

void tst_ThemeController::recent_destinations_can_be_cleared() {
    ThemeController theme;
    QVERIFY(theme.recordRecentDestination(QStringLiteral("/synthetic/recent")));
    QVERIFY(theme.clearRecentDestinations());
    QVERIFY(theme.recentDestinations().isEmpty());
    QVERIFY(!theme.clearRecentDestinations());
}

void tst_ThemeController::workspace_layout_settings_notify_and_clamp() {
    ThemeController theme;
    QSignalSpy changed(&theme, &ThemeController::navigationSettingsChanged);

    QVERIFY(!theme.dualPaneEnabled());
    QCOMPARE(theme.splitRatio(), 0.5);
    theme.setDualPaneEnabled(true);
    theme.setSplitRatio(0.9);
    QVERIFY(theme.dualPaneEnabled());
    QCOMPARE(theme.splitRatio(), 0.75);
    QCOMPARE(changed.count(), 2);

    theme.setDualPaneEnabled(true);
    theme.setSplitRatio(0.75);
    QCOMPARE(changed.count(), 2);
}

void tst_ThemeController::corrupt_navigation_settings_fall_back_safely() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));
    QFile damaged(path);
    QVERIFY(damaged.open(QIODevice::WriteOnly));
    damaged.write("version=999\nplaces_count=2\nplace=%GG\nrecent_count=1\nrecent=%ZZ\n");
    damaged.close();

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.places().size(), 1);
    QCOMPARE(restored.places().front().toMap().value(QStringLiteral("path")).toString(),
             QStringLiteral("/"));
    QVERIFY(restored.recentDestinations().isEmpty());
}

void tst_ThemeController::state_persists_across_instances() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));

    {
        ThemeController theme;
        theme.setStoragePath(path);
        theme.setPaletteId(QStringLiteral("odyssey-aurora"));
        theme.setAccentPresetId(QStringLiteral("orchid"));
        theme.setProfile(ThemeController::Strong);
        theme.setDensity(ThemeController::Compact);
        theme.setUiScale(1.25);
        theme.setReducedMotion(true);
    }

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.paletteId(), QStringLiteral("odyssey-aurora"));
    QCOMPARE(restored.accentPresetId(), QStringLiteral("orchid"));
    QCOMPARE(restored.profile(), ThemeController::Strong);
    QCOMPARE(restored.density(), ThemeController::Compact);
    QCOMPARE(restored.uiScale(), 1.25);
    QVERIFY(restored.reducedMotion());
}

void tst_ThemeController::navigation_settings_persist_across_instances() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));
    {
        ThemeController theme;
        theme.setStoragePath(path);
        QVERIFY(theme.addPlace({{QStringLiteral("label"), QStringLiteral("Projects")},
                                {QStringLiteral("path"), QStringLiteral("/synthetic/projects")}}));
        QVERIFY(theme.recordRecentDestination(QStringLiteral("/synthetic/recent")));
        theme.setDualPaneEnabled(true);
        theme.setSplitRatio(0.65);
    }

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.places().size(), 2);
    QCOMPARE(restored.places().at(1).toMap().value(QStringLiteral("path")).toString(),
             QStringLiteral("/synthetic/projects"));
    QCOMPARE(restored.recentDestinations(), QStringList{QStringLiteral("/synthetic/recent")});
    QVERIFY(restored.dualPaneEnabled());
    QCOMPARE(restored.splitRatio(), 0.65);
}

void tst_ThemeController::reset_restores_and_persists_the_defaults() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));

    ThemeController theme;
    theme.setStoragePath(path);
    theme.setPaletteId(QStringLiteral("odyssey-amber"));
    theme.setAccentPresetId(QStringLiteral("verdant"));
    theme.setScanline(0.3);
    theme.setHighContrast(true);

    QSignalSpy changed(&theme, &ThemeController::appearanceChanged);
    theme.resetToDefaults();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(theme.paletteId(), QStringLiteral("odyssey-default"));
    QCOMPARE(theme.accentPresetId(), QStringLiteral("tideglass"));
    QCOMPARE(theme.profile(), ThemeController::Balanced);
    QVERIFY(!theme.highContrast());

    // A second reset from the default state is a no-op.
    theme.resetToDefaults();
    QCOMPARE(changed.count(), 1);

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.paletteId(), QStringLiteral("odyssey-default"));
    QCOMPARE(restored.accentPresetId(), QStringLiteral("tideglass"));
    QCOMPARE(restored.profile(), ThemeController::Balanced);
}

void tst_ThemeController::text_lift_brightens_chromatic_inks_from_effective_state() {
    ThemeController theme;

    // Baseline: Off renders the plain palette (effective lift one).
    theme.setProfile(ThemeController::Off);
    const QColor plain = theme.dirInk();
    const QColor plainBody = theme.text();

    // A stored lift brightens chromatic inks channel-wise, toward white.
    theme.setTextLift(1.4);
    const QColor lifted = theme.dirInk();
    QVERIFY(lifted.redF() >= plain.redF());
    QVERIFY(lifted.greenF() >= plain.greenF());
    QVERIFY(lifted.blueF() >= plain.blueF());
    QVERIFY(lifted != plain);

    // Neutral body text is exempt: the lift changes emphasis, not
    // legibility.
    QCOMPARE(theme.text(), plainBody);

    // High contrast pins the effective lift to one, so the ink returns to
    // the plain palette while the stored preference survives.
    theme.setHighContrast(true);
    QCOMPARE(theme.dirInk(), plain);
    QCOMPARE(theme.textLift(), 1.4);

    theme.setHighContrast(false);
    QCOMPARE(theme.dirInk(), lifted);
}

QTEST_MAIN(tst_ThemeController)
#include "tst_theme_controller.moc"

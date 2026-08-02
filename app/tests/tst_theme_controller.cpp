// Tests for the shell's appearance state.
//
// Covers what the appearance surface promises: every setter changes live
// state observably and immediately, profile and reset transitions behave,
// fonts resolve to real families, and state survives a restart through the
// configured storage path. Persistence runs against a temporary directory.
#include "theme_controller.hpp"
#include "theme_palettes.hpp"

#include <QFile>
#include <QFontDatabase>
#include <QIODevice>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtNumeric>
#include <QtTest>

using odysea::app::ThemeController;

class tst_ThemeController : public QObject {
    Q_OBJECT

  private slots:
    void defaults_are_the_shipped_configuration();
    void palettes_resolve_and_restyle();
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
    void no_op_writes_do_not_notify();
    void state_persists_across_instances();
    void reset_restores_and_persists_the_defaults();
    void text_lift_brightens_chromatic_inks_from_effective_state();
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
    QCOMPARE(theme.availablePalettes().size(), 6);
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
    QVERIFY(theme.effectiveBloomCore() > 0.0);

    theme.setHighContrast(false);
    QCOMPARE(theme.textMuted(), mutedBefore);
    QCOMPARE(theme.effectiveScanline(), theme.scanline());

    theme.setReducedMotion(true);
    QCOMPARE(theme.effectivePersistence(), 0.0);
    QVERIFY(theme.persistence() > 0.0);
    QVERIFY(theme.effectiveBloomCore() > 0.0);
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
    QVERIFY(theme.fontPixelSize() >= 9);

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
    QVERIFY(text.contains("version=1"));

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.paletteId(), QStringLiteral("odyssey-default"));
    QCOMPARE(restored.uiScale(), 1.0);
}

void tst_ThemeController::fonts_resolve_to_available_families() {
    ThemeController theme;
    QVERIFY(theme.bundledFontAvailable());
    QCOMPARE(theme.fontFamily(), QStringLiteral("Victor Mono"));

    theme.setFontSource(ThemeController::System);
    QCOMPARE(theme.fontFamily(), QFontDatabase::systemFont(QFontDatabase::FixedFont).family());

    // A named family that does not exist falls back to a real one.
    theme.setFontSource(ThemeController::Named);
    theme.setNamedFontFamily(QStringLiteral("No Such Family 123"));
    QVERIFY(QFontDatabase::hasFamily(theme.fontFamily()));

    // A named family that exists is honored.
    const QStringList families = QFontDatabase::families();
    QVERIFY(!families.isEmpty());
    theme.setNamedFontFamily(families.first());
    QCOMPARE(theme.fontFamily(), families.first());
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
    QCOMPARE(theme.fontPixelSize(), 26);
    QVERIFY(theme.gridCellWidth() > 144);

    theme.setUiScale(99.0);
    QCOMPARE(theme.uiScale(), 2.0);
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

void tst_ThemeController::state_persists_across_instances() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));

    {
        ThemeController theme;
        theme.setStoragePath(path);
        theme.setPaletteId(QStringLiteral("odyssey-aurora"));
        theme.setProfile(ThemeController::Strong);
        theme.setDensity(ThemeController::Compact);
        theme.setUiScale(1.25);
        theme.setReducedMotion(true);
    }

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.paletteId(), QStringLiteral("odyssey-aurora"));
    QCOMPARE(restored.profile(), ThemeController::Strong);
    QCOMPARE(restored.density(), ThemeController::Compact);
    QCOMPARE(restored.uiScale(), 1.25);
    QVERIFY(restored.reducedMotion());
}

void tst_ThemeController::reset_restores_and_persists_the_defaults() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("appearance.conf"));

    ThemeController theme;
    theme.setStoragePath(path);
    theme.setPaletteId(QStringLiteral("odyssey-amber"));
    theme.setScanline(0.3);
    theme.setHighContrast(true);

    QSignalSpy changed(&theme, &ThemeController::appearanceChanged);
    theme.resetToDefaults();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(theme.paletteId(), QStringLiteral("odyssey-default"));
    QCOMPARE(theme.profile(), ThemeController::Balanced);
    QVERIFY(!theme.highContrast());

    // A second reset from the default state is a no-op.
    theme.resetToDefaults();
    QCOMPARE(changed.count(), 1);

    ThemeController restored;
    restored.setStoragePath(path);
    QCOMPARE(restored.paletteId(), QStringLiteral("odyssey-default"));
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

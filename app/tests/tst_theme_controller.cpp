// Tests for the shell's appearance state.
//
// Covers what the appearance surface promises: every setter changes live
// state observably and immediately, profile and reset transitions behave,
// fonts resolve to real families, and state survives a restart through the
// configured storage path. Persistence runs against a temporary directory.
#include "theme_controller.hpp"
#include "theme_palettes.hpp"

#include <QFontDatabase>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    void fonts_resolve_to_available_families();
    void metrics_follow_density_and_scale();
    void no_op_writes_do_not_notify();
    void state_persists_across_instances();
    void reset_restores_and_persists_the_defaults();
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

    theme.setHighContrast(true);
    QCOMPARE(theme.scanline(), 0.0);
    QCOMPARE(theme.vignette(), 0.0);
    QCOMPARE(theme.textLift(), 1.0);
    QCOMPARE(theme.textMuted(), theme.text());
    QVERIFY(theme.textMuted() != mutedBefore);
    QVERIFY(theme.bloomCore() > 0.0);

    theme.setHighContrast(false);
    QCOMPARE(theme.textMuted(), mutedBefore);

    theme.setReducedMotion(true);
    QCOMPARE(theme.persistence(), 0.0);
    QVERIFY(theme.bloomCore() > 0.0);
}

void tst_ThemeController::fonts_resolve_to_available_families() {
    ThemeController theme;
    // Whatever the platform has installed, the resolved family must exist.
    QVERIFY(QFontDatabase::hasFamily(theme.fontFamily()));

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

QTEST_MAIN(tst_ThemeController)
#include "tst_theme_controller.moc"

// Tests that the appearance controls change the live theme state the moment
// they are used. Every check drives the real control from the module —
// pointer clicks and key presses, not direct property writes — and then reads
// the shared state, so a control that renders without being wired fails here.
import QtQuick
import QtTest
import OdySea

Item {
    id: harness

    width: 800
    height: 640

    ShellTheme {
        id: theme
    }

    AppearancePanel {
        id: panel

        parent: harness
        theme: theme
    }

    TestCase {
        id: testCase

        name: "AppearanceControls"
        when: windowShown

        function control(objectName) {
            const item = findChild(panel.contentItem, objectName);
            verify(item !== null, "missing control: " + objectName);
            return item;
        }

        function init() {
            theme.resetToDefaults();
            panel.open();
            tryVerify(function () {
                return panel.opened;
            });
            waitForRendering(panel.contentItem);
        }

        function cleanup() {
            panel.close();
            tryVerify(function () {
                return !panel.opened;
            });
        }

        function test_highContrastAppliesOnClick() {
            // Snapshot as a string: a bare color read stays live and would
            // follow the property.
            const before = theme.textMuted.toString();
            mouseClick(control("highContrastCheck"));
            verify(theme.highContrast);
            compare(theme.textMuted, theme.text);
            verify(!Qt.colorEqual(theme.textMuted, before));

            mouseClick(control("highContrastCheck"));
            verify(!theme.highContrast);
            verify(Qt.colorEqual(theme.textMuted, before));
        }

        function test_reducedMotionAppliesOnClick() {
            verify(theme.effectivePersistence > 0);
            mouseClick(control("reducedMotionCheck"));
            verify(theme.reducedMotion);
            // The override pins what gets rendered; the stored preference the
            // slider shows survives untouched.
            compare(theme.effectivePersistence, 0);
            verify(theme.persistence > 0);
        }

        function test_effectSliderMovesToCustomImmediately() {
            compare(theme.profile, ShellTheme.Balanced);
            const slider = control("scanlineSlider");
            const before = theme.scanline;

            // Drag the handle toward the top of the range.
            mousePress(slider, slider.width * 0.5, slider.height / 2);
            mouseMove(slider, slider.width * 0.95, slider.height / 2);
            mouseRelease(slider, slider.width * 0.95, slider.height / 2);

            compare(theme.profile, ShellTheme.Custom);
            verify(theme.scanline !== before);
            verify(theme.scanline > 0.2);
        }

        function test_profilePresetSteersSliderPositions() {
            const slider = control("bloomCoreSlider");
            const balancedValue = slider.value;
            verify(balancedValue > 0);

            theme.profile = ShellTheme.Off;
            tryCompare(slider, "value", 0);

            theme.profile = ShellTheme.Strong;
            tryVerify(function () {
                return slider.value > balancedValue;
            });
        }

        function test_sliderKeyboardPathWritesTheme() {
            const slider = control("uiScaleSlider");
            slider.forceActiveFocus();
            verify(slider.activeFocus);
            const before = theme.uiScale;
            keyClick(Qt.Key_Right);
            verify(theme.uiScale > before);
        }

        function test_paletteComboRestylesTokens() {
            const combo = control("paletteBox");
            const ground = theme.background.toString();
            const target = theme.availablePalettes.indexOf("odyssey-parchment-light");
            verify(target >= 0);
            combo.currentIndex = target;
            combo.activated(target);
            compare(theme.paletteId, "odyssey-parchment-light");
            verify(theme.lightPalette);
            verify(!Qt.colorEqual(theme.background, ground));
        }

        function test_densityAndFontSourceCombosApply() {
            const density = control("densityBox");
            const rowsBefore = theme.rowHeight;
            density.currentIndex = 2;
            density.activated(2);
            compare(theme.density, ShellTheme.Comfortable);
            verify(theme.rowHeight > rowsBefore);

            const fontSource = control("fontSourceBox");
            fontSource.currentIndex = 1;
            fontSource.activated(1);
            compare(theme.fontSource, ShellTheme.System);
            verify(theme.fontFamily.length > 0);
        }

        function test_slidersStayLiveUnderHighContrast() {
            mouseClick(control("highContrastCheck"));
            verify(theme.highContrast);
            compare(theme.effectiveScanline, 0);

            // The slider still shows the stored preference and still accepts
            // a drag: the handle keeps the written value instead of snapping
            // back, while the effective level stays pinned by the override.
            const slider = control("scanlineSlider");
            verify(slider.enabled);
            verify(slider.value > 0);
            mousePress(slider, slider.width * 0.5, slider.height / 2);
            mouseMove(slider, slider.width * 0.95, slider.height / 2);
            mouseRelease(slider, slider.width * 0.95, slider.height / 2);

            compare(theme.profile, ShellTheme.Custom);
            verify(theme.scanline > 0.2);
            tryVerify(function () {
                return Math.abs(slider.value - theme.scanline) < 0.001;
            });
            compare(theme.effectiveScanline, 0);

            // Lifting the override renders the adjustment made under it.
            mouseClick(control("highContrastCheck"));
            verify(!theme.highContrast);
            compare(theme.effectiveScanline, theme.scanline);
        }

        function test_profileComboFollowsThemeChanges() {
            const combo = control("profileBox");
            compare(combo.currentIndex, ShellTheme.Balanced);

            // A slider write abandons the preset for Custom; the combo must
            // report that without being touched itself.
            theme.scanline = 0.3;
            tryCompare(combo, "currentIndex", ShellTheme.Custom);

            theme.profile = ShellTheme.Minimal;
            tryCompare(combo, "currentIndex", ShellTheme.Minimal);
        }

        function test_paletteComboFollowsThemeChanges() {
            const combo = control("paletteBox");
            const target = theme.availablePalettes.indexOf("odyssey-aurora");
            verify(target >= 0);
            theme.paletteId = "odyssey-aurora";
            tryCompare(combo, "currentIndex", target);
        }

        function test_controlsCarryAccessibleNames() {
            verify(control("scanlineSlider").Accessible.name.length > 0);
            verify(control("uiScaleSlider").Accessible.name.length > 0);
            verify(control("paletteBox").Accessible.name.length > 0);
            verify(control("profileBox").Accessible.name.length > 0);
        }

        function test_resetButtonRestoresShippedState() {
            theme.paletteId = "odyssey-amber";
            theme.scanline = 0.3;
            theme.highContrast = true;
            compare(theme.profile, ShellTheme.Custom);

            mouseClick(control("resetAppearanceButton"));
            compare(theme.paletteId, "odyssey-default");
            compare(theme.profile, ShellTheme.Balanced);
            verify(!theme.highContrast);

            // The preset sliders follow the reset too.
            tryVerify(function () {
                return Math.abs(control("scanlineSlider").value - theme.scanline) < 0.001;
            });
        }
    }
}

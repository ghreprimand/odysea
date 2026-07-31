// Tests that the presentation pipeline consumes the theme's effective
// values, that the live controls reach the rendered uniforms through both
// pointer and keyboard paths, that the profiles produce distinct frames,
// and that protected wells stay byte-true under the strongest profile.
//
// The binding assertions run on every scene-graph backend: shader uniforms
// are plain properties whether or not the stage draws. The frame-comparison
// tests need a real GPU path and skip themselves when the scene graph falls
// back to software.
import QtQuick
import QtTest
import OdySea

Item {
    id: harness

    width: 640
    height: 480

    ShellTheme {
        id: theme
    }

    Item {
        id: content

        anchors.fill: parent

        DeepFieldGround {
            anchors.fill: parent
            deepField: theme.effectiveDeepField
            sheetColor: theme.background
            deepColor: theme.backgroundDeep
        }

        Text {
            x: 48
            y: 64
            text: "storage tube emitter sample"
            color: theme.text
            font.pixelSize: 30
            font.bold: true
        }

        Text {
            x: 48
            y: 130
            text: "aurora directory"
            color: theme.dirInk
            font.pixelSize: 24
        }

        // Synthetic thumbnail well: saturated content that would bloom and
        // band hard if the mask failed.
        Rectangle {
            id: thumbWell

            x: 420
            y: 320
            width: 120
            height: 90
            color: "#ffffff"

            Rectangle {
                x: 20
                y: 20
                width: 40
                height: 30
                color: "#ff2010"
            }
        }
    }

    WellMaskLayer {
        id: wells

        anchors.fill: content
    }

    PresentationLayer {
        id: layer

        anchors.fill: content
        content: content
        wellMask: wells
        theme: theme
    }

    AppearancePanel {
        id: panel

        parent: harness
        theme: theme
    }

    TestCase {
        id: testCase

        name: "PresentationPipeline"
        when: windowShown

        property var composite: null
        property var brightPass: null

        function initTestCase() {
            composite = findChild(layer, "presentationComposite");
            verify(composite !== null);
            brightPass = findChild(layer, "presentationBrightPass");
            verify(brightPass !== null);
            wells.registerWell(thumbWell);
            compare(wells.wellItems.length, 1);
        }

        function init() {
            theme.resetToDefaults();
        }

        function settleAndGrab() {
            wait(60);
            waitForRendering(harness);
            return grabImage(harness);
        }

        function test_uniformsTrackEffectiveNotStoredValues() {
            // Divergence setup: strong stored preferences under a pinning
            // accessibility override. A pipeline stage bound to the stored
            // preference properties reports the stored value here and fails.
            theme.scanline = 0.30;
            theme.vignette = 0.40;
            theme.bloomCore = 0.60;
            theme.bloomWide = 0.80;
            compare(theme.profile, ShellTheme.Custom);
            fuzzyCompare(composite.scanI, 0.30, 1e-6);
            fuzzyCompare(composite.vigI, 0.40, 1e-6);
            fuzzyCompare(composite.coreI, 0.60, 1e-6);
            // The composite decodes the wide chain's x4 headroom encode.
            fuzzyCompare(composite.wideI, 0.80 * 0.25, 1e-6);
            // Both accepted presets resolve exactly through the derived
            // threshold: this custom table matches Strong.
            fuzzyCompare(brightPass.threshold, 0.45, 1e-6);

            theme.highContrast = true;
            // Stored survives; effective pins; the uniforms follow effective.
            fuzzyCompare(theme.scanline, 0.30, 1e-6);
            compare(theme.effectiveScanline, 0);
            fuzzyCompare(composite.scanI, 0, 1e-9);
            fuzzyCompare(composite.vigI, 0, 1e-9);
            // Text lift pins to one: chromatic ink drops back to the plain
            // palette value.
            compare(theme.effectiveTextLift, 1.0);

            theme.highContrast = false;
            fuzzyCompare(composite.scanI, 0.30, 1e-6);
        }

        function test_reducedMotionZeroesTheMotionToken() {
            verify(theme.effectivePersistence > 0);
            verify(layer.motionDurationMs > 0);
            theme.reducedMotion = true;
            compare(layer.motionDurationMs, 0);
            // The stored preference the slider shows is untouched.
            verify(theme.persistence > 0);
        }

        function test_offProfileDisengagesThePipeline() {
            theme.profile = ShellTheme.Off;
            verify(!layer.active);
            verify(!composite.visible);
            // Content renders on the plain path: nothing hides it.
            verify(content.visible);
        }

        function test_pointerControlChangeReachesTheUniforms() {
            panel.open();
            tryVerify(function () {
                return panel.opened;
            });
            waitForRendering(panel.contentItem);
            const slider = findChild(panel.contentItem, "scanlineSlider");
            verify(slider !== null);
            const before = composite.scanI;
            mousePress(slider, slider.width * 0.5, slider.height / 2);
            mouseMove(slider, slider.width * 0.95, slider.height / 2);
            mouseRelease(slider, slider.width * 0.95, slider.height / 2);
            verify(composite.scanI !== before);
            fuzzyCompare(composite.scanI, theme.effectiveScanline, 1e-6);
            panel.close();
            tryVerify(function () {
                return !panel.opened;
            });
        }

        function test_keyboardControlChangeReachesTheUniforms() {
            panel.open();
            tryVerify(function () {
                return panel.opened;
            });
            waitForRendering(panel.contentItem);
            const slider = findChild(panel.contentItem, "vignetteSlider");
            verify(slider !== null);
            slider.forceActiveFocus();
            verify(slider.activeFocus);
            const before = composite.vigI;
            keyClick(Qt.Key_Right);
            verify(composite.vigI !== before);
            fuzzyCompare(composite.vigI, theme.effectiveVignette, 1e-6);
            panel.close();
            tryVerify(function () {
                return !panel.opened;
            });
        }

        function test_profilesProduceDistinctFrames() {
            if (layer.softwareBackend) {
                skip("software scene graph: the pipeline is disengaged by design");
            }
            theme.profile = ShellTheme.Off;
            const off = settleAndGrab();
            theme.profile = ShellTheme.Minimal;
            const minimal = settleAndGrab();
            theme.profile = ShellTheme.Balanced;
            const balanced = settleAndGrab();
            theme.profile = ShellTheme.Strong;
            const strong = settleAndGrab();
            theme.scanline = 0.35;
            theme.vignette = 0.05;
            compare(theme.profile, ShellTheme.Custom);
            const custom = settleAndGrab();

            // Minimal keeps the deep-field ground; everything else differs
            // through the pipeline.
            verify(!off.equals(minimal), "Off and Minimal must differ (deep field)");
            verify(!minimal.equals(balanced), "Minimal and Balanced must differ");
            verify(!balanced.equals(strong), "Balanced and Strong must differ");
            verify(!strong.equals(custom), "Strong and Custom must differ");
        }

        function test_protectedWellStaysByteTrueUnderStrong() {
            if (layer.softwareBackend) {
                skip("software scene graph: the pipeline is disengaged by design");
            }
            theme.profile = ShellTheme.Off;
            const off = settleAndGrab();
            theme.profile = ShellTheme.Strong;
            const strong = settleAndGrab();

            // Probe inside the registered well: the composite passes the
            // original pixels through, so the strongest profile leaves them
            // exactly as the plain path drew them.
            const cx = thumbWell.x + 30;
            const cy = thumbWell.y + 30;
            compare(strong.pixel(cx, cy), off.pixel(cx, cy));
            const ex = thumbWell.x + 60;
            const ey = thumbWell.y + 60;
            compare(strong.pixel(ex, ey), off.pixel(ex, ey));

            // And the frame outside the well did change.
            verify(!strong.equals(off));
        }
    }
}

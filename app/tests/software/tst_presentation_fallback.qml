// Software scene-graph fallback for the presentation pipeline. The runner
// forces QT_QUICK_BACKEND=software, which is exactly the environment of a
// machine without a usable GPU path: the pipeline must never engage, the
// content must stay visible and interactive, and the appearance controls
// must keep writing the stored preferences for when a capable backend
// returns. All of it silently — no crash, no warning flood, no blank frame.
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
            text: "fallback sample"
            color: theme.text
            font.pixelSize: 30
        }

        Rectangle {
            id: beacon

            x: 420
            y: 320
            width: 120
            height: 90
            color: "#ffffff"
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

    QuickPreviewModel {
        id: previewModel
    }

    QuickPreviewOverlay {
        id: quickPreview

        parent: harness
        previewModel: previewModel
        theme: theme
    }

    TestCase {
        id: testCase

        name: "PresentationSoftwareFallback"
        when: windowShown

        function init() {
            theme.resetToDefaults();
        }

        function test_pipelineNeverEngagesOnSoftware() {
            verify(layer.softwareBackend);
            compare(theme.profile, ShellTheme.Balanced);
            verify(theme.effectiveBloomCore > 0);
            // Levels demand effects, but the backend cannot render them:
            // the pipeline stays disengaged instead of drawing nothing.
            verify(!layer.active);
            verify(!layer.emissionActive);
            verify(!layer.contextGlowAvailable);
            const composite = findChild(layer, "presentationComposite");
            verify(composite !== null);
            verify(!composite.visible);
        }

        function test_contentStaysVisibleOnThePlainPath() {
            wait(40);
            waitForRendering(harness);
            const frame = grabImage(harness);
            // The white beacon renders exactly where the plain path puts it:
            // nothing hid the content while the pipeline stood down.
            compare(frame.pixel(beacon.x + 40, beacon.y + 40), Qt.rgba(1, 1, 1, 1));
        }

        function test_controlsKeepWritingStoredStateWithoutThePipeline() {
            panel.open();
            tryVerify(function () {
                return panel.opened;
            });
            waitForRendering(panel.contentItem);
            const slider = findChild(panel.contentItem, "scanlineSlider");
            verify(slider !== null);
            const before = theme.scanline;
            mousePress(slider, slider.width * 0.5, slider.height / 2);
            mouseMove(slider, slider.width * 0.95, slider.height / 2);
            mouseRelease(slider, slider.width * 0.95, slider.height / 2);
            verify(theme.scanline !== before);
            compare(theme.profile, ShellTheme.Custom);
            // Still no engagement, still no crash.
            verify(!layer.active);
            panel.close();
            tryVerify(function () {
                return !panel.opened;
            });
        }

        function test_protectedWellsSurviveTheFallback() {
            // Registration must stay harmless while the pipeline is down,
            // and the registered region renders exactly what the plain path
            // draws — the fallback drops the effects, never the protected
            // thumbnail and preview regions.
            wells.registerWell(beacon);
            compare(wells.wellCount, 1);
            wells.bump();
            wait(40);
            waitForRendering(harness);
            const frame = grabImage(harness);
            compare(frame.pixel(beacon.x + 40, beacon.y + 40), Qt.rgba(1, 1, 1, 1));
            wells.unregisterWell(beacon);
            compare(wells.wellCount, 0);
        }

        function test_quickPreviewSoftwareFallbackStaysVisibleAndInteractive() {
            verify(layer.softwareBackend);
            quickPreview.openFor("file:///preview-fixture/missing.txt");
            tryVerify(function () {
                return quickPreview.opened;
            });
            tryCompare(previewModel, "state", QuickPreviewModel.Error);
            const message = findChild(quickPreview.contentItem, "quickPreviewMessage");
            verify(message !== null);
            verify(message.visible);
            keyClick(Qt.Key_Escape);
            tryVerify(function () {
                return !quickPreview.opened;
            });
            tryCompare(previewModel, "state", QuickPreviewModel.Idle);
        }

        function test_materialAndMotionSurviveTheFallback() {
            // The still material layers and the motion token are geometry-
            // and palette-side, so the fallback keeps them.
            verify(theme.effectiveDeepField > 0);
            verify(layer.motionDurationMs > 0);
            theme.reducedMotion = true;
            compare(layer.motionDurationMs, 0);
        }
    }
}

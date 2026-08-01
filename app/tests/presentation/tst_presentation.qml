// Tests that the presentation pipeline consumes the theme's effective
// values, that the live controls reach the rendered uniforms through both
// pointer and keyboard paths, that the profiles produce distinct frames,
// that protected wells stay byte-true under the strongest profile, that a
// well scrolled out of its clipping viewport stops masking, that well
// registration is incremental, and that a failed shader stage latches the
// pipeline off and leaves the content on the plain path.
//
// The binding, registration, and geometry assertions run on every
// scene-graph backend. The frame-comparison and shader-failure tests need
// a real GPU path and skip themselves when the scene graph falls back to
// software.
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

    Component {
        id: wellFactory

        Rectangle {
            width: 20
            height: 20
            color: "#ffffff"
        }
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

        // Bright chrome band: pixels above the emission threshold that no
        // well may ever exempt from the pipeline.
        Rectangle {
            id: chromeBand

            x: 0
            y: 8
            width: parent.width
            height: 40
            color: "#e8e2d8"
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

        VectorIcon {
            id: toolbarIcon

            x: 48
            y: 190
            width: 24
            height: 24
            name: "folder"
            ink: theme.iconInk
            highContrast: theme.highContrast
        }

        // Clipped scrolling viewport: the synthetic stand-in for a grid
        // whose cache buffer keeps delegates realized beyond its bounds.
        Item {
            id: scrollViewport

            x: 300
            y: 200
            width: 200
            height: 150
            clip: true

            Item {
                id: scrollCanvas

                width: parent.width
                height: 600

                Rectangle {
                    id: scrolledWell

                    x: 30
                    y: 0
                    width: 120
                    height: 60
                    color: "#ffffff"
                }
            }
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
            compare(wells.wellCount, 1);
        }

        function init() {
            theme.resetToDefaults();
            scrollCanvas.y = 0;
            wells.bump();
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

        function test_registeringOneWellCreatesExactlyOneMirror() {
            // The reviewer-measured scale: a wide grid at 2x realizes on
            // the order of sixty thumbnails. Registering one more must
            // create exactly one mirror and leave every existing mirror
            // object untouched.
            const baseCount = wells.wellCount;
            const items = [];
            for (let i = 0; i < 60; ++i) {
                items.push(wellFactory.createObject(content, {
                    "x": 8 * (i % 30),
                    "y": 440 + 22 * Math.floor(i / 30)
                }));
                wells.registerWell(items[i]);
            }
            compare(wells.wellCount, baseCount + 60);
            const baseline = wells.mirrorCreationCount;
            const firstMirror = wells.mirrorFor(items[0]);
            verify(firstMirror !== null);

            const extra = wellFactory.createObject(content, {
                "x": 300,
                "y": 440
            });
            wells.registerWell(extra);
            compare(wells.mirrorCreationCount, baseline + 1);
            verify(wells.mirrorFor(items[0]) === firstMirror);

            // Re-registering is a no-op, not another mirror.
            wells.registerWell(extra);
            compare(wells.mirrorCreationCount, baseline + 1);

            wells.unregisterWell(extra);
            extra.destroy();
            for (let i = 0; i < items.length; ++i) {
                wells.unregisterWell(items[i]);
                items[i].destroy();
            }
            compare(wells.wellCount, baseCount);
        }

        function test_destroyedWellIsPrunedOnTheNextRegistration() {
            const baseCount = wells.wellCount;
            const doomed = wellFactory.createObject(content, {
                "x": 300,
                "y": 440
            });
            wells.registerWell(doomed);
            compare(wells.wellCount, baseCount + 1);
            const mirror = wells.mirrorFor(doomed);
            verify(mirror !== null);

            doomed.destroy();
            tryVerify(function () {
                return mirror.well === null;
            });
            // A destroyed well's mirror collapses to nothing once refreshed.
            wells.bump();
            tryVerify(function () {
                return mirror.height === 0 && mirror.width === 0;
            });

            // The next registration sweeps the stale record away.
            const replacement = wellFactory.createObject(content, {
                "x": 330,
                "y": 440
            });
            wells.registerWell(replacement);
            compare(wells.wellCount, baseCount + 1);
            wells.unregisterWell(replacement);
            replacement.destroy();
            compare(wells.wellCount, baseCount);
        }

        function test_mirrorClampsToItsViewport() {
            // Half-scrolled: the well spans mapped y 170..230, the viewport
            // starts at 200, so only the 30 px inside it may mask.
            scrollCanvas.y = -30;
            wells.registerWell(scrolledWell, scrollViewport);
            wells.bump();
            const mirror = wells.mirrorFor(scrolledWell);
            verify(mirror !== null);
            compare(mirror.y, scrollViewport.y);
            compare(mirror.height, 30);
            compare(mirror.x, scrollViewport.x + scrolledWell.x);
            compare(mirror.width, scrolledWell.width);

            // Fully scrolled out: the intersection is empty and the mirror
            // collapses to nothing.
            scrollCanvas.y = -180;
            wells.bump();
            tryVerify(function () {
                return mirror.height === 0;
            });

            wells.unregisterWell(scrolledWell);
        }

        function test_wellScrolledOutOfItsViewportLeavesChromeUntouched() {
            if (layer.softwareBackend) {
                skip("software scene graph: the pipeline is disengaged by design");
            }
            // The reviewer pixel case: a registered well whose delegate
            // stays realized in the cache buffer scrolls until its mapped
            // rectangle lands on the bright chrome band above the viewport.
            // The frame must render identically with and without the
            // registration — no exemption may reach the band.
            theme.profile = ShellTheme.Strong;
            scrollCanvas.y = -180;
            wells.registerWell(scrolledWell, scrollViewport);
            wells.bump();
            const registered = settleAndGrab();
            wells.unregisterWell(scrolledWell);
            const unregistered = settleAndGrab();
            verify(registered.equals(unregistered), "a well outside its viewport must not mask chrome");
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

        function test_subduedVectorIconStaysBelowStrongBrightPass() {
            theme.profile = ShellTheme.Strong;
            const brightest = Math.max(toolbarIcon.ink.r, toolbarIcon.ink.g, toolbarIcon.ink.b);
            verify(brightest < brightPass.threshold);

            const strong = settleAndGrab();
            const first = strong.pixel(toolbarIcon.x, toolbarIcon.y);
            let varied = false;
            for (let y = toolbarIcon.y; y < toolbarIcon.y + toolbarIcon.height && !varied; ++y) {
                for (let x = toolbarIcon.x; x < toolbarIcon.x + toolbarIcon.width; ++x) {
                    if (!Qt.colorEqual(strong.pixel(x, y), first)) {
                        varied = true;
                        break;
                    }
                }
            }
            verify(varied, "the Strong frame must contain the vector icon without promoting it to an emitter");
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

    // Shader-failure latch scenes. Each failure test poisons its layer for
    // the whole session — that is the latch's contract — so every test gets
    // its own scene. Fixed colors keep the plain path profile-independent,
    // which lets the latched frame compare byte-equal against an Off frame.
    component LatchScene: Item {
        id: latchScene

        readonly property alias sceneTheme: latchTheme
        readonly property alias sceneLayer: latchLayer
        readonly property alias sceneContent: latchContent

        width: 320
        height: 240
        visible: false

        ShellTheme {
            id: latchTheme
        }

        Item {
            id: latchContent

            anchors.fill: parent

            Rectangle {
                anchors.fill: parent
                color: "#14100c"
            }

            Text {
                x: 24
                y: 32
                text: "latch sample"
                color: "#e8e2d8"
                font.pixelSize: 24
                font.bold: true
            }

            Rectangle {
                x: 200
                y: 150
                width: 80
                height: 60
                color: "#ffffff"
            }
        }

        WellMaskLayer {
            id: latchWells

            anchors.fill: latchContent
        }

        PresentationLayer {
            id: latchLayer

            anchors.fill: latchContent
            content: latchContent
            wellMask: latchWells
            theme: latchTheme
        }
    }

    LatchScene {
        id: latchSceneA
    }

    LatchScene {
        id: latchSceneB
    }

    TestCase {
        name: "ShaderFailureLatch"
        when: windowShown

        function grabScene(scene) {
            wait(60);
            waitForRendering(scene);
            return grabImage(scene);
        }

        // Breaks one blur stage on a live scene and requires the latch to
        // stand the whole pipeline down onto the silent plain path.
        function exerciseLatch(scene, stageName) {
            if (scene.sceneLayer.softwareBackend) {
                skip("software scene graph: shader stages never compile");
            }
            scene.visible = true;
            scene.sceneTheme.resetToDefaults();
            scene.sceneTheme.profile = ShellTheme.Off;
            const plain = grabScene(scene);

            scene.sceneTheme.profile = ShellTheme.Strong;
            verify(scene.sceneLayer.active);
            waitForRendering(scene);

            const stage = findChild(scene.sceneLayer, stageName);
            verify(stage !== null);
            stage.fragmentShader = "shaders/no-such-stage.frag.qsb";
            tryVerify(function () {
                return scene.sceneLayer.shaderFailed;
            });
            verify(!scene.sceneLayer.pipelineAvailable);
            verify(!scene.sceneLayer.active);
            const composite = findChild(scene.sceneLayer, "presentationComposite");
            verify(composite !== null);
            verify(!composite.visible);
            verify(scene.sceneContent.visible);

            // The latched frame is the plain path, byte for byte: the
            // strongest profile with a broken stage renders exactly what
            // no pipeline at all renders.
            const latched = grabScene(scene);
            verify(latched.equals(plain), "a latched pipeline must leave the plain path untouched");
            scene.visible = false;
        }

        function test_failedCoreVerticalStageLatchesThePipelineOff() {
            exerciseLatch(latchSceneA, "presentationCoreV");
        }

        function test_failedWideVerticalStageLatchesThePipelineOff() {
            exerciseLatch(latchSceneB, "presentationWideV");
        }
    }
}

// GPU-only composed-scene accessibility audit.
//
// This separate TestCase keeps its one RHI-only assertion independently
// filterable: the RHI filter gate can require it to execute without pulling
// unrelated logical-layout checks onto a hardware-only path.
pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea
import "../support" as Support

Support.ShellTestCase {
    id: testCase

    name: "VisualAccessibility"

    function theme() {
        return testCase.shellWindow.shellTheme;
    }

    function composedEffectSources() {
        const sources = [];
        const presentationStages = ["presentationBrightPass", "presentationCoreH", "presentationCoreV", "presentationWideH", "presentationWideV", "presentationComposite"];

        function inspect(item) {
            if (item === null) {
                return;
            }
            if (item.glowEmitting === true) {
                sources.push(item.objectName.length > 0 ? item.objectName : "unnamed glow frame");
            }
            if (presentationStages.indexOf(item.objectName) !== -1 && item.visible) {
                sources.push(item.objectName);
            }
            for (let index = 0; index < item.children.length; ++index) {
                inspect(item.children[index]);
            }
        }

        inspect(testCase.shellWindow.contentItem);
        const list = child("directoryListView");
        const grid = child("directoryGridView");
        if (list.persistenceSourceActive) {
            sources.push("directory list persistence");
        }
        if (grid.persistenceSourceActive) {
            sources.push("directory grid persistence");
        }
        return sources;
    }

    function assertComposedEffectsAbsent(label) {
        const layer = child("presentationLayer");
        compare(layer.active, false, label + ": the presentation composite must be inactive");
        compare(layer.emissionActive, false, label + ": no bloom source may remain active");
        compare(layer.contextGlowAvailable, false, label + ": no context glow may remain available");
        compare(layer.motionDurationMs, 0, label + ": no persistence duration may remain");
        compare(composedEffectSources(), [], label + ": no composed effect source may remain");
    }

    function test_composedAccessibilityOverridesRemoveEveryEffectSource() {
        // The ordinary software pass proves fallback behavior but cannot tell
        // an accessibility override from the renderer's fallback.
        // qmllint disable unqualified
        if (typeof presentationRequireGpuFrames === "undefined" || !presentationRequireGpuFrames) {
            skip("the composed accessibility-source audit runs only through shell_visual_accessibility_rhi, whose launcher requires OpenGL RHI");
        }
        // qmllint enable unqualified
        const layer = child("presentationLayer");
        verify(!layer.softwareBackend, "the RHI accessibility gate reached a software scene graph");

        theme().profile = ShellTheme.Strong;
        waitForRendering(testCase.shellWindow.contentItem);
        tryVerify(function () {
            return layer.active && composedEffectSources().length > 0;
        });

        theme().reducedMotion = true;
        tryVerify(function () {
            return !layer.active;
        });
        assertComposedEffectsAbsent("reduced motion");

        theme().reducedMotion = false;
        theme().highContrast = true;
        tryVerify(function () {
            return !layer.active;
        });
        assertComposedEffectsAbsent("high contrast");

        theme().highContrast = false;
        theme().profile = ShellTheme.Off;
        tryVerify(function () {
            return !layer.active;
        });
        assertComposedEffectsAbsent("effects off");
    }
}

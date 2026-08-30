// Application-mark acceptance at launcher and taskbar sizes. The validation
// runner executes this scene under both the ordinary 1x layout and the
// declared QT_SCALE_FACTOR=2 layout; the test asserts the defining scale so a
// missing scaled run cannot masquerade as coverage.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtTest
import OdySea

Item {
    id: harness

    width: 180
    height: 100

    // qmllint disable unqualified
    readonly property real declaredScale: (typeof presentationExpectedFrameScale !== "undefined") ? presentationExpectedFrameScale : 0
    // qmllint enable unqualified
    readonly property bool renderedChildGrabsAvailable: declaredScale <= 1

    ShellTheme {
        id: theme
    }

    ApplicationMark {
        id: mark

        x: 12
        y: 12
        width: 16
        height: 16
        theme: theme
    }

    Image {
        id: desktopIcon

        x: 48
        y: 12
        width: 16
        height: 16
        source: "qrc:/qt/qml/OdySea/resources/icons/odysea.svg"
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
    }

    Image {
        id: symbolicIcon

        x: 84
        y: 12
        width: 16
        height: 16
        source: "qrc:/qt/qml/OdySea/resources/icons/odysea-symbolic.svg"
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
    }

    VectorIcon {
        id: monochromeMark

        x: 120
        y: 12
        width: 16
        height: 16
        name: "identity"
        ink: "#000000"
        outlineStrokeWidth: 1.8
    }

    TestCase {
        name: "ApplicationIdentityMark"
        when: windowShown

        function drawnBounds(image) {
            const background = image.pixel(0, 0);
            let left = image.width;
            let top = image.height;
            let right = -1;
            let bottom = -1;
            let count = 0;
            for (let y = 0; y < image.height; ++y) {
                for (let x = 0; x < image.width; ++x) {
                    const pixel = image.pixel(x, y);
                    const distance = Math.abs(pixel.r - background.r) + Math.abs(pixel.g - background.g) + Math.abs(pixel.b - background.b) + Math.abs(pixel.a - background.a);
                    if (distance > 0.06) {
                        left = Math.min(left, x);
                        top = Math.min(top, y);
                        right = Math.max(right, x);
                        bottom = Math.max(bottom, y);
                        ++count;
                    }
                }
            }
            return {
                "left": left,
                "top": top,
                "right": right,
                "bottom": bottom,
                "width": right >= left ? right - left + 1 : 0,
                "height": bottom >= top ? bottom - top + 1 : 0,
                "count": count
            };
        }

        function verifySmallMark(item, label, outlineOnly, expectMargin) {
            waitForRendering(item);
            const image = grabImage(item);
            const bounds = drawnBounds(image);
            verify(bounds.count > 0, label + " must render pixels");
            verify(bounds.width >= image.width * 0.66, label + " must retain a broad silhouette");
            verify(bounds.height >= image.height * 0.66, label + " must retain a tall silhouette");
            if (expectMargin) {
                verify(bounds.left > 0 && bounds.top > 0, label + " must not clip its upper or left stroke");
                verify(bounds.right < image.width - 1 && bounds.bottom < image.height - 1, label + " must not clip its lower or right stroke");
            }
            if (outlineOnly) {
                verify(bounds.count < image.width * image.height * 0.62, label + " must remain an open mark rather than a filled tile");
            }
        }

        function init() {
            theme.resetToDefaults();
            mark.width = 16;
            mark.height = 16;
            desktopIcon.width = 16;
            desktopIcon.height = 16;
            symbolicIcon.width = 16;
            symbolicIcon.height = 16;
            monochromeMark.width = 16;
            monochromeMark.height = 16;
        }

        function test_declaredMonitorScaleWasApplied() {
            const expected = harness.declaredScale > 0 ? harness.declaredScale : 1;
            compare(Screen.devicePixelRatio, expected);
            console.log("identity-mark scale evidence: logical layout at " + expected + "x; Screen.devicePixelRatio=" + Screen.devicePixelRatio);
        }

        function test_markSurvivesLauncherAndTaskbarSizes() {
            if (!harness.renderedChildGrabsAvailable) {
                skip("the offscreen software backend reports the declared 2x layout but returns empty child-item grabs; rendered 2x icon rasters are gated by app_application_icon_raster");
            }
            const sizes = [16, 20, 24, 32, 48];
            for (let index = 0; index < sizes.length; ++index) {
                const size = sizes[index];
                mark.width = size;
                mark.height = size;
                verifySmallMark(mark, "application mark at " + size + " logical px", true, false);

                symbolicIcon.width = size;
                symbolicIcon.height = size;
                tryCompare(symbolicIcon, "status", Image.Ready);
                verifySmallMark(symbolicIcon, "symbolic desktop icon at " + size + " logical px", true, false);

                desktopIcon.width = size;
                desktopIcon.height = size;
                tryCompare(desktopIcon, "status", Image.Ready);
                verifySmallMark(desktopIcon, "desktop icon at " + size + " logical px", false, false);
            }
        }

        function test_smallSizeGeometryAtDeclaredScale() {
            const path = mark.pathData;
            const scale = harness.declaredScale > 0 ? harness.declaredScale : 1;
            const sizes = [16, 20, 24, 32, 48];
            for (let index = 0; index < sizes.length; ++index) {
                const size = sizes[index];
                mark.width = size;
                mark.height = size;
                compare(mark.pathData, path);
                compare(mark.width, size);
                compare(mark.height, size);

                desktopIcon.width = size;
                desktopIcon.height = size;
                symbolicIcon.width = size;
                symbolicIcon.height = size;
                compare(desktopIcon.sourceSize.width, size * scale);
                compare(desktopIcon.sourceSize.height, size * scale);
                compare(symbolicIcon.sourceSize.width, size * scale);
                compare(symbolicIcon.sourceSize.height, size * scale);
            }
        }

        function test_monochromePathUsesOneInk() {
            if (!harness.renderedChildGrabsAvailable) {
                skip("the offscreen software backend returns empty child-item grabs at its declared 2x layout; the symbolic asset's rendered 2x monochrome pixels are gated by app_application_icon_raster");
            }
            waitForRendering(monochromeMark);
            const image = grabImage(monochromeMark);
            const background = image.pixel(0, 0);
            let compared = 0;
            for (let y = 0; y < image.height; ++y) {
                for (let x = 0; x < image.width; ++x) {
                    const pixel = image.pixel(x, y);
                    if (!Qt.colorEqual(pixel, background)) {
                        verify(Math.abs(pixel.r - pixel.g) < 0.03);
                        verify(Math.abs(pixel.g - pixel.b) < 0.03);
                        ++compared;
                    }
                }
            }
            verify(compared > 8, "the monochrome assertion must sample a rendered stroke");
            verifySmallMark(monochromeMark, "monochrome application mark", true, false);

            theme.highContrast = true;
            verifySmallMark(mark, "high-contrast application mark", true, false);
        }

        function test_geometrySurvivesProfilesAndAccentRoutingStaysDisconnected() {
            const path = mark.pathData;
            const ink = mark.ink.toString();
            const profiles = [ShellTheme.Off, ShellTheme.Minimal, ShellTheme.Balanced, ShellTheme.Strong, ShellTheme.Custom];
            for (let index = 0; index < profiles.length; ++index) {
                theme.profile = profiles[index];
                compare(mark.pathData, path);
                if (harness.renderedChildGrabsAvailable) {
                    verifySmallMark(mark, "profile " + index, true, false);
                }
            }

            // This loop pins a controller-routing contract, not preset color
            // data: every current and future model entry must remain
            // disconnected from iconInk and therefore from the product mark.
            for (let index = 0; index < theme.accentPresets.length; ++index) {
                theme.accentPresetIndex = index;
                compare(mark.pathData, path);
                compare(mark.ink.toString(), ink);
            }
        }

        function test_paletteAndHighContrastRestyleTheMark() {
            const defaultInk = mark.ink.toString();
            theme.paletteId = "odyssey";
            tryCompare(mark, "ink", theme.iconInk);
            verify(mark.ink.toString() !== defaultInk, "a palette change must reach the mark through iconInk");

            const paletteInk = mark.ink.toString();
            theme.highContrast = true;
            tryCompare(mark, "ink", theme.text);
            verify(mark.ink.toString() !== paletteInk, "high contrast must promote the mark to primary text ink");
        }
    }
}

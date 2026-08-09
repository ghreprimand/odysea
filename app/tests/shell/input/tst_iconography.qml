pragma ComponentBehavior: Bound
import QtQuick
import QtTest
import OdySea

Item {
    id: harness

    width: 120
    height: 80

    ShellTheme {
        id: theme
    }

    VectorIcon {
        id: sample

        x: 20
        y: 20
        width: 18
        height: 18
        name: "folder"
        ink: theme.iconInk
        highContrast: theme.highContrast
    }

    TestCase {
        name: "VectorIconography"
        when: windowShown

        function hasDrawnPixel(image) {
            for (let y = 0; y < image.height; ++y) {
                for (let x = 0; x < image.width; ++x) {
                    if (image.pixel(x, y).a > 0) {
                        return true;
                    }
                }
            }
            return false;
        }

        function init() {
            theme.resetToDefaults();
            sample.name = "folder";
            sample.width = 18;
            sample.height = 18;
        }

        function test_everySemanticSymbolHasVectorGeometry() {
            const names = ["folder", "file", "symlink", "back", "forward", "up", "refresh", "panes", "list", "grid", "appearance", "add", "close", "select-all", "copy", "move", "rename", "trash", "open", "commands", "search"];
            for (let index = 0; index < names.length; ++index) {
                sample.name = names[index];
                verify(sample.pathData.length > 0, names[index]);
            }
            sample.name = "unknown";
            compare(sample.pathData, "");
        }

        function test_sameGeometryRendersAtOneAndTwoTimesScale() {
            const path = sample.pathData;
            waitForRendering(harness);
            const oneX = grabImage(sample);
            verify(hasDrawnPixel(oneX));

            sample.width = 36;
            sample.height = 36;
            waitForRendering(harness);
            const twoX = grabImage(sample);
            verify(hasDrawnPixel(twoX));
            compare(sample.pathData, path);
            compare(twoX.width, oneX.width * 2);
            compare(twoX.height, oneX.height * 2);
        }

        function test_highContrastRecolorsWithoutChangingGeometry() {
            const path = sample.pathData;
            const normalInk = sample.ink.toString();
            theme.highContrast = true;
            compare(sample.pathData, path);
            compare(sample.ink, theme.text);
            verify(sample.ink.toString() !== normalInk);
        }
    }
}

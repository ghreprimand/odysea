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

    EntryIcon {
        id: entrySample

        x: 70
        y: 20
        width: 18
        height: 18
        directoryInk: theme.dirInk
        fileInk: theme.textFaint
        symbolicLinkInk: theme.linkInk
        highContrast: theme.highContrast
    }

    ApplicationMark {
        id: identitySample

        x: 45
        y: 52
        width: 18
        height: 18
        theme: theme
    }

    TestCase {
        name: "VectorIconography"
        when: windowShown

        function hasDrawnPixel(image) {
            const background = image.pixel(0, 0);
            for (let y = 0; y < image.height; ++y) {
                for (let x = 0; x < image.width; ++x) {
                    if (!Qt.colorEqual(image.pixel(x, y), background)) {
                        return true;
                    }
                }
            }
            return false;
        }

        function drawnPixelCount(image) {
            let count = 0;
            const background = image.pixel(0, 0);
            for (let y = 0; y < image.height; ++y) {
                for (let x = 0; x < image.width; ++x) {
                    if (!Qt.colorEqual(image.pixel(x, y), background)) {
                        ++count;
                    }
                }
            }
            return count;
        }

        function init() {
            theme.resetToDefaults();
            sample.name = "folder";
            sample.width = 18;
            sample.height = 18;
            entrySample.directory = false;
            entrySample.symbolicLink = false;
            entrySample.width = 18;
            entrySample.height = 18;
        }

        function test_everySemanticSymbolHasVectorGeometry() {
            const names = ["identity", "folder", "file", "symlink", "back", "forward", "up", "refresh", "panes", "list", "grid", "columns", "appearance", "add", "close", "select-all", "copy", "move", "rename", "trash", "open", "commands", "search"];
            for (let index = 0; index < names.length; ++index) {
                sample.name = names[index];
                verify(sample.pathData.length > 0, names[index]);
            }
            sample.name = "unknown";
            compare(sample.pathData, "");
        }

        function test_identityMarkUsesSharedAccentIndependentIconRole() {
            const path = identitySample.pathData;
            const ink = identitySample.ink.toString();
            compare(identitySample.name, "identity");
            verify(path.length > 0);
            for (let index = 0; index < theme.accentPresets.length; ++index) {
                theme.accentPresetIndex = index;
                compare(identitySample.pathData, path);
                compare(identitySample.ink.toString(), ink);
                compare(identitySample.ink, theme.iconInk);
            }

            theme.highContrast = true;
            compare(identitySample.pathData, path);
            compare(identitySample.ink, theme.text);
            compare(identitySample.outlineStrokeWidth, 2.35);
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

        function test_entryKindOwnsGeometryAndSemanticInk() {
            compare(entrySample.semanticName, "file");
            compare(entrySample.name, "file");
            compare(entrySample.semanticInk, theme.textFaint);
            compare(entrySample.ink, theme.textFaint);

            entrySample.directory = true;
            compare(entrySample.semanticName, "folder");
            compare(entrySample.name, "folder");
            compare(entrySample.semanticInk, theme.dirInk);
            compare(entrySample.ink, theme.dirInk);

            entrySample.symbolicLink = true;
            compare(entrySample.semanticName, "symlink");
            compare(entrySample.name, "symlink");
            compare(entrySample.semanticInk, theme.linkInk);
            compare(entrySample.ink, theme.linkInk);
        }

        function test_entryOutlineStaysThinAndOpenAtOneAndTwoTimesScale() {
            entrySample.directory = true;
            const path = entrySample.pathData;
            compare(entrySample.outlineStrokeWidth, 1.45);

            waitForRendering(harness);
            const oneX = grabImage(entrySample);
            const oneXDrawn = drawnPixelCount(oneX);
            verify(oneXDrawn > 0);
            verify(oneXDrawn < oneX.width * oneX.height / 2, "the outline must not become a filled glyph at 1x");

            entrySample.width = 36;
            entrySample.height = 36;
            waitForRendering(harness);
            const twoX = grabImage(entrySample);
            const twoXDrawn = drawnPixelCount(twoX);
            verify(twoXDrawn > oneXDrawn * 2, "the scaled outline must retain visible stroke coverage");
            verify(twoXDrawn < twoX.width * twoX.height / 2, "the outline must not become a filled glyph at 2x");
            compare(entrySample.pathData, path);
            compare(twoX.width, oneX.width * 2);
            compare(twoX.height, oneX.height * 2);
        }

        function test_entryOutlineSurvivesEveryProfileAndHighContrast() {
            entrySample.symbolicLink = true;
            const path = entrySample.pathData;
            const profiles = [ShellTheme.Off, ShellTheme.Minimal, ShellTheme.Balanced, ShellTheme.Strong, ShellTheme.Custom];
            for (let index = 0; index < profiles.length; ++index) {
                theme.profile = profiles[index];
                compare(entrySample.pathData, path);
                compare(entrySample.ink, theme.linkInk);
                waitForRendering(harness);
                verify(hasDrawnPixel(grabImage(entrySample)));
            }

            theme.highContrast = true;
            compare(entrySample.pathData, path);
            compare(entrySample.ink, theme.linkInk);
            compare(entrySample.outlineStrokeWidth, 2.2);
            waitForRendering(harness);
            verify(hasDrawnPixel(grabImage(entrySample)));
        }

        function test_entryRoleInkDoesNotFollowAccentPresets() {
            const initial = [theme.dirInk.toString(), theme.textFaint.toString(), theme.linkInk.toString()];
            for (let index = 0; index < theme.accentPresets.length; ++index) {
                theme.accentPresetIndex = index;
                compare(theme.dirInk.toString(), initial[0]);
                compare(theme.textFaint.toString(), initial[1]);
                compare(theme.linkInk.toString(), initial[2]);

                entrySample.directory = true;
                entrySample.symbolicLink = false;
                compare(entrySample.ink, theme.dirInk);
                entrySample.directory = false;
                compare(entrySample.ink, theme.textFaint);
                entrySample.symbolicLink = true;
                compare(entrySample.ink, theme.linkInk);
            }
        }
    }
}

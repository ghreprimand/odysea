// Device-pixel acceptance for the presentation pipeline.
//
// Mask geometry must stay logical, the frame must render at the full
// device resolution, and a protected well must stay byte-true to the plain
// path along its entire border — a misaligned mask leaves a processed seam
// on the outermost well row and fails the border sweep, which an emitter
// ring outside the well arms in every direction. Protection is also bounded
// from the outside: the ring one device pixel beyond a well sits on
// receiving material and must stay processed, so an oversized mask fails
// the outward sweep instead of silently exempting surrounding chrome. The
// frame comparisons need a real GPU path and skip themselves on the
// software scene graph.
//
// Scale coverage is split by what each environment can honestly render.
// The offscreen platform never allocates a genuine high-density
// framebuffer: under QT_SCALE_FACTOR=2 it reports a device pixel ratio of
// two while rasterizing at 1x, and a grabbed frame is a device-sized
// canvas that does not carry the scene at device resolution. The pixel
// assertions therefore gate the real GPU path at 1x, the logical-geometry
// assertions run at both scales, and the same suite run on a windowing
// system with a real 2x surface exercises every assertion at full density
// — the vacuity sentinel in the border sweep rejects any environment that
// grabs a frame without the rendered scene in it.
import QtQuick
import QtQuick.Window
import QtTest
import OdySea

Item {
    id: harness

    width: 320
    height: 240

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
            x: 24
            y: 32
            text: "device pixel sample"
            color: theme.text
            font.pixelSize: 24
            font.bold: true
        }

        // An emitter ring hugging all four well edges: its bloom spills
        // across every boundary, so a mask misaligned in any direction
        // feeds added light onto a border row the sweep below checks byte
        // for byte.
        Rectangle {
            x: thumbWell.x - 12
            y: thumbWell.y - 12
            width: thumbWell.width + 24
            height: thumbWell.height + 24
            color: "#ffffff"
        }

        // Protected well in a mid tone: dark enough that received bloom
        // changes its bytes instead of clamping at white, so the border
        // sweep can see a half-pixel mask seam.
        Rectangle {
            id: thumbWell

            x: 180
            y: 120
            width: 100
            height: 80
            color: "#303030"

            Rectangle {
                x: 16
                y: 16
                width: 40
                height: 30
                color: "#ff2010"
            }
        }

        // Second protected well, armed for the opposite direction: an
        // emitter frame at a distance, with a dark gutter between it and
        // the well, so the ring one device pixel OUTSIDE the well sits on
        // receiving material instead of on the saturated emitter — white
        // cannot brighten, so a ring hugging an emitter can never reveal an
        // oversized mask. Every gutter pixel takes measurable bloom, which
        // makes wrongful protection visible: a mask even half a device
        // pixel too large leaves part of that ring byte-equal to the plain
        // path and fails the outward sweep.
        Rectangle {
            id: gutterEmitter

            x: gutterWell.x - 16
            y: gutterWell.y - 16
            width: gutterWell.width + 32
            height: gutterWell.height + 32
            color: "#ffffff"
        }

        Rectangle {
            x: gutterWell.x - 3
            y: gutterWell.y - 3
            width: gutterWell.width + 6
            height: gutterWell.height + 6
            color: "#101010"
        }

        Rectangle {
            id: gutterWell

            x: 40
            y: 120
            width: 60
            height: 60
            color: "#303030"
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

    TestCase {
        id: testCase

        name: "DevicePixelScaling"
        when: windowShown

        property real dpr: 1

        function initTestCase() {
            dpr = Screen.devicePixelRatio;
            verify(dpr >= 1);
            wells.registerWell(thumbWell);
            wells.registerWell(gutterWell);
            compare(wells.wellCount, 2);
        }

        function init() {
            theme.resetToDefaults();
        }

        function settleAndGrab() {
            wait(60);
            waitForRendering(harness);
            return grabImage(harness);
        }

        function test_maskGeometryStaysLogical() {
            // Mirror geometry is logical-coordinate math: it must equal the
            // well's rectangle exactly, at every device pixel ratio.
            wells.bump();
            const mirror = wells.mirrorFor(thumbWell);
            verify(mirror !== null);
            compare(mirror.x, thumbWell.x);
            compare(mirror.y, thumbWell.y);
            compare(mirror.width, thumbWell.width);
            compare(mirror.height, thumbWell.height);
        }

        function test_frameRendersAtDeviceResolution() {
            if (layer.softwareBackend) {
                skip("software scene graph: the software rasterizer grabs at logical resolution");
            }
            const frame = settleAndGrab();
            // The grabbed frame carries the full device resolution: logical
            // size times the device pixel ratio, which is what crisp-core
            // text renders into. A pipeline that rendered at logical
            // resolution and upscaled would fail here at 2x.
            compare(frame.width, Math.round(harness.width * testCase.dpr));
            compare(frame.height, Math.round(harness.height * testCase.dpr));
        }

        function test_wellBorderStaysByteTrueAtDeviceResolution() {
            if (layer.softwareBackend) {
                skip("software scene graph: the pipeline is disengaged by design");
            }
            // Both states pin the same deep-field level, so the two frames
            // may differ only through the pipeline itself: the engagement
            // check below cannot be satisfied by the still material.
            theme.profile = ShellTheme.Off;
            theme.deepField = 0.3;
            compare(theme.profile, ShellTheme.Custom);
            const off = settleAndGrab();

            // Vacuity sentinel: a grab that fails to carry the rendered
            // scene — a blank or misplaced frame — must fail loudly here
            // instead of letting every byte comparison pass over nothing.
            const sx = Math.round((thumbWell.x - 6) * testCase.dpr);
            const sy = Math.round((thumbWell.y + 40) * testCase.dpr);
            compare(off.pixel(sx, sy), Qt.rgba(1, 1, 1, 1));

            theme.bloomCore = 0.6;
            theme.bloomWide = 0.8;
            theme.scanline = 0.3;
            theme.vignette = 0.4;
            verify(layer.active);
            const strong = settleAndGrab();
            verify(!strong.equals(off), "the pipeline must reach the frame");

            // Sweep the well's entire innermost border in device pixels:
            // every pixel must match the plain path byte for byte. The
            // emitter ring outside guarantees added light presses against
            // every edge, so a mask misaligned in any direction feeds a
            // border row and fails the sweep.
            const left = Math.round(thumbWell.x * testCase.dpr);
            const top = Math.round(thumbWell.y * testCase.dpr);
            const right = Math.round((thumbWell.x + thumbWell.width) * testCase.dpr) - 1;
            const bottom = Math.round((thumbWell.y + thumbWell.height) * testCase.dpr) - 1;
            for (let x = left; x <= right; ++x) {
                compare(strong.pixel(x, top), off.pixel(x, top));
                compare(strong.pixel(x, bottom), off.pixel(x, bottom));
            }
            for (let y = top; y <= bottom; ++y) {
                compare(strong.pixel(left, y), off.pixel(left, y));
                compare(strong.pixel(right, y), off.pixel(right, y));
            }

            // And the well's interior, sampled at its center and across the
            // saturated patch, stays byte-true as well.
            const cx = Math.round((thumbWell.x + 36) * testCase.dpr);
            const cy = Math.round((thumbWell.y + 31) * testCase.dpr);
            compare(strong.pixel(cx, cy), off.pixel(cx, cy));
        }

        function test_ringOutsideWellStaysProcessed() {
            if (layer.softwareBackend) {
                skip("software scene graph: the pipeline is disengaged by design");
            }
            // The inner sweep above catches a mask that is too small; this
            // sweep catches one that is too large. Nothing else in the gate
            // requires a pixel just outside a well to be processed, so
            // without it an oversized mask silently exempts a growing band
            // of chrome and every inner assertion stays green.
            theme.profile = ShellTheme.Off;
            theme.deepField = 0.3;
            compare(theme.profile, ShellTheme.Custom);
            const off = settleAndGrab();

            // Vacuity sentinel on the second harness: the emitter frame is
            // white on the plain path.
            const sx = Math.round((gutterWell.x - 8) * testCase.dpr);
            const sy = Math.round((gutterWell.y + 30) * testCase.dpr);
            compare(off.pixel(sx, sy), Qt.rgba(1, 1, 1, 1));

            theme.bloomCore = 0.6;
            theme.bloomWide = 0.8;
            theme.scanline = 0.3;
            theme.vignette = 0.4;
            verify(layer.active);
            const strong = settleAndGrab();

            // Sweep the ring one device pixel outside the well. Every pixel
            // sits on dark gutter material that receives bloom from the
            // emitter frame, so the pipeline must change each one; a pixel
            // byte-equal to the plain path here is wrongly protected. This
            // is what pins the mask's outer tolerance: an oversize of half
            // a device pixel protects part of this ring, one full pixel
            // protects all of it, and both fail loudly.
            const left = Math.round(gutterWell.x * testCase.dpr) - 1;
            const top = Math.round(gutterWell.y * testCase.dpr) - 1;
            const right = Math.round((gutterWell.x + gutterWell.width) * testCase.dpr);
            const bottom = Math.round((gutterWell.y + gutterWell.height) * testCase.dpr);
            for (let x = left; x <= right; ++x) {
                verify(!Qt.colorEqual(strong.pixel(x, top), off.pixel(x, top)), "over-protected device pixel at " + x + "," + top);
                verify(!Qt.colorEqual(strong.pixel(x, bottom), off.pixel(x, bottom)), "over-protected device pixel at " + x + "," + bottom);
            }
            for (let y = top; y <= bottom; ++y) {
                verify(!Qt.colorEqual(strong.pixel(left, y), off.pixel(left, y)), "over-protected device pixel at " + left + "," + y);
                verify(!Qt.colorEqual(strong.pixel(right, y), off.pixel(right, y)), "over-protected device pixel at " + right + "," + y);
            }

            // The well itself stays byte-true: the outward requirement must
            // not be satisfiable by shrinking the mask.
            const wx = Math.round((gutterWell.x + 30) * testCase.dpr);
            const wy = Math.round((gutterWell.y + 30) * testCase.dpr);
            compare(strong.pixel(wx, wy), off.pixel(wx, wy));
        }
    }
}

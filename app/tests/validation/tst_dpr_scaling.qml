// Device-pixel acceptance for the presentation pipeline.
//
// Mask geometry must stay logical, the frame must render at the full
// device resolution, and a protected well must stay byte-true to the plain
// path along its entire border — a misaligned mask leaves a processed seam
// on the outermost well row and fails the border sweep, which an emitter
// ring outside the well arms in every direction. The frame comparisons
// need a real GPU path and skip themselves on the software scene graph.
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
            compare(wells.wellCount, 1);
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
    }
}

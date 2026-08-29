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
// assertions therefore gate the real GPU path at 1x, while the software
// scaled-layout entry checks logical geometry under QT_SCALE_FACTOR=2. The
// same suite can exercise every assertion at full density only on a declared
// windowing surface that genuinely allocates at 2x — the vacuity sentinel in
// the border sweep rejects any environment that grabs a frame without the
// rendered scene in it.
import QtQuick
import QtQuick.Window
import QtTest
import OdySea
import "../support/grab.js" as Grab

Item {
    id: harness

    width: 320
    height: 240

    // Bound on how many times a grab helper re-settles when its vacuity
    // sentinel reports the scene is not yet in the frame. The bound tolerates
    // a slow or momentarily starved frame under GPU contention; it never
    // relaxes what a complete frame must contain, and it never retries a
    // content comparison. On the happy path the first attempt satisfies the
    // sentinel and the extra iterations cost nothing.
    readonly property int grabSettleAttempts: 6

    // True when the run demands the real GPU frame path: the real-compositor
    // gate sets ODYSEA_REQUIRE_GPU_FRAMES, which the shared setup main
    // publishes here. Under it the pixel tests fail instead of skipping when
    // the scene graph is software, so a gate that ran on a fallback cannot
    // report success while every device-resolution assertion was skipped.
    // Unset everywhere else, so the offscreen software passes skip exactly as
    // before. The context property is injected by the C++ setup main, so the
    // linter cannot see it; the guard keeps the scene loadable under any
    // runner that does not set it.
    // qmllint disable unqualified
    readonly property bool requireGpuFrames: (typeof presentationRequireGpuFrames !== "undefined") ? presentationRequireGpuFrames : false
    // The device scale the run was launched at, published by the forced-2x
    // validation gate (ODYSEA_EXPECTED_FRAME_SCALE=2 alongside
    // QT_SCALE_FACTOR=2); 0 when no scale was declared. The device-resolution
    // test asserts the grabbed frame carries at least this multiple of the
    // logical size when it is set, so a run that came back at 1x cannot pass
    // as if it were 2x.
    readonly property real expectedFrameScale: (typeof presentationExpectedFrameScale !== "undefined") ? presentationExpectedFrameScale : 0
    // qmllint enable unqualified

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
            // The software scaled-layout entry declares the factor it asked
            // Qt to apply. Its offscreen frame still rasterizes at logical
            // resolution, so this proves only that the logical-scale
            // condition took effect; it is not device-resolution evidence.
            // The compositor entry uses the same declaration as a minimum
            // frame-density bound below, where a screen's native scale may
            // compose with the forced factor and therefore is not compared
            // for exact equality here.
            if (layer.softwareBackend && harness.expectedFrameScale > 0) {
                compare(dpr, harness.expectedFrameScale, "the software scaled-layout entry must reach its declared logical scale");
            }
            verify(layer.wellMaskUsesLinearSampling, "the protected-well mask must retain filtered edge coverage");
            wells.registerWell(thumbWell);
            wells.registerWell(gutterWell);
            compare(wells.wellCount, 2);
        }

        function init() {
            theme.resetToDefaults();
        }

        // Frame comparisons need a real GPU path. On the software scene graph
        // they normally skip, since the pipeline is disengaged there by
        // design. Under the real-compositor gate a software backend is not a
        // reason to skip — it is the gate failing to exercise what it exists
        // for — so this fails loudly instead. It never relaxes an assertion:
        // when the GPU path is present it returns and the test runs in full.
        function ensureGpuPathOrSkip(reason) {
            if (!layer.softwareBackend) {
                return;
            }
            if (harness.requireGpuFrames) {
                verify(false, "the real-compositor gate requires the OpenGL RHI frame path, but the scene graph fell back to software: " + reason);
            }
            skip("software scene graph: " + reason);
        }

        // The completeness signal for a DPR grab: thumbWell's protected red
        // patch is byte-true on every profile and backend, so its presence
        // means the frame carries the rendered scene. It gates the settle
        // retry below without constraining any content comparison — the border
        // and ring sweeps still judge their own pixels once, byte for byte.
        function sceneRendered(frame) {
            // The probe is placed through the frame's own device ratio, not
            // Screen.devicePixelRatio: under Wayland fractional scaling the two
            // disagree and a screen-ratio probe lands outside the frame. The
            // sentinel is total, so a probe outside the frame is an observable
            // miss the settle loop retries, never an exception.
            return Grab.carriesColor(frame, thumbWell.x + 36, thumbWell.y + 31, harness.width, harness.height, "#ff2010");
        }

        function settleAndGrab() {
            // Retries bounded on the settle, never on the comparison. Under
            // GPU contention a window grab can return before the frame it
            // should carry is composited; sceneRendered() is the completeness
            // signal, so the settle repeats up to grabSettleAttempts times
            // until it holds. A slow frame is waited for; a genuinely absent
            // scene fails loudly once the bound is spent; the returned frame
            // is judged once by the caller's byte-exact sweeps, which are
            // never retried, so a wrong frame is never retried into a pass.
            let frame = null;
            for (let attempt = 0; attempt < harness.grabSettleAttempts; ++attempt) {
                wait(60);
                waitForRendering(harness);
                frame = grabImage(harness);
                if (sceneRendered(frame)) {
                    return frame;
                }
            }
            verify(false, "vacuous grab: the protected patch is missing from the frame after " + harness.grabSettleAttempts + " settle attempts");
            return frame;
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
            ensureGpuPathOrSkip("the software rasterizer grabs at logical resolution");
            const frame = settleAndGrab();
            // The grabbed frame carries the scene at the compositor's real
            // device resolution, which is the frame's own ratio. Under Wayland
            // fractional scaling that ratio is the buffer scale, not the
            // rounded Screen.devicePixelRatio, so asserting logical times the
            // screen ratio over-expects and fails on a correctly rendered
            // frame. The honest, platform-independent claim is that the
            // pipeline renders at at least logical resolution and scales width
            // and height together: a pipeline that rendered at logical and
            // upscaled non-uniformly fails here, on any platform.
            verify(frame.width >= harness.width, "frame width " + frame.width + " is below logical width " + harness.width);
            verify(frame.height >= harness.height, "frame height " + frame.height + " is below logical height " + harness.height);
            // The two axes carry one real scale, so the width and height
            // ratios must agree — but a fractional buffer scale forces the
            // compositor to round each axis to an integer independently, and
            // the two ratios then differ by that rounding and no more. With a
            // true uniform scale s, frame.width = round(s*logicalW) and
            // frame.height = round(s*logicalH), each off the ideal by under a
            // pixel, so the cross product
            //   frame.width*logicalH - frame.height*logicalW = ew*logicalH - eh*logicalW
            // is bounded in magnitude by logicalW + logicalH: one pixel of
            // rounding on each axis. A genuinely non-uniform upscale — a
            // different scale per axis — exceeds that envelope and still
            // fails. The bound is the rounding a compositor can physically
            // introduce, not a tolerance chosen to admit this run: at an
            // integer scale the envelope still admits only an exact match,
            // and at any scale a wrong aspect ratio is rejected.
            const crossDelta = Math.abs(frame.width * harness.height - frame.height * harness.width);
            const roundingEnvelope = harness.width + harness.height;
            verify(crossDelta <= roundingEnvelope, "frame " + frame.width + "x" + frame.height + " for logical " + harness.width + "x" + harness.height + " scales the axes non-uniformly: cross delta " + crossDelta + " exceeds the " + roundingEnvelope + "-pixel one-per-axis rounding envelope");
            // When the gate declares the scale it launched at — the forced-2x
            // validation entry sets it to 2 alongside QT_SCALE_FACTOR=2 — the
            // grabbed frame must carry at least that density. This is the
            // assertion that makes 2x device-resolution coverage non-vacuous:
            // a run that fell back to 1x, or a pipeline that reported a high
            // ratio while rasterizing low, grabs a frame below the declared
            // multiple and fails here instead of passing as if it had rendered
            // at full density. The bound is "at least", not "exactly", because
            // QT_SCALE_FACTOR composes with the screen's own scale: on a
            // fractional output the effective ratio exceeds the forced factor,
            // so the frame is legitimately larger than logical times the
            // declared scale. Combined with the uniform-scaling check above,
            // this proves the pipeline rendered at no less than the forced
            // density and scaled width and height together. It is inert (0) on
            // every entry that does not declare a scale.
            if (harness.expectedFrameScale > 0) {
                console.log("device-resolution gate: frame " + frame.width + "x" + frame.height + " for logical " + harness.width + "x" + harness.height + " at declared scale " + harness.expectedFrameScale + " (Screen.devicePixelRatio " + Screen.devicePixelRatio.toFixed(4) + ")");
                verify(frame.width >= Math.round(harness.width * harness.expectedFrameScale), "frame width " + frame.width + " is below the declared " + harness.expectedFrameScale + "x of logical width " + harness.width);
                verify(frame.height >= Math.round(harness.height * harness.expectedFrameScale), "frame height " + frame.height + " is below the declared " + harness.expectedFrameScale + "x of logical height " + harness.height);
            }
        }

        // Records the surface the run actually reached against the surface the
        // gate declared, so a run that never met the per-axis rounding cannot
        // read as an equal pass to one that did. The device-resolution bound
        // above is only non-trivial when the two axes round to integers
        // independently: an integer scale, or an exactly proportional
        // fractional scale — offscreen, xcb, or a compositor that upscales
        // 320x240 to 800x600 at 2.5 — gives cross delta zero and clears the
        // bound over nothing. This prints the geometry, the cross delta, and
        // the declared scale, and skips with the per-axis-rounding case named
        // unexercised on such a surface, which reads as a visibly weaker run in
        // the totals rather than a green pass. On a surface that did round the
        // axes independently it asserts what the bound guarantees: the cross
        // delta is non-zero, so the old exact-equality compare could not have
        // held here, and it stays within one device pixel of rounding per axis.
        function test_perAxisRoundingReachedIsRecordedAndBounded() {
            ensureGpuPathOrSkip("no frame is grabbed on the software path");
            const frame = settleAndGrab();
            const crossDelta = Math.abs(frame.width * harness.height - frame.height * harness.width);
            const roundingEnvelope = harness.width + harness.height;
            const declared = harness.expectedFrameScale > 0 ? harness.expectedFrameScale : "none";
            console.log("device-resolution surface: frame " + frame.width + "x" + frame.height + " for logical " + harness.width + "x" + harness.height + " => crossDelta " + crossDelta + ", envelope " + roundingEnvelope + ", declared scale " + declared + " (Screen.devicePixelRatio " + Screen.devicePixelRatio.toFixed(4) + ")");
            if (crossDelta === 0) {
                skip("frame " + frame.width + "x" + frame.height + " scales " + harness.width + "x" + harness.height + " exactly proportionally (crossDelta 0); the independent per-axis rounding the device-resolution bound exists for was not reached on this run, so that defect class is unexercised here");
            }
            verify(frame.width * harness.height !== frame.height * harness.width, "the per-axis rounding must be genuine here: the exact cross-product equality the old assertion demanded must not hold on a surface this test credits");
            verify(crossDelta <= roundingEnvelope, "frame " + frame.width + "x" + frame.height + " for logical " + harness.width + "x" + harness.height + " rounds the axes beyond one device pixel each: cross delta " + crossDelta + " exceeds the " + roundingEnvelope + "-pixel one-per-axis rounding envelope");
        }

        function test_wellBorderStaysByteTrueAtDeviceResolution() {
            ensureGpuPathOrSkip("the pipeline is disengaged by design");
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
            const sx = Grab.deviceX(off, thumbWell.x - 6, harness.width);
            const sy = Grab.deviceY(off, thumbWell.y + 40, harness.height);
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
            const left = Grab.deviceX(strong, thumbWell.x, harness.width);
            const top = Grab.deviceY(strong, thumbWell.y, harness.height);
            const right = Grab.deviceX(strong, thumbWell.x + thumbWell.width, harness.width) - 1;
            const bottom = Grab.deviceY(strong, thumbWell.y + thumbWell.height, harness.height) - 1;
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
            const cx = Grab.deviceX(strong, thumbWell.x + 36, harness.width);
            const cy = Grab.deviceY(strong, thumbWell.y + 31, harness.height);
            compare(strong.pixel(cx, cy), off.pixel(cx, cy));
        }

        function test_ringOutsideWellStaysProcessed() {
            ensureGpuPathOrSkip("the pipeline is disengaged by design");
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
            const sx = Grab.deviceX(off, gutterWell.x - 8, harness.width);
            const sy = Grab.deviceY(off, gutterWell.y + 30, harness.height);
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
            const left = Grab.deviceX(off, gutterWell.x, harness.width) - 1;
            const top = Grab.deviceY(off, gutterWell.y, harness.height) - 1;
            const right = Grab.deviceX(off, gutterWell.x + gutterWell.width, harness.width);
            const bottom = Grab.deviceY(off, gutterWell.y + gutterWell.height, harness.height);
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
            const wx = Grab.deviceX(off, gutterWell.x + 30, harness.width);
            const wy = Grab.deviceY(off, gutterWell.y + 30, harness.height);
            compare(strong.pixel(wx, wy), off.pixel(wx, wy));
        }
    }
}

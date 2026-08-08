// Shared grab helpers for the frame-comparing suites.
//
// A grabbed frame's device-per-logical ratio must be measured from the frame
// itself, never assumed from Screen.devicePixelRatio. The two disagree under
// Wayland fractional scaling: the screen reports a rounded-up integer ratio
// (for example 2) while the compositor renders the buffer at the fractional
// ratio (for example 1.25), so a 640x480 harness is grabbed as an 800x600
// image. A probe placed with the screen ratio then lands outside the frame,
// frame.pixel() returns undefined, and Qt.colorEqual() raises. Deriving the
// ratio from frame.width / logicalWidth is correct on every platform: under
// xcb the two ratios coincide, and under Wayland fractional scaling only the
// frame-derived one addresses the pixels that were actually rendered.
.pragma library

// The device x for a logical x, using the frame's own horizontal ratio.
function deviceX(frame, logicalX, logicalWidth) {
    return Math.round(logicalX * frame.width / logicalWidth);
}

// The device y for a logical y, using the frame's own vertical ratio.
function deviceY(frame, logicalY, logicalHeight) {
    return Math.round(logicalY * frame.height / logicalHeight);
}

// The frame's pixel at a logical coordinate, converted through the frame's own
// ratio. Callers that compare content between two frames of the same subject
// use this so both are sampled at the same rendered point.
function pixelAt(frame, logicalX, logicalY, logicalWidth, logicalHeight) {
    return frame.pixel(deviceX(frame, logicalX, logicalWidth),
                       deviceY(frame, logicalY, logicalHeight));
}

// A total vacuity sentinel: true only when the frame carries `color` at the
// device point for the given logical coordinate. A missing frame, a probe
// outside the frame, or an undefined pixel is a definite miss the caller's
// settle loop can observe and retry, never an exception that unwinds past the
// retry bound. This keeps the bounded settle effective in exactly the case it
// was built for — a frame that is not yet, or never, complete.
function carriesColor(frame, logicalX, logicalY, logicalWidth, logicalHeight, color) {
    if (!frame || frame.width < 1 || frame.height < 1) {
        return false;
    }
    const px = deviceX(frame, logicalX, logicalWidth);
    const py = deviceY(frame, logicalY, logicalHeight);
    if (px < 0 || py < 0 || px >= frame.width || py >= frame.height) {
        return false;
    }
    const probed = frame.pixel(px, py);
    if (probed === undefined || probed === null) {
        return false;
    }
    return Qt.colorEqual(probed, color);
}

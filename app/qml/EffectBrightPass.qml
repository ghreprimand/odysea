import QtQuick

// Thresholded, chroma-preserving bright pass over the content frame — the
// emitter source for both bloom blur chains. Protected wells are excluded
// at the source: where the mask has alpha 1 nothing emits, so thumbnails
// and previews never feed bloom.
ShaderEffect {
    property variant src: null  // ShaderEffectSource of the content root
    property variant mask: null // ShaderEffectSource of the well mask
    property real threshold: 1.0

    blending: false
    fragmentShader: "shaders/brightpass.frag.qsb"
}

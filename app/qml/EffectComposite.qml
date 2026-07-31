import QtQuick

// Final presentation composite: additive core and wide emission over the
// crisp content, then scanlines, vignette, and ordered dither riding on the
// added light. Protected wells pass the original pixels through untouched.
// The parent presentation layer decides when this runs; everything here is
// a plain uniform.
ShaderEffect {
    id: fx

    property variant src: null     // ShaderEffectSource of the content root
    property variant mask: null    // ShaderEffectSource of the well mask
    property variant coreSrc: null // core-blurred bright pass
    property variant wideSrc: null // wide-blurred bright pass

    property real coreI: 0
    property real wideI: 0
    property real scanI: 0
    property real period: 7.0
    property real vigI: 0
    property real dpr: 1
    // Source-over blending only when the ground is translucent; opaque
    // frames keep the cheaper no-blend path.
    property bool translucentGround: false

    readonly property vector2d sizePx: Qt.vector2d(fx.width * fx.dpr, fx.height * fx.dpr)

    blending: translucentGround
    fragmentShader: "shaders/composite.frag.qsb"
}

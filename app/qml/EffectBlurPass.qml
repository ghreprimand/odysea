import QtQuick

// One separable Gaussian blur pass. `srcSize` is the source texture size in
// texels, `axis` is (1,0) or (0,1), and `sigma` is in source texels. Two
// passes at right angles form one 2-D Gaussian. `gain` is the linear
// quantization-headroom encode for 8-bit intermediate textures; 1.0 is the
// identity (see shaders/blur1d.frag).
ShaderEffect {
    id: pass

    property variant src: null
    property size srcSize: Qt.size(1, 1)
    property vector2d axis: Qt.vector2d(1, 0)
    property real sigma: 1.0
    property real gain: 1.0

    // Named to avoid the GLSL builtin `step`: a uniform with that name
    // silently never binds after shader translation and the pass becomes an
    // identity.
    readonly property vector2d stepv: Qt.vector2d(pass.axis.x / Math.max(1, pass.srcSize.width), pass.axis.y / Math.max(1, pass.srcSize.height))

    blending: false
    fragmentShader: "shaders/blur1d.frag.qsb"
}

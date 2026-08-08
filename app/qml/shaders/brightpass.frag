#version 440
// Thresholded bright pass over the shell content frame: the emitter source
// for both bloom blur chains. Chroma-preserving — RGB is scaled by the
// over-threshold excess fraction, so green content glows green and amber
// glows amber instead of collapsing to a gray extract. Protected wells
// (mask alpha 1) emit nothing, which is what keeps thumbnails and previews
// from feeding bloom. Output is opaque black where nothing exceeds the
// threshold, so downstream passes can sample .rgb directly.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float threshold; // bright-pass luma threshold, 0.30 to 1.0
};
layout(binding = 1) uniform sampler2D src;
layout(binding = 2) uniform sampler2D mask;

void main() {
    vec4 c = texture(src, qt_TexCoord0);
    // Binarized like the composite's protection: a half-covered mask texel
    // must suppress emission outright, or a fractional device scale leaks
    // a rim of the well's own brightness into the bloom chain.
    float protectedPx = step(0.5, texture(mask, qt_TexCoord0).a);

    float a = max(c.a, 1e-4);
    vec3 col = c.rgb / a;

    float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
    float excess = max(l - threshold, 0.0);
    float scale = excess / max(l, 1e-4);

    vec3 bright = col * scale * (1.0 - protectedPx);
    fragColor = vec4(bright, 1.0) * qt_Opacity;
}

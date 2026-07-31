#version 440
// One separable 1-D Gaussian pass. `sigma` is in source texels; `stepv` is
// the blur axis divided by the source texture size. Weights are computed
// in-shader and normalized, so the pass preserves energy.
//
// The loop bound is constant, but kernel support always covers 3*sigma: at
// large sigma the taps stride (stride = ceil(3*sigma/64), always much
// smaller than sigma, so the strided sum still tracks the Gaussian). A
// fixed-tap kernel without the stride truncates the wide halo to an exact
// zero past the tap count.
//
// `gain` is a linear quantization-headroom encode for 8-bit intermediate
// textures on graphics stacks without float render targets: the wide chain
// stores values multiplied by gain so the sub-1/255 Gaussian tail survives
// the texture round trip, and the composite divides the product back out.
// Gain is linear, so it commutes with the convolution exactly. Without it
// the wide tail quantizes to exact zero beyond roughly two sigma.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float sigma; // gaussian sigma, source texels
    float gain;  // linear output gain (quantization headroom encode)
    vec2 stepv;  // sample step, normalized texture coordinates
};
layout(binding = 1) uniform sampler2D src;

void main() {
    vec3 acc = texture(src, qt_TexCoord0).rgb;
    float wsum = 1.0;
    float s = max(sigma, 0.1);
    float reach = 3.0 * s;
    float stride = max(1.0, ceil(reach / 64.0));
    for (int i = 1; i <= 64; ++i) {
        float fi = float(i) * stride;
        if (fi > reach)
            break;
        float w = exp(-0.5 * fi * fi / (s * s));
        acc += w * (texture(src, qt_TexCoord0 + stepv * fi).rgb
                  + texture(src, qt_TexCoord0 - stepv * fi).rgb);
        wsum += 2.0 * w;
    }
    fragColor = vec4(acc / wsum * gain, 1.0) * qt_Opacity;
}

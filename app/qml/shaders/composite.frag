#version 440
// Final presentation composite. The order is load-bearing: additive
// emission first (core + wide blurred bright pass), then the multiplicative
// scanline/vignette pass rides on top of the added light so the bands
// modulate the glow instead of the glow filling the bands back in. A
// soft-knee floor keeps lit pixels from ever reaching zero, the vignette
// exempts the center 62% of the half-diagonal, the scanline period is in
// physical pixels, and an 8x8 ordered dither on the dimming factor prevents
// posterization rings. Protected wells (mask alpha 1) pass the original
// pixels through untouched. Alpha-aware so translucent grounds composite
// correctly.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float coreI;  // additive core gain, 0 to 0.80
    float wideI;  // additive wide gain, 0 to 1.00 (pre-decoded)
    float scanI;  // scanline trough dimming, 0 to 0.35
    float period; // scanline period, physical px
    float vigI;   // vignette strength, 0 to 0.45
    float dpr;    // device pixel ratio
    vec2 sizePx;  // item size, device px
};
layout(binding = 1) uniform sampler2D src;
layout(binding = 2) uniform sampler2D mask;
layout(binding = 3) uniform sampler2D coreSrc; // blurred bright pass, tight
layout(binding = 4) uniform sampler2D wideSrc; // blurred bright pass, wide

float bayer8(vec2 p) {
    // 8x8 ordered-dither threshold in [0,1); float-only recursive Bayer
    // construction (no integer bit operations).
    float v = 0.0;
    float mul = 1.0;
    vec2 q = floor(p);
    for (int i = 0; i < 3; ++i) {
        vec2 b = mod(q, 2.0);
        v += mul * mod(2.0 * b.x + 3.0 * b.y, 4.0);
        mul *= 4.0;
        q = floor(q / 2.0);
    }
    return v / 64.0;
}

void main() {
    vec4 c = texture(src, qt_TexCoord0);
    // Binarized protection: linear sampling of the mask edge can land on a
    // half-texel phase at fractional device scales, and a proportional mix
    // would leak a rim of added light onto the outermost protected row.
    // Any pixel at least half covered by the mask is protected outright.
    float protectedPx = step(0.5, texture(mask, qt_TexCoord0).a);

    vec2 px = qt_TexCoord0 * sizePx;

    float a = max(c.a, 1e-4);
    vec3 col = c.rgb / a;

    // 1. Additive emission: energy is only ever added.
    col += coreI * texture(coreSrc, qt_TexCoord0).rgb
         + wideI * texture(wideSrc, qt_TexCoord0).rgb;

    // 2. Scanline bands at a physical-pixel period, modulating the glow.
    float phase = cos(6.28318530718 * px.y / max(period, 1.0));
    float trough = max(-phase, 0.0);

    // 3. Vignette (center 62% of the half-diagonal untouched).
    vec2 half_ = sizePx * 0.5;
    float r = length(px - half_) / length(half_);
    float vigDim = vigI * smoothstep(0.62, 1.0, r);

    // Combined multiplicative dimming with a soft-knee floor.
    float dim = scanI * trough + vigDim;
    float knee = 0.55;
    float f = 1.0 - dim + dim * dim * (1.0 - knee);
    f = clamp(f, knee, 1.0);

    // Ordered dither on the factor to prevent posterization rings.
    f += (bayer8(px / max(dpr, 1.0)) - 0.5) / 255.0;

    col *= f;

    // Protected wells are exempt from every stage.
    vec3 outc = mix(col * a, c.rgb, protectedPx);
    fragColor = vec4(outc, c.a) * qt_Opacity;
}

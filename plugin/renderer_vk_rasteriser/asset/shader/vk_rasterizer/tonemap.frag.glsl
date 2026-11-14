#version 460

// HDR -> display. Reads the linear HDR scene colour, applies exposure + tonemapper and encodes sRGB for the UNORM target.
// With the sky off, pixels the scene did not cover (depth == 1) get the display-referred clear colour, so that background
// is unchanged by exposure / tonemapping. With the sky on, the background is real HDR radiance and is tonemapped like the rest

layout(set = 0, binding = 0) uniform sampler2D hdr_tex;
layout(set = 0, binding = 1) uniform sampler2D depth_tex;

layout(push_constant) uniform PushConstants {
    vec4  clear_color;          // display-referred (already sRGB encoded), written as-is for background pixels
    float exposure;             // linear multiplier (2^EV)
    uint  tonemapper;           // 0 = ACES (fitted), 1 = AgX, 2 = Reinhard (luminance), 3 = none (clamp)
    uint  background_mode;      // 0 = clear colour composite for depth == 1, 1 = HDR sky already in hdr_tex
    uint  _pad1;
} pc;

layout(location = 0) out vec4 out_color;


// ---- tonemappers --------------------------------------------------------------------------------------------------------

// Narkowicz 2015 fit of the ACES RRT+ODT. Same curve the material shaders used to apply inline
vec3 tonemap_aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}


// AgX (Troy Sobotka), polynomial approximation of the default contrast curve (Benjamin Wrensch). Produces values in
// linear sRGB primaries, ready for the OETF below
vec3 agx_contrast(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return  15.5     * x4 * x2
          - 40.14    * x4 * x
          + 31.96    * x4
          - 6.868    * x2 * x
          + 0.4298   * x2
          + 0.1191   * x
          - 0.00232;
}

vec3 tonemap_agx(vec3 color) {

    const mat3 agx_inset = mat3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 agx_outset = mat3(
         1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433,  1.15107367264116);
    const float min_ev = -12.47393;
    const float max_ev = 4.026069;

    color = agx_inset * max(color, vec3(1e-10));
    color = clamp(log2(color), min_ev, max_ev);
    color = (color - min_ev) / (max_ev - min_ev);
    color = agx_contrast(color);                    // display-encoded (≈ sRGB EOTF^-1 baked in)
    color = agx_outset * color;
    return pow(max(color, vec3(0.0)), vec3(2.2));   // back to linear so every path shares the same sRGB encode
}


vec3 tonemap_reinhard(vec3 color) {
    const float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return color / (1.0 + luma);
}


// ---- sRGB OETF ----------------------------------------------------------------------------------------------------------

vec3 linear_to_srgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}


vec3 tonemap(vec3 hdr) {
    hdr = max(hdr * pc.exposure, vec3(0.0));                // guard against negative / NaN-producing inputs
    switch (pc.tonemapper) {
        case 1u:    return tonemap_agx(hdr);
        case 2u:    return tonemap_reinhard(hdr);
        case 3u:    return clamp(hdr, 0.0, 1.0);
        default:    return tonemap_aces(hdr);
    }
}


void main() {

    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    const vec4 hdr = texelFetch(hdr_tex, pixel, 0);

    // Background (no opaque geometry). scene_hdr was cleared to 0; translucents drawn over the background left their
    // radiance (premultiplied by coverage) in rgb and their coverage in alpha. Composite that over the display-referred
    // clear colour. Approximate (display space, not HDR) - goes away once the sky pass renders a real HDR background
    if (pc.background_mode == 0u && texelFetch(depth_tex, pixel, 0).r >= 1.0) {
        const float coverage = clamp(hdr.a, 0.0, 1.0);
        const vec3 over = linear_to_srgb(tonemap(hdr.rgb));
        out_color = vec4(clamp(pc.clear_color.rgb * (1.0 - coverage) + over, 0.0, 1.0), 1.0);
        return;
    }

    out_color = vec4(linear_to_srgb(tonemap(hdr.rgb)), 1.0);
}


#version 460
#extension GL_EXT_scalar_block_layout : require

// 8x8 is fine for a memory-bound pass. 16x16 also works
layout(local_size_x = 8, local_size_y = 8) in;

layout(set = 0, binding = 1) uniform CameraUBO {
    mat4  view_inv;
    mat4  proj_inv;
    mat4  prev_view_proj;
    mat4  view_proj;

    vec4  sun_direction;
    vec4  sun_color;
    vec4  sun_params;       // x = angular_radius, y = shadow_ray_tmax

    uvec4 temporal;         // x = reset, y = max_history, z = write_idx, w = frame_counter
    vec4  temporal_params;  // x = clip_k

    uvec4 visual_uints;     // x = ao_samples, y = indirect_samples_base, z = sun_samples
    vec4  visual_floats;    // x = ao_radius,  y = ao_ray_bias
} cam;

layout(set = 0, binding = 2,  rgba8)  uniform image2D out_image;
layout(set = 0, binding = 8,  rgba32f) uniform image2D accum_0;
layout(set = 0, binding = 9,  rgba32f) uniform image2D accum_1;
layout(set = 0, binding = 10, rgba32f) uniform image2D gbuffer_pos_0;
layout(set = 0, binding = 11, rgba32f) uniform image2D gbuffer_pos_1;
layout(set = 0, binding = 12, rgba16f) uniform image2D gbuffer_nrm_0;
layout(set = 0, binding = 13, rgba16f) uniform image2D gbuffer_nrm_1;
layout(set = 0, binding = 14, rgba16f) uniform image2D current_raw;


vec4 load_gbuffer_pos(const ivec2 p, const uint idx) {
    return (idx == 0u) ? imageLoad(gbuffer_pos_0, p) : imageLoad(gbuffer_pos_1, p);
}

vec4 load_gbuffer_nrm(const ivec2 p, const uint idx) {
    return (idx == 0u) ? imageLoad(gbuffer_nrm_0, p) : imageLoad(gbuffer_nrm_1, p);
}

vec4 load_accum(const ivec2 p, const uint idx) {
    return (idx == 0u) ? imageLoad(accum_0, p) : imageLoad(accum_1, p);
}

void store_accum(const ivec2 p, const vec4 v, const uint idx) {
    if (idx == 0u) imageStore(accum_0, p, v);
    else           imageStore(accum_1, p, v);
}

// Bilinear sample of the accum buffer. Same uv convention as the raygen:
// uv = pixel_index / size, so no half-texel shift.
vec4 sample_accum_bilinear(vec2 uvn, ivec2 sz, uint idx) {
    vec2  texel = uvn * vec2(sz);
    ivec2 base = ivec2(floor(texel));
    vec2  frac = texel - vec2(base);
    ivec2 hi = sz - ivec2(1);
    vec4 a = load_accum(clamp(base + ivec2(0, 0), ivec2(0), hi), idx);
    vec4 b = load_accum(clamp(base + ivec2(1, 0), ivec2(0), hi), idx);
    vec4 c = load_accum(clamp(base + ivec2(0, 1), ivec2(0), hi), idx);
    vec4 d = load_accum(clamp(base + ivec2(1, 1), ivec2(0), hi), idx);
    return mix(mix(a, b, frac.x), mix(c, d, frac.x), frac.y);
}



void main() {

    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size  = imageSize(out_image);
    if (any(greaterThanEqual(pixel, size)))
        return;

    const uint write_idx = cam.temporal.z;
    const uint prev_idx  = 1u - write_idx;

    // ---- current frame's raw colour ---------------------------------------------
    const vec3 current_color = imageLoad(current_raw, pixel).rgb;

    // ---- neighbourhood statistics (5x5 box) --------------------------------------
    // The current frame's raw signal is very noisy -- 4 indirect rays per pixel
    // around a small emissive source means individual pixels are either bright
    // or dark with a huge spread. A 3x3 kernel doesn't give stable statistics:
    // on some frames the local neighbourhood is unrepresentatively dim even
    // though the true mean is bright, which makes the clip reject the (correct)
    // history. A 5x5 kernel smooths that out at the cost of 25 taps instead of 9.
    vec3 m1 = vec3(0.0);
    vec3 m2 = vec3(0.0);
    const ivec2 hi = size - ivec2(1);

    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            const ivec2 p = clamp(pixel + ivec2(x, y), ivec2(0), hi);
            const vec3  c = imageLoad(current_raw, p).rgb;
            m1 += c;
            m2 += c * c;
        }
    }
    m1 /= 25.0;
    m2 /= 25.0;

    // Clamp variance to zero on the negative side: float error can make
    // `m2 - m1*m1` slightly negative for constant neighbourhoods.
    const vec3 sigma = sqrt(max(m2 - m1 * m1, vec3(0.0)));

    // k = 2.0 is permissive enough for very noisy ray-traced GI. With 3x3 and
    // k = 1.25, the clip was firing on individual noisy frames and producing
    // flicker. If you still see flicker after this, raise k further (2.5, 3.0)
    // before touching anything else.
    const float k = max(cam.temporal_params.x, 0.1);   // guard against a UI set to 0
    const vec3  clip_lo = m1 - k * sigma;
    const vec3  clip_hi = m1 + k * sigma;

    // ---- reprojection ------------------------------------------------------------
    const vec4 gpos_curr = load_gbuffer_pos(pixel, write_idx);
    const vec4 gnrm_curr = load_gbuffer_nrm(pixel, write_idx);

    vec3 result_color = current_color;
    float history_len  = 1.0;

    const bool can_try_reproject = (cam.temporal.x == 0u) && (gpos_curr.w > 0.5);
    if (can_try_reproject) {

        const vec4 prev_clip = cam.prev_view_proj * vec4(gpos_curr.xyz, 1.0);
        if (prev_clip.w > 0.0) {

            const vec2 prev_ndc = prev_clip.xy / prev_clip.w;
            const vec2 prev_uv  = prev_ndc * 0.5 + 0.5;
            const ivec2 prev_pixel = ivec2(round(prev_uv * vec2(size)));

            if (all(greaterThanEqual(prev_pixel, ivec2(0))) &&
                all(lessThan(prev_pixel, size))) {

                const vec4 gpos_prev = load_gbuffer_pos(prev_pixel, prev_idx);
                const vec4 gnrm_prev = load_gbuffer_nrm(prev_pixel, prev_idx);

                const vec3  cam_pos = cam.view_inv[3].xyz;
                const float dist = max(length(gpos_curr.xyz - cam_pos), 1e-3);
                const float pos_diff = length(gpos_curr.xyz - gpos_prev.xyz);

                const bool pos_ok = (gpos_prev.w > 0.5) && (pos_diff < max(0.0002, dist * 0.01));

                const vec3 n_curr = gnrm_curr.xyz * 2.0 - 1.0;
                const vec3 n_prev = gnrm_prev.xyz * 2.0 - 1.0;
                const bool nrm_ok = (gnrm_prev.w > 0.5) && (dot(n_curr, n_prev) > 0.9);

                if (pos_ok && nrm_ok) {

                    const vec4 prev_accum = sample_accum_bilinear(prev_uv, size, prev_idx);
                    const float prev_len  = prev_accum.a;

                    if (prev_len > 0.5) {

                        // Pull the reprojected history into the current frame's
                        // local colour distribution. This is what kills emissive
                        // afterglow and moving-object ghosts.
                        //
                        // NOTE: the `rej` heuristic that used to live here has
                        // been removed. It was firing on individual noisy frames
                        // (where the current 3x3 happened to be unrepresentative)
                        // and resetting history, producing exactly the flicker
                        // you're seeing. The clip alone is sufficient.
                        const vec3 clipped_history = clamp(prev_accum.rgb, clip_lo, clip_hi);

                        history_len = min(prev_len + 1.0, float(cam.temporal.y));

                        const float blend = 1.0 / history_len;
                        result_color = mix(clipped_history, current_color, blend);
                    }
                }
            }
        }
    }

    store_accum(pixel, vec4(result_color, history_len), write_idx);
    imageStore(out_image, pixel, vec4(result_color, 1.0));
}

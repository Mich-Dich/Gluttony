
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : require

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 1) uniform CameraUBO {
    mat4            view_inv;
    mat4            proj_inv;
    vec4            sun_direction;
    vec4            sun_color;
    mat4            prev_view_proj;
    uvec4           temporal;           //x=reset, y=max_history, z=write_idx, w=unused
} cam;
layout(set = 0, binding = 2, rgba8)   uniform image2D out_image;

// ping-pong accum + g-buffer
layout(set = 0, binding = 8, rgba32f)  uniform coherent image2D accum_0;
layout(set = 0, binding = 9, rgba32f)  uniform coherent image2D accum_1;
layout(set = 0, binding = 10, rgba32f) uniform coherent image2D gbuffer_pos_0;
layout(set = 0, binding = 11, rgba32f) uniform coherent image2D gbuffer_pos_1;
layout(set = 0, binding = 12, rgba16f) uniform coherent image2D gbuffer_nrm_0;
layout(set = 0, binding = 13, rgba16f) uniform coherent image2D gbuffer_nrm_1;
layout(location = 0) rayPayloadEXT vec4 payload;

// --- helpers so we can index the ping-pong pair without dynamic descriptors ---
vec4 load_gbuffer_nrm(const ivec2 p, const uint idx) {
    return (idx == 0u) ? imageLoad(gbuffer_nrm_0, p) : imageLoad(gbuffer_nrm_1, p);
}


vec4 load_gbuffer_pos(const ivec2 p, const uint idx) {
    return (idx == 0u) ? imageLoad(gbuffer_pos_0, p) : imageLoad(gbuffer_pos_1, p);
}


vec4 load_accum(const ivec2 p, const uint idx) {
    return (idx == 0u) ? imageLoad(accum_0, p) : imageLoad(accum_1, p);
}


void store_accum(const ivec2 p, const vec4 v, const uint idx) {
    if (idx == 0u) imageStore(accum_0, p, v);
    else           imageStore(accum_1, p, v);
}



// Bilinear sample of the accum buffer. Accum stores (rgb, history_len); filtering all four components together keeps
// the history length in sync with the colour it belongs to
vec4 sample_accum_bilinear(vec2 uvn, ivec2 sz, uint idx) {

    // this codebase uses the convention uv = pixel_index / size. Pixel centres are therefore at integer positions in [uvn * sz],
    // NOT at half-integers like the standard (OpenGL/DirectX) UV convention. The usual "- 0.5" texel-centre offset is therefore
    // wrong here and is deliberately omitted
    vec2  texel = uvn * vec2(sz);
    ivec2 base  = ivec2(floor(texel));
    vec2  frac  = texel - vec2(base);
    ivec2 hi    = sz - ivec2(1);

    vec4 a = load_accum(clamp(base + ivec2(0, 0), ivec2(0), hi), idx);
    vec4 b = load_accum(clamp(base + ivec2(1, 0), ivec2(0), hi), idx);
    vec4 c = load_accum(clamp(base + ivec2(0, 1), ivec2(0), hi), idx);
    vec4 d = load_accum(clamp(base + ivec2(1, 1), ivec2(0), hi), idx);

    return mix(mix(a, b, frac.x), mix(c, d, frac.x), frac.y);
}


void main() {

    const vec2 uv = vec2(gl_LaunchIDEXT.xy) / vec2(gl_LaunchSizeEXT.xy);
    const vec2 ndc = uv * 2.0 - 1.0;
    const vec4 origin = cam.view_inv * vec4(0, 0, 0, 1);
    const vec4 target = cam.proj_inv * vec4(ndc.x, ndc.y, 1, 1);
    const vec4 direction = cam.view_inv * vec4(normalize(target.xyz / target.w), 0);

    payload = vec4(0.0, 0.0, 0.0, 0.0);
    traceRayEXT(topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, 0, origin.xyz, 0.001, direction.xyz, 1000.0, 0);

    // rchit wrote the world position of the primary hit into the "write" g-buffer during the traceRay above
    // Since the image is [coherent], we can read it
    memoryBarrierImage();

    const ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
    const ivec2 size  = ivec2(gl_LaunchSizeEXT.xy);

    const uint write_idx = cam.temporal.z;
    const uint prev_idx  = 1u - write_idx;

    const vec3 current_color = payload.xyz;
    const vec4 gpos_curr     = load_gbuffer_pos(pixel, write_idx);

    vec3  result_color = current_color;
    float history_len  = 1.0;

    // -------- try to reproject into history -------------------------------------------
    // conditions:
    //   - not the first frame after a reset
    //   - the primary ray actually hit something (gpos_curr.w > 0.5)
    const bool can_try_reproject = (cam.temporal.x == 0u) && (gpos_curr.w > 0.5);

    if (can_try_reproject) {

        const vec4 prev_clip = cam.prev_view_proj * vec4(gpos_curr.xyz, 1.0);

        if (prev_clip.w > 0.0) {

            const vec2  prev_ndc = prev_clip.xy / prev_clip.w;
            const vec2  prev_uv  = prev_ndc * 0.5 + 0.5;

            // ---- round, don't truncate -----------------------------------------------
            // uv is defined so that [uv * size == pixel_index] exactly. Truncating introduces up to 1 pixel of re-projection error,
            // which is what made the disocclusion test flap. Round to the nearest pixel
            const ivec2 prev_pixel = ivec2(round(prev_uv * vec2(size)));

            if (all(greaterThanEqual(prev_pixel, ivec2(0))) &&
                all(lessThan(prev_pixel, size))) {

                const vec4 gpos_prev = load_gbuffer_pos(prev_pixel, prev_idx);
                const vec4 gnrm_prev = load_gbuffer_nrm(prev_pixel, prev_idx);
                const vec4 gnrm_curr = load_gbuffer_nrm(pixel, write_idx);

                // ---- relative depth test ---------------------------------------------
                // An absolute world-space threshold is meaningless across scene scales and camera distances. Scale the tolerance with
                // the distance from the camera instead
                const vec3  cam_pos = cam.view_inv[3].xyz;
                const float dist = max(length(gpos_curr.xyz - cam_pos), 1e-3);
                const float pos_diff = length(gpos_curr.xyz - gpos_prev.xyz);

                const bool pos_ok = (gpos_prev.w > 0.5) && (pos_diff < max(0.0002, dist * 0.01));

                // ---- normal test -----------------------------------------------------
                // Catches silhouette edges where the world position is nearly the same but the two surfaces face different directions
                vec3 n_curr = gnrm_curr.xyz * 2.0 - 1.0;
                vec3 n_prev = gnrm_prev.xyz * 2.0 - 1.0;
                const bool nrm_ok = (gnrm_prev.w > 0.5) && (dot(n_curr, n_prev) > 0.9);

                if (pos_ok && nrm_ok) {

                    const vec4 prev_accum = sample_accum_bilinear(prev_uv, size, prev_idx);
                    const float prev_len = prev_accum.a;

                    if (prev_len > 0.5) {
                        history_len = min(prev_len + 1.0, float(cam.temporal.y));

                        const float blend = 1.0 / history_len;
                        result_color = mix(prev_accum.rgb, current_color, blend);
                    }
                }
            }
        }
    }

    // write the accumulated value, tagged with the new history length
    store_accum(pixel, vec4(result_color, history_len), write_idx);
    imageStore(out_image, pixel, vec4(result_color, 1.0));
}

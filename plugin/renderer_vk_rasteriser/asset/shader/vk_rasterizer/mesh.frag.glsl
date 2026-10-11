
#version 450

// Camera UBO — must declare every field up to cluster_depth so offsets match camera_ubo_data
// std140 layout lines up exactly with the C++ struct (see static_assert in types.h)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4            view_proj;
    mat4            view;
    vec4            camera_pos;
    vec4            frustum_planes[6];
    uvec4           flags;
    mat4            projection;
    mat4            inv_projection;
    uvec4           cluster_dims;   // x = grid_x, y = grid_y, z = grid_z
    vec4            cluster_depth;  // x = near, y = far,  z = render_w, w = render_h
    mat4            light_view_proj;
    vec4            shadow_params;  // x = world units/texel, y = normal bias (texels), z = depth bias, w = 1/resolution
} camera;

struct Light {
    vec4            position_radius;      // xyz = world pos, w = radius
    vec4            color_intensity;      // rgb = color, a = intensity
    vec4            direction_inner_cos;  // xyz = world dir, w = cos(inner cone angle)
    vec4            params;               // x = cos(outer cone), y = type (0=dir, 1=point, 2=spot)
};

struct ShadowView {
    mat4 view_proj;
    vec4 atlas_rect;    // xy = uv offset, zw = uv scale (zw == 0: no tile)
    vec4 params;        // x = texel world size per unit distance, y = normal bias (texels), z = light-ward bias (texels), w = 1/atlas size
    vec4 sphere;
    vec4 planes[6];
    vec4 _pad[3];       // keeps the std430 stride at 256 bytes, same as SHADOW_VIEW_STRIDE
};

layout(std430, set = 0, binding = 2) readonly buffer LightBuffer { Light lights[]; } light_buffer;
layout(std430, set = 0, binding = 3) readonly buffer LightGrid { uvec2 grid[]; } light_grid;
layout(std430, set = 0, binding = 4) readonly buffer LightIndexList { uint indices[]; } light_index_list;
layout(set = 0, binding = 5) uniform sampler2DShadow shadow_map;
layout(std430, set = 0, binding = 6) readonly buffer ShadowViews { ShadowView views[]; } shadow_views;
layout(set = 0, binding = 7) uniform sampler2DShadow shadow_atlas;

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

// Match the light_culling shader: positive view-space Z from window-space depth
float linearize_depth(float d) {
    vec4 clip = vec4(0.0, 0.0, d, 1.0);
    vec4 view = camera.inv_projection * clip;
    return -view.z / view.w;
}


// 1 = lit, 0 = shadowed
float sample_shadow(vec3 world_pos, vec3 N, float ndotl) {

    // Normal-offset: push the lookup position along the normal, more at grazing angles. This removes most acne without peter-panning
    const float normal_offset = camera.shadow_params.x * camera.shadow_params.y * (1.0 - ndotl);
    const vec4 light_clip = camera.light_view_proj * vec4(world_pos + N * normal_offset, 1.0);
    const vec3 ndc = light_clip.xyz / light_clip.w;     // w == 1 for ortho

    const vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0)
        return 1.0;

    const float ref_depth = ndc.z - camera.shadow_params.z;
    const float texel = camera.shadow_params.w;

    // 3x3 taps, each a hardware-filtered 2x2 compare. textureLod because we're in non-uniform control flow
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += textureLod(shadow_map, vec3(uv + vec2(x, y) * texel, ref_depth), 0.0);
    return sum / 9.0;
}


// Cube face from the light->fragment direction. Order must match SHADOW_FACE_DIRECTIONS on the CPU: +X -X +Y -Y +Z -Z
uint point_face(vec3 d) {
    const vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z)   return d.x > 0.0 ? 0u : 1u;
    if (a.y >= a.z)                 return d.y > 0.0 ? 2u : 3u;
    return d.z > 0.0 ? 4u : 5u;
}

// 1 = lit, 0 = shadowed. Bias is expressed in shadow texels at the fragment's distance, so it holds across tile sizes
float sample_local_shadow(uint view_index, vec3 world_pos, vec3 N, vec3 L, float ndotl, float dist) {

    const ShadowView v = shadow_views.views[view_index];
    if (v.atlas_rect.z <= 0.0)
        return 1.0;                                     // culled face / didn't fit in the atlas

    const float texel_world = dist * v.params.x;
    const vec3 biased = world_pos
        + N * (texel_world * v.params.y * (1.0 - ndotl))
        + L * (texel_world * v.params.z);

    const vec4 clip = v.view_proj * vec4(biased, 1.0);
    if (clip.w <= 0.0)
        return 1.0;

    const vec3 ndc = clip.xyz / clip.w;
    const vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0)
        return 1.0;

    const vec2 atlas_uv = v.atlas_rect.xy + uv * v.atlas_rect.zw;
    const float t = v.params.w;                         // one atlas texel in uv
    const vec2 lo = v.atlas_rect.xy + 0.5 * t;          // taps never leave this tile
    const vec2 hi = v.atlas_rect.xy + v.atlas_rect.zw - 0.5 * t;

    // 4 taps at +-0.5 texel, each a hardware bilinear compare -> an effective 3x3 PCF for 4 fetches
    float s = 0.0;
    s += textureLod(shadow_atlas, vec3(clamp(atlas_uv + vec2(-0.5, -0.5) * t, lo, hi), ndc.z), 0.0);
    s += textureLod(shadow_atlas, vec3(clamp(atlas_uv + vec2( 0.5, -0.5) * t, lo, hi), ndc.z), 0.0);
    s += textureLod(shadow_atlas, vec3(clamp(atlas_uv + vec2(-0.5,  0.5) * t, lo, hi), ndc.z), 0.0);
    s += textureLod(shadow_atlas, vec3(clamp(atlas_uv + vec2( 0.5,  0.5) * t, lo, hi), ndc.z), 0.0);
    return s * 0.25;
}



void main() {

    const vec3 N = normalize(in_normal);
    const vec3 albedo = vec3(0.75, 0.72, 0.68);

    // ---- Cluster lookup ----------------------------------------------------------------------------
    // The depth prepass wrote gl_FragCoord.z; reuse it here to avoid a second depth fetch
    const uvec3 grid_dims = camera.cluster_dims.xyz;
    const uvec2 tile = uvec2(gl_FragCoord.xy / vec2(16.0, 16.0));   // CLUSTER_TILE_SIZE_X/Y

    const float near_z = camera.cluster_depth.x;
    const float far_z = camera.cluster_depth.y;

    const float lz = clamp(linearize_depth(gl_FragCoord.z), near_z, far_z);

    // Inverse of the log-slice formula used in light_culling.comp.glsl
    const float ratio = log(far_z / near_z);
    const float slice_f = float(grid_dims.z) * log(lz / near_z) / ratio;
    const uint slice = uint(clamp(slice_f, 0.0, float(grid_dims.z - 1u)));

    const uint cluster_id = tile.x + tile.y * grid_dims.x + slice * grid_dims.x * grid_dims.y;

    const uvec2 entry = light_grid.grid[cluster_id];
    const uint offset = entry.x;
    const uint count = entry.y;

    // ---- Forward+ accumulation ----------------------------------------------------------------------
    vec3 color = albedo * 0.15;   // cheap ambient so fully-shadowed pixels aren't pure black

    for (uint i = 0u; i < count; ++i) {

        const uint li = light_index_list.indices[offset + i];
        const Light light = light_buffer.lights[li];
        const uint type = uint(light.params.y);

        vec3 L;
        float attenuation = 1.0;
        float light_dist = 0.0;

        if (type == 0u) {
            // Directional
            L = normalize(-light.direction_inner_cos.xyz);

        } else {
            const vec3  to_light = light.position_radius.xyz - in_world_pos;
            const float dist = length(to_light);
            light_dist = dist;
            const float radius = light.position_radius.w;
            L = to_light / max(dist, 1e-4);

            // Radius-parameterized window: 1 at the light, 0 at the radius.
            // No 1/dist² term — intensity is the peak value, radius is the reach.
            const float x = clamp(dist / radius, 0.0, 1.0);
            const float window = 1.0 - x * x;
            attenuation = window * window;

            if (type == 2u) {
                const vec3 spot_dir = normalize(light.direction_inner_cos.xyz);
                const float cd = dot(-L, spot_dir);
                const float cos_outer = light.params.x;
                const float cos_inner = light.direction_inner_cos.w;
                const float spot = clamp((cd - cos_outer) / max(cos_inner - cos_outer, 1e-4), 0.0, 1.0);
                attenuation *= spot * spot;
            }
        }

        const float ndotl = max(dot(N, L), 0.0);

        if (type == 0u) {

            if (light.params.z > 0.5 && ndotl > 0.0)
                attenuation *= sample_shadow(in_world_pos, N, ndotl);

        } else if (light.params.z > 0.5 && attenuation > 0.002 && ndotl > 0.0) {

            // params.z = first shadow view index + 1. Point lights add the cube face on top
            uint view_index = uint(light.params.z) - 1u;
            if (type == 1u)
                view_index += point_face(in_world_pos - light.position_radius.xyz);
            attenuation *= sample_local_shadow(view_index, in_world_pos, N, L, ndotl, light_dist);
        }

        color += albedo * light.color_intensity.rgb * light.color_intensity.a * ndotl * attenuation;
    }

    out_color = vec4(color, 1.0);
}

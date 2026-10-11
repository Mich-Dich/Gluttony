
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
} camera;

struct Light {
    vec4            position_radius;      // xyz = world pos, w = radius
    vec4            color_intensity;      // rgb = color, a = intensity
    vec4            direction_inner_cos;  // xyz = world dir, w = cos(inner cone angle)
    vec4            params;               // x = cos(outer cone), y = type (0=dir, 1=point, 2=spot)
};

layout(std430, set = 0, binding = 2) readonly buffer LightBuffer { Light lights[]; } light_buffer;
layout(std430, set = 0, binding = 3) readonly buffer LightGrid { uvec2 grid[]; } light_grid;
layout(std430, set = 0, binding = 4) readonly buffer LightIndexList { uint indices[]; } light_index_list;

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

        if (type == 0u) {
            // Directional
            L = normalize(-light.direction_inner_cos.xyz);

        } else {
            const vec3  to_light = light.position_radius.xyz - in_world_pos;
            const float dist = length(to_light);
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
        color += albedo * light.color_intensity.rgb * light.color_intensity.a * ndotl * attenuation;
    }

    out_color = vec4(color, 1.0);
}

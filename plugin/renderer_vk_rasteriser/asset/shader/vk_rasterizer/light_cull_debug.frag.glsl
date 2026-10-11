
#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4  view_proj;
    mat4  view;
    vec4  camera_pos;
    vec4  frustum_planes[6];
    uvec4 flags;
    mat4  projection;
    mat4  inv_projection;
    uvec4 cluster_dims;
    vec4  cluster_depth;  // x = near, y = far, z = render_w, w = render_h
} camera;

layout(std430, set = 0, binding = 1) readonly buffer LightGrid { uvec2 grid[]; } light_grid;
layout(set = 0, binding = 2) uniform sampler2D depth_texture;

// Scaling for the heatmap. Should be close to the per-cluster saturation point
// you expect in the scene. Lights-per-cluster above this clamp to red.
const float HEATMAP_MAX_LIGHTS = 32.0;

layout(location = 0) out vec4 out_color;


float linearize_depth(float d) {
    vec4 clip = vec4(0.0, 0.0, d, 1.0);
    vec4 view = camera.inv_projection * clip;
    return -view.z / view.w;
}


// Simple blue → cyan → green → yellow → red ramp
vec3 heatmap(float t) {

    t = clamp(t, 0.0, 1.0);

    vec3 cold = vec3(0.05, 0.05, 0.35);   // deep blue
    vec3 mid = vec3(0.10, 0.80, 0.30);   // green
    vec3 hot = vec3(1.00, 0.20, 0.05);   // red

    if (t < 0.5) return mix(cold, mid, smoothstep(0.0, 0.5, t));
    return mix(mid, hot, smoothstep(0.5, 1.0, t));
}


void main() {

    const uvec3 grid_dims = camera.cluster_dims.xyz;
    const uvec2 tile = uvec2(gl_FragCoord.xy / vec2(16.0, 16.0));

    const float near_z = camera.cluster_depth.x;
    const float far_z = camera.cluster_depth.y;

    // Exact same path as mesh.frag.glsl, so a mismatch here means our shaders disagree
    const float d_raw = texelFetch(depth_texture, ivec2(gl_FragCoord.xy), 0).r;
    const float lz = clamp(linearize_depth(d_raw), near_z, far_z);
    const float ratio = log(far_z / near_z);
    const float slice_f = float(grid_dims.z) * log(lz / near_z) / ratio;
    const uint  slice = uint(clamp(slice_f, 0.0, float(grid_dims.z - 1u)));

    const uint cluster_id = tile.x + tile.y * grid_dims.x + slice * grid_dims.x * grid_dims.y;
    const uvec2 entry = light_grid.grid[cluster_id];

    // Background = depth cleared to far plane, lights-only pixels = black
    if (d_raw >= 1.0) {
        out_color = vec4(0.02, 0.02, 0.05, 1.0);
        return;
    }

    const float t = float(entry.y) / HEATMAP_MAX_LIGHTS;
    out_color = vec4(heatmap(t), 1.0);
}

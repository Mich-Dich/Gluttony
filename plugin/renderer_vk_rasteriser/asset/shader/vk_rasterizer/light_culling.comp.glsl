
#version 450

layout(local_size_x = 64) in;

// Bindings ------------------------------------------------------------------------------------------------------------
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
    vec4 frustum_planes[6];
    uvec4 flags;
    mat4 projection;
    mat4 inv_projection;
    uvec4 cluster_dims;   // x = grid_x, y = grid_y, z = grid_z
    vec4  cluster_depth;  // x = near, y = far, z = render_w, w = render_h
} camera;

struct Light {
    vec4 position_radius;      // xyz = world pos, w = radius
    vec4 color_intensity;      // rgb = color,    a = intensity
    vec4 direction_inner_cos;  // xyz = world dir, w = cos(inner cone angle)
    vec4 params;               // x = cos(outer cone), y = type (0=dir, 1=point, 2=spot)
};

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer { Light lights[]; } light_buffer;
layout(std430, set = 0, binding = 2) writeonly buffer LightGrid { uvec2 grid[]; } light_grid;   // { offset_into_index_list, count }
layout(std430, set = 0, binding = 3) writeonly buffer LightIndexList { uint indices[]; } light_index_list;
layout(std430, set = 0, binding = 4) buffer LightCounter { uint next_free; } light_counter; // global atomic cursor into light_index_list
layout(set = 0, binding = 5) uniform LightMetaUBO { uvec4 meta; } light_meta; // x = light_count, y = index_list_capacity, z = max_per_cluster
layout(set = 0, binding = 6) uniform sampler2D depth_texture;

// Helpers -------------------------------------------------------------------------------------------------------------

// Convert a window-space depth value in [0,1] to a positive view-space Z.
// Works for any projection whose inverse is available, regardless of Z-range convention.
float linearize_depth(float d, mat4 inv_proj) {
    vec4 clip = vec4(0.0, 0.0, d, 1.0);
    vec4 view = inv_proj * clip;
    return -view.z / view.w;    // view-space Z is negative in front
}


void cluster_aabb_view(uvec3 cluster, out vec3 aabb_min, out vec3 aabb_max) {

    const uint  slices = camera.cluster_dims.z;
    const float near_z = camera.cluster_depth.x;
    const float far_z  = camera.cluster_depth.y;

    // ---- 1. Tighten the slice range from the actual depth in this tile ----
    // Sample a 4x4 grid across the 16x16 footprint. The subsample is conservative: if any
    // pixel that was sampled is at background depth, its linear value clamps toward far_z,
    // which only makes the range wider — never narrower. Worst case is the un-tightened
    // slice range, which is what the shader would have used anyway.
    const ivec2 tile_origin = ivec2(cluster.xy) * ivec2(16, 16);
    const int   sub_step    = 4;    // 4 samples per axis
    float min_linear = 1e30;
    float max_linear = -1e30;

    for (int y = 0; y < 16; y += sub_step) {
        for (int x = 0; x < 16; x += sub_step) {
            const ivec2 pixel = tile_origin + ivec2(x, y);
            const float d     = texelFetch(depth_texture, pixel, 0).r;
            const float lz    = linearize_depth(d, camera.inv_projection);
            min_linear = min(min_linear, lz);
            max_linear = max(max_linear, lz);
        }
    }

    const float log_ratio = log(far_z / near_z);
    const float slice_near_nominal = near_z * exp(log_ratio * (float(cluster.z)     / float(slices)));
    const float slice_far_nominal  = near_z * exp(log_ratio * (float(cluster.z + 1) / float(slices)));

    const float slice_near = max(slice_near_nominal, min_linear);
    const float slice_far  = min(slice_far_nominal,  max_linear);

    // If the entire slice is empty (min > max), collapse to an empty range. Light tests
    // against an empty AABB reject everything, which is the correct behavior
    if (slice_near > slice_far) {
        aabb_min = vec3(0.0);
        aabb_max = vec3(-1.0);      // inverted, no sphere can intersect
        return;
    }

    // ---- 2. Build the view-space AABB for the tightened slice range ----
    const vec2 render_size = camera.cluster_depth.zw;
    const vec2 tile_px_min = vec2(tile_origin);
    const vec2 tile_px_max = tile_px_min + vec2(16.0, 16.0);

    const vec2 ndc_min = (tile_px_min / render_size) * 2.0 - 1.0;
    const vec2 ndc_max = (tile_px_max / render_size) * 2.0 - 1.0;

    vec4 n00 = camera.inv_projection * vec4(ndc_min.x, ndc_min.y, -1.0, 1.0); n00 /= n00.w;
    vec4 n10 = camera.inv_projection * vec4(ndc_max.x, ndc_min.y, -1.0, 1.0); n10 /= n10.w;
    vec4 n01 = camera.inv_projection * vec4(ndc_min.x, ndc_max.y, -1.0, 1.0); n01 /= n01.w;
    vec4 n11 = camera.inv_projection * vec4(ndc_max.x, ndc_max.y, -1.0, 1.0); n11 /= n11.w;

    const float sn = slice_near / near_z;
    const float sf = slice_far  / near_z;

    vec3 c0n = n00.xyz * sn; vec3 c1n = n10.xyz * sn;
    vec3 c2n = n01.xyz * sn; vec3 c3n = n11.xyz * sn;
    vec3 c0f = n00.xyz * sf; vec3 c1f = n10.xyz * sf;
    vec3 c2f = n01.xyz * sf; vec3 c3f = n11.xyz * sf;

    aabb_min = min(min(min(c0n, c1n), min(c2n, c3n)), min(min(c0f, c1f), min(c2f, c3f)));
    aabb_max = max(max(max(c0n, c1n), max(c2n, c3n)), max(max(c0f, c1f), max(c2f, c3f)));
}


bool sphere_intersects_aabb(vec3 center, float radius, vec3 aabb_min, vec3 aabb_max) {

    vec3 closest = clamp(center, aabb_min, aabb_max);
    vec3 d = center - closest;
    return dot(d, d) <= radius * radius;
}


// Conservative: treats the cone as an infinite double cone. `pz + sr > 0` rejects spheres that
// are entirely behind the apex, and the rest of the test rejects anything off the lateral surface
bool sphere_intersects_cone(vec3 apex, vec3 axis, float cos_outer, vec3 sphere_center, float sphere_radius) {

    vec3 V = sphere_center - apex;
    float pz = dot(V, axis);
    float pr = length(V - pz * axis);

    float sin_t = sqrt(max(1.0 - cos_outer * cos_outer, 0.0));
    float dist = abs(pz * sin_t - pr * cos_outer);

    return dist < sphere_radius && pz + sphere_radius > 0.0;
}


// Entry point: one workgroup per cluster ------------------------------------------------------------------------------

const uint LOCAL_SIZE = 64u;
const uint MAX_PER_CLUSTER_LDS = 256u;   // matches MAX_LIGHTS_PER_CLUSTER on the C++ side

shared uint s_light_count;
shared uint s_light_indices[MAX_PER_CLUSTER_LDS];
shared vec3 s_aabb_min;
shared vec3 s_aabb_max;



void main() {

    const uint  local_id = gl_LocalInvocationIndex;
    const uvec3 cluster = gl_WorkGroupID;

    // Decode the linear cluster id from the 3D dispatch
    const uint cluster_id = cluster.x + cluster.y * camera.cluster_dims.x + cluster.z * camera.cluster_dims.x * camera.cluster_dims.y;

    if (local_id == 0u)
        cluster_aabb_view(cluster, s_aabb_min, s_aabb_max);
    barrier();

    const uint max_per_cluster = min(min(light_meta.meta.z, MAX_PER_CLUSTER_LDS), 256u);

    if (local_id == 0u)
        s_light_count = 0u;
    barrier();

    const uint light_count = light_meta.meta.x;

    for (uint i = local_id; i < light_count; i += LOCAL_SIZE) {

        Light light = light_buffer.lights[i];
        const uint type = uint(light.params.y);

        // Light in view space. Radius is unchanged by the rigid transform
        const vec3  light_pos_vs = (camera.view * vec4(light.position_radius.xyz, 1.0)).xyz;
        const float radius = light.position_radius.w;

        bool visible = false;

        if (type == 1u) {

            // Point light
            visible = sphere_intersects_aabb(light_pos_vs, radius, s_aabb_min, s_aabb_max);

        } else if (type == 2u) {

            // Spot: sphere-AABB first as a cheap reject, then the cone
            if (sphere_intersects_aabb(light_pos_vs, radius, s_aabb_min, s_aabb_max)) {

                const vec3  axis_vs = normalize((camera.view * vec4(light.direction_inner_cos.xyz, 0.0)).xyz);
                const float cos_outer = light.params.x;

                // Conservative: use the AABB's bounding sphere
                const vec3  center = 0.5 * (s_aabb_min + s_aabb_max);
                const float aabb_radius = 0.5 * length(s_aabb_max - s_aabb_min);

                visible = sphere_intersects_cone(light_pos_vs, axis_vs, cos_outer, center, aabb_radius + radius);
            }

        } else {

            // Directional light: always affects every cluster
            visible = true;
        }

        if (visible) {
            const uint slot = atomicAdd(s_light_count, 1u);
            if (slot < max_per_cluster)
                s_light_indices[slot] = i;
        }
    }

    barrier();

    // One thread reserves space and flushes the LDS list to the global buffer
    if (local_id == 0u) {

        uint count = min(s_light_count, max_per_cluster);

        if (count == 0u) {
            light_grid.grid[cluster_id] = uvec2(0u, 0u);
            return;
        }

        const uint capacity = light_meta.meta.y;
        const uint base = atomicAdd(light_counter.next_free, count);

        // Overflow: drop this cluster's lights rather than stomp on another cluster's range
        if (base >= capacity) {
            light_grid.grid[cluster_id] = uvec2(0u, 0u);
            return;
        }
        if (base + count > capacity)
            count = capacity - base;

        light_grid.grid[cluster_id] = uvec2(base, count);

        for (uint k = 0u; k < count; ++k)
            light_index_list.indices[base + k] = s_light_indices[k];
    }
}

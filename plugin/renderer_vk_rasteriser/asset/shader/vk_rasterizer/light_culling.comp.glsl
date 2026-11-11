#version 450

// Clustered light assignment (Forward+). ONE WORKGROUP PER SCREEN TILE (all depth slices of that tile).
//
// The first version ran one 64-thread workgroup per CLUSTER (tile x slice): every slice of a tile re-read and
// re-linearised the same 16x16 depth texels, a single thread built the cluster AABB while 63 idled, and every light was
// tested by a separate workgroup per slice. ~200k workgroups at 1080p, ~24 ms of GPU time with 2 lights.
//
// The second version ran one workgroup per tile with one thread per depth texel (256): 0.61 ms on an Intel Arc iGPU at
// 1260x693 (trace 2026-11-11 15:03), more than the depth pre-pass. The work per tile is small; the cost is that every
// tile starts 256 threads, lines them up at up to six barriers and sends two shared-memory atomics per texel.
//
// Now, per tile, with GROUP_SIZE_X * GROUP_SIZE_Y (16) threads:
//   1. every thread reads its block of the tile's depth texels (4x4) and reduces it to min/max in registers (sky /
//      cleared depth == 1 is skipped: no geometry there); two atomics per THREAD merge the blocks. The range is extended
//      by the translucents' view depth span (clamped to the opaque surface)
//   2. the threads share out the slices: each slice gets its view-space AABB, clamped to the tile's real depth range;
//      slices with no geometry are inactive and get no lights (fragments never land in them)
//   3. lights are transformed to view space once per batch of 16 (shared memory), then every (active slice, light) pair
//      is tested in parallel; hits set a bit in a per-slice light bitmask (MAX_LIGHTS bits)
//   4. per slice: popcount -> one atomicAdd reserves the index range, threads write the indices (sorted by light index)
//
// 16 threads are few enough to fit into one SIMD unit of current GPUs (Intel 16 or 32 lanes, NVIDIA 32, AMD 64). A
// driver can then run a tile's threads in lock-step, and barrier() needs no real synchronisation.
//
// The result is the same as with one thread per texel: same texels, same arithmetic per texel, same (slice, light)
// tests. Checked against the second version on random scenes (identical light lists for every cluster); only the
// offsets into light_index_list differ from run to run, as before (atomic counter).
//
// Output format is unchanged: light_grid[cluster] = { offset, count } into light_index_list.

#define GROUP_SIZE_X 4
#define GROUP_SIZE_Y 4

layout(local_size_x = GROUP_SIZE_X, local_size_y = GROUP_SIZE_Y, local_size_z = 1) in;

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
layout(set = 0, binding = 5) uniform LightMetaUBO { uvec4 meta; } light_meta; // x = light_count, y = index_list_capacity, z = max_per_cluster, w = translucent view depth span (half2, 0 = none)
layout(set = 0, binding = 6) uniform sampler2D depth_texture;

// Constants / shared --------------------------------------------------------------------------------------------------

const uint  THREADS = uint(GROUP_SIZE_X * GROUP_SIZE_Y);
const uvec2 GROUP_SIZE = uvec2(GROUP_SIZE_X, GROUP_SIZE_Y);
const uvec2 TILE_SIZE = uvec2(CLUSTER_TILE_SIZE_X, CLUSTER_TILE_SIZE_Y);
const uvec2 BLOCK_SIZE = (TILE_SIZE + GROUP_SIZE - 1u) / GROUP_SIZE;        // depth texels per thread (rounded up)
const uint  SLICES = CLUSTER_DEPTH_SLICES;
const uint MASK_WORDS = (MAX_LIGHTS + 31u) / 32u;

shared uint  s_min_bits;
shared uint  s_max_bits;
shared uint  s_has_background;                    // any texel of the tile at cleared depth (sky)

shared vec3  s_aabb_min[SLICES];
shared vec3  s_aabb_max[SLICES];
shared uint  s_slice_active[SLICES];
shared uint  s_active_any;

shared uint  s_mask[SLICES][MASK_WORDS];
shared uint  s_base[SLICES];
shared uint  s_count[SLICES];

// one batch of lights in view space
shared vec4  s_light_pos_radius[THREADS];         // xyz view pos, w radius
shared vec4  s_light_axis_cos[THREADS];           // xyz view axis, w cos(outer)
shared uint  s_light_type[THREADS];

// Helpers -------------------------------------------------------------------------------------------------------------

float linearize_depth(float d) {
    vec4 v = camera.inv_projection * vec4(0.0, 0.0, d, 1.0);
    return -v.z / v.w;      // view-space Z is negative in front
}


bool sphere_intersects_aabb(vec3 center, float radius, vec3 aabb_min, vec3 aabb_max) {
    vec3 closest = clamp(center, aabb_min, aabb_max);
    vec3 d = center - closest;
    return dot(d, d) <= radius * radius;
}


// Conservative: infinite double cone; `pz + sr > 0` rejects spheres entirely behind the apex
bool sphere_intersects_cone(vec3 apex, vec3 axis, float cos_outer, vec3 sphere_center, float sphere_radius) {
    vec3 V = sphere_center - apex;
    float pz = dot(V, axis);
    float pr = length(V - pz * axis);
    float sin_t = sqrt(max(1.0 - cos_outer * cos_outer, 0.0));
    float dist = abs(pz * sin_t - pr * cos_outer);
    return dist < sphere_radius && pz + sphere_radius > 0.0;
}


// View-space AABB of one slice of this tile, tightened to [min_linear, max_linear]. Returns false if the slice holds
// no geometry
bool slice_aabb(uvec2 tile, uint slice, uint slices, float min_linear, float max_linear, out vec3 aabb_min, out vec3 aabb_max) {

    const float near_z = camera.cluster_depth.x;
    const float far_z = camera.cluster_depth.y;
    const float log_ratio = log(far_z / near_z);
    const float slice_near = max(near_z * exp(log_ratio * (float(slice) / float(slices))), min_linear * 0.999);
    const float slice_far = min(near_z * exp(log_ratio * (float(slice + 1u) / float(slices))), max_linear * 1.001);

    aabb_min = vec3(0.0);
    aabb_max = vec3(-1.0);
    if (slice_near > slice_far)
        return false;

    const vec2 render_size = camera.cluster_depth.zw;
    const vec2 tile_px_min = vec2(tile * uvec2(CLUSTER_TILE_SIZE_X, CLUSTER_TILE_SIZE_Y));
    const vec2 tile_px_max = tile_px_min + vec2(CLUSTER_TILE_SIZE_X, CLUSTER_TILE_SIZE_Y);
    const vec2 ndc_min = (tile_px_min / render_size) * 2.0 - 1.0;
    const vec2 ndc_max = (tile_px_max / render_size) * 2.0 - 1.0;

    vec4 p00 = camera.inv_projection * vec4(ndc_min.x, ndc_min.y, 0.0, 1.0);
    vec4 p10 = camera.inv_projection * vec4(ndc_max.x, ndc_min.y, 0.0, 1.0);
    vec4 p01 = camera.inv_projection * vec4(ndc_min.x, ndc_max.y, 0.0, 1.0);
    vec4 p11 = camera.inv_projection * vec4(ndc_max.x, ndc_max.y, 0.0, 1.0);

    // Rays through the tile corners, normalised to view-space z == -1; corner at depth d = ray * d
    const vec3 r00 = p00.xyz / -p00.z;
    const vec3 r10 = p10.xyz / -p10.z;
    const vec3 r01 = p01.xyz / -p01.z;
    const vec3 r11 = p11.xyz / -p11.z;

    const vec3 rmin = min(min(r00, r10), min(r01, r11));
    const vec3 rmax = max(max(r00, r10), max(r01, r11));
    aabb_min = min(rmin * slice_near, rmin * slice_far);
    aabb_max = max(rmax * slice_near, rmax * slice_far);
    return true;
}


void main() {

    const uint  tid = gl_LocalInvocationIndex;
    const uvec2 tile = gl_WorkGroupID.xy;
    const uint  slices = min(camera.cluster_dims.z, SLICES);
    const uint  tiles_per_slice = camera.cluster_dims.x * camera.cluster_dims.y;
    const uint  tile_index = tile.x + tile.y * camera.cluster_dims.x;
    const uint  light_count = min(light_meta.meta.x, MAX_LIGHTS);
    const uint  max_per_cluster = min(light_meta.meta.z, MAX_LIGHTS_PER_CLUSTER);
    const uint  words = (light_count + 31u) / 32u;

    if (tid == 0u) {
        s_min_bits = floatBitsToUint(1e30);
        s_max_bits = 0u;
        s_active_any = 0u;
        s_has_background = 0u;
    }
    for (uint i = tid; i < slices * words; i += THREADS)        // only these words of the masks are ever read or written
        s_mask[i / words][i % words] = 0u;
    barrier();

    // ---- 1. tile depth range (one block of texels per thread) -------------------------------------------------------
    {
        const ivec2 size = textureSize(depth_texture, 0);
        const uvec2 block = gl_LocalInvocationID.xy * BLOCK_SIZE;
        uint min_bits = 0xFFFFFFFFu;
        uint max_bits = 0u;
        bool geometry = false;
        bool background = false;
        for (uint y = 0u; y < BLOCK_SIZE.y; ++y) {
            for (uint x = 0u; x < BLOCK_SIZE.x; ++x) {
                const uvec2 texel = block + uvec2(x, y);        // within the tile
                const ivec2 pixel = ivec2(tile * TILE_SIZE + texel);
                if (texel.x >= TILE_SIZE.x || texel.y >= TILE_SIZE.y || pixel.x >= size.x || pixel.y >= size.y)
                    continue;
                const float d = texelFetch(depth_texture, pixel, 0).r;
                if (d < 1.0) {                                  // background: nothing to light
                    const uint lz_bits = floatBitsToUint(max(linearize_depth(d), 0.0));
                    min_bits = min(min_bits, lz_bits);          // positive floats order like their bit patterns
                    max_bits = max(max_bits, lz_bits);
                    geometry = true;
                } else {
                    background = true;
                }
            }
        }
        if (geometry) {
            atomicMin(s_min_bits, min_bits);
            atomicMax(s_max_bits, max_bits);
        }
        if (background)
            s_has_background = 1u;                              // benign race: every writer stores 1
    }
    barrier();

    float min_linear = uintBitsToFloat(s_min_bits);
    float max_linear = uintBitsToFloat(s_max_bits);
    bool  tile_has_geometry = s_max_bits != 0u && min_linear <= max_linear;

    // Translucents are not in the depth buffer. Extend the tile's range by their (screen-global) view depth span so
    // translucent fragments find lights. Only a tile FULLY covered by opaque geometry may cut the span at its farthest
    // opaque texel: if any texel shows the sky, a translucent behind the opaque part is still visible through it
    if (light_meta.meta.w != 0u) {
        const vec2  span = unpackHalf2x16(light_meta.meta.w);
        const float t_lo = span.x;
        const float t_hi = (tile_has_geometry && s_has_background == 0u) ? min(span.y, max_linear) : span.y;
        if (t_lo <= t_hi) {
            min_linear = tile_has_geometry ? min(min_linear, t_lo) : t_lo;
            max_linear = tile_has_geometry ? max(max_linear, t_hi) : t_hi;
            tile_has_geometry = true;
        }
    }

    // ---- 2. per-slice AABBs (thread t: slices t, t + THREADS, ...) --------------------------------------------------
    for (uint slice = tid; slice < slices; slice += THREADS) {
        vec3 mn, mx;
        const bool slice_live = tile_has_geometry && slice_aabb(tile, slice, slices, min_linear, max_linear, mn, mx);
        s_aabb_min[slice] = mn;
        s_aabb_max[slice] = mx;
        s_slice_active[slice] = slice_live ? 1u : 0u;
        if (slice_live)
            atomicOr(s_active_any, 1u);
    }
    barrier();

    // ---- 3. light tests (bitmask per slice) -------------------------------------------------------------------------
    if (s_active_any != 0u && light_count > 0u) {                // uniform across the workgroup

        for (uint batch = 0u; batch < light_count; batch += THREADS) {

            const uint batch_size = min(THREADS, light_count - batch);
            if (tid < batch_size) {
                Light light = light_buffer.lights[batch + tid];
                s_light_pos_radius[tid] = vec4((camera.view * vec4(light.position_radius.xyz, 1.0)).xyz, light.position_radius.w);
                const vec3  dir_vs = (camera.view * vec4(light.direction_inner_cos.xyz, 0.0)).xyz;
                const float len2 = dot(dir_vs, dir_vs);
                s_light_axis_cos[tid] = vec4(len2 > 1e-12 ? dir_vs * inversesqrt(len2) : vec3(0.0, 0.0, -1.0), light.params.x);
                s_light_type[tid] = uint(light.params.y);
            }
            barrier();

            for (uint pair = tid; pair < batch_size * slices; pair += THREADS) {

                const uint slice = pair / batch_size;
                const uint local = pair % batch_size;
                if (s_slice_active[slice] == 0u)
                    continue;

                const uint type = s_light_type[local];
                const vec4 pr = s_light_pos_radius[local];
                bool visible;
                if (type == 1u) {
                    visible = sphere_intersects_aabb(pr.xyz, pr.w, s_aabb_min[slice], s_aabb_max[slice]);
                } else if (type == 2u) {
                    visible = sphere_intersects_aabb(pr.xyz, pr.w, s_aabb_min[slice], s_aabb_max[slice]);
                    if (visible) {
                        const vec3  center = 0.5 * (s_aabb_min[slice] + s_aabb_max[slice]);
                        const float aabb_radius = 0.5 * length(s_aabb_max[slice] - s_aabb_min[slice]);
                        const vec4  ac = s_light_axis_cos[local];
                        visible = sphere_intersects_cone(pr.xyz, ac.xyz, ac.w, center, aabb_radius + pr.w);
                    }
                } else {
                    visible = true;                             // directional: every active cluster
                }

                if (visible) {
                    const uint light_index = batch + local;
                    atomicOr(s_mask[slice][light_index >> 5u], 1u << (light_index & 31u));
                }
            }
            barrier();
        }
    }

    // ---- 4. per slice: count, reserve, write --------------------------------------------------------------------------
    for (uint slice = tid; slice < slices; slice += THREADS) {
        uint running = 0u;
        for (uint w = 0u; w < words; ++w)
            running += uint(bitCount(s_mask[slice][w]));
        uint count = min(running, max_per_cluster);
        uint base = 0u;
        if (count > 0u) {
            base = atomicAdd(light_counter.next_free, count);
            const uint capacity = light_meta.meta.y;
            if (base >= capacity)
                count = 0u;                                     // overflow: drop rather than stomp another cluster's range
            else if (base + count > capacity)
                count = capacity - base;
        }
        s_base[slice] = base;
        s_count[slice] = count;
        light_grid.grid[slice * tiles_per_slice + tile_index] = uvec2(count > 0u ? base : 0u, count);
    }
    barrier();

    for (uint i = tid; i < slices * words; i += THREADS) {
        const uint slice = i / words;
        const uint w = i % words;
        uint bits = s_mask[slice][w];
        uint rank = 0u;                                         // lights of this slice in earlier words
        for (uint pw = 0u; pw < w; ++pw)
            rank += uint(bitCount(s_mask[slice][pw]));
        const uint count = s_count[slice];
        while (bits != 0u && rank < count) {
            const uint bit = uint(findLSB(bits));
            bits &= bits - 1u;
            light_index_list.indices[s_base[slice] + rank] = w * 32u + bit;
            ++rank;
        }
    }
}

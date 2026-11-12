
#version 450

// Virtual geometry: per-cluster level of detail + culling for the main view.
//
// Meshes on this path are stored as a hierarchy of clusters (core/src/asset/mesh_cluster.h). The CPU lists every
// instance that uses the path as work items of 64 clusters each; one workgroup handles one item, one thread one
// cluster. A cluster is drawn when
//      - its own simplification error is at most `error_threshold` on screen, and
//      - the error of the coarser clusters that would replace it is not
// Both tests of a group of clusters use the same sphere + error, and errors only grow towards the root of the
// hierarchy, so exactly one version of every piece of surface passes: no holes, no overlaps, no cracks.
// Surviving clusters are frustum and backface-cone culled and emitted as indexed indirect draws into the cluster
// command ranges of their material (same scheme as cull.comp.glsl, separate buffers).
//
// Shadow views do not run this shader: they keep drawing the mesh's discrete LODs through cull.comp / shadow_cull.comp.

layout(local_size_x = 64) in;

// Shared limits (MAX_MATERIALS, ...) are injected as #defines by the renderer's shader compiler from the C++
// constants (util/shader_compiler.cpp). Do not redeclare them here.

struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    uint mesh_index;
    uint material_index;
    uint flags;         // bits 0..3 forced LOD + 1, bit 4 virtual geometry, bit 5 backface cones usable
    uint _pad1;
};

struct MeshData {
    vec3 local_min;
    uint first_submesh;
    vec3 local_max;
    uint submesh_count;
    uint vertex_offset;
    uint index_offset;
    uint lod_count;
    uint _pad1;
    float lod_screen[8];    // [0] cull size (0 = never)
};

// gpu_mesh_cluster_data (shared/type.h), same index as MeshData
struct MeshClusterData {
    uint  first_cluster;
    uint  cluster_count;    // 0 = mesh without clusters
    uint  index_base;       // cluster indices start here inside the mesh's index range
    float error_scale;      // per mesh: > 1 keeps more detail
};

// GLT::asset::mesh::cluster (core/src/asset/mesh.h), 80 bytes. Spheres: xyz centre, w radius, mesh space
struct ClusterData {
    vec4  bounds;           // this cluster's triangles (culling)
    vec4  lod_bounds;       // group this cluster was simplified from
    vec4  parent_bounds;    // group that replaces this cluster with coarser geometry
    float lod_error;        // mesh units, 0 = source triangles
    float parent_error;     // FLT_MAX = nothing coarser exists
    uint  first_index;
    uint  index_count;
    uint  submesh;
    uint  depth;            // hierarchy level, 0 = source triangles
    uint  cone;             // bytes: axis x, y, z, cutoff (signed, / 127)
    uint  _pad;
};

struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer     { InstanceData instances[]; };
layout(std430, set = 0, binding = 1) readonly buffer MeshBuffer         { MeshData meshes[]; };
layout(std430, set = 0, binding = 2) readonly buffer MeshClusterBuffer  { MeshClusterData mesh_clusters[]; };
layout(std430, set = 0, binding = 3) readonly buffer ClusterBuffer      { ClusterData clusters[]; };
layout(std430, set = 0, binding = 4) writeonly buffer DrawCommandBuffer { DrawIndexedIndirectCommand commands[]; };
layout(std430, set = 0, binding = 5) buffer DrawCountBuffer             { uint counts[]; };
layout(set = 0, binding = 6) uniform CameraUBO {
    mat4  view_proj;
    mat4  view;
    vec4  camera_pos;
    vec4  frustum_planes[6];
    uvec4 flags;
    mat4  projection;
} camera;
// Compact per-instance material list: instance.material_index is the offset of the instance's first submesh entry
layout(std430, set = 0, binding = 7) readonly buffer MaterialTable      { uint material_table[]; };
// Per material slot: x = first cluster command of the material's range, y = range capacity (CPU-computed upper bound)
layout(std430, set = 0, binding = 8) readonly buffer MaterialRanges     { uvec2 material_ranges[]; };
// gpu_draw_stats (scene/type.h). The first four counters are shared with cull.comp
layout(std430, set = 0, binding = 9) buffer DrawStats {
    uint visible_draws;
    uint triangles;
    uint visible_instances;
    uint _pad;
    uint virtual_clusters;
    uint virtual_triangles;
    uint _pad1;
    uint _pad2;
} stats;
// x = instance index, y = first cluster of the item (relative to the mesh's clusters)
layout(std430, set = 0, binding = 10) readonly buffer WorkItems         { uvec2 work_items[]; };
// Parallel to `commands`: the cluster each command draws, as an index into `clusters`. The draw itself does not need
// it; the debug views (virtual_geometry_debug.vert.glsl) look the cluster up through gl_DrawID
layout(std430, set = 0, binding = 11) writeonly buffer DrawClusterBuffer { uint draw_clusters[]; };

layout(push_constant) uniform PushConstants {
    uint  item_count;       // valid entries of work_items
    uint  dispatch_width;   // workgroups per dispatch row: item = WorkGroupID.x + WorkGroupID.y * dispatch_width
    float error_threshold;  // allowed error as a fraction of the viewport height (pixel error / height)
    float projection_scale; // |P11| * 0.5: world size / distance -> fraction of the viewport height
    float near_plane;
    uint  flags;            // FLAG_*
} pc;

const uint FLAG_CONE_CULLING   = 1u;        // backface cone culling allowed (renderer setting)

// gpu_instance_data::flags (scene/type.h)
const uint INSTANCE_FLAG_CONE_CULL = 1u << 5u;

// firstInstance encoding, see cull.comp.glsl
const uint INSTANCE_LOD_SHIFT = 20u;
const uint INSTANCE_HAS_LODS  = 1u << 24u;

const float CLUSTER_ERROR_NONE = 3.0e38;    // parent_error at or above this: the cluster has no coarser replacement


// Error of a group seen from the nearest point of its sphere, as a fraction of the viewport height. `precise`: the
// clusters made from a group and the clusters it replaces must come to exactly the same value, whatever the compiler
// does with the expression at the two call sites
float projected_error(vec4 sphere, float error, mat4 model, float scale, float error_factor) {

    precise vec3 center = (model * vec4(sphere.xyz, 1.0)).xyz;
    precise float dist = max(distance(center, camera.camera_pos.xyz) - sphere.w * scale, pc.near_plane);
    precise float projected = error * error_factor / dist;
    return projected;
}


void main() {

    const uint item_index = gl_WorkGroupID.x + gl_WorkGroupID.y * pc.dispatch_width;
    if (item_index >= pc.item_count)
        return;

    const uvec2 item = work_items[item_index];
    const uint instance_index = item.x;
    if (instance_index >= camera.flags.x || instance_index >= uint(instances.length()))
        return;

    const InstanceData instance = instances[instance_index];
    const MeshClusterData mesh_cluster = mesh_clusters[instance.mesh_index];
    const uint local_cluster = item.y + gl_LocalInvocationID.x;
    if (local_cluster >= mesh_cluster.cluster_count)
        return;                                         // tail of the last item, or the mesh lost its clusters

    const MeshData mesh = meshes[instance.mesh_index];

    // ---- whole instance: frustum + "cull below", same rules as cull.comp --------------------------------------------
    vec3 world_min = vec3( 1e30);
    vec3 world_max = vec3(-1e30);
    for (uint ci = 0u; ci < 8u; ++ci) {
        const vec3 corner = mix(mesh.local_min, mesh.local_max, vec3(
            (ci & 1u) != 0u ? 1.0 : 0.0,
            (ci & 2u) != 0u ? 1.0 : 0.0,
            (ci & 4u) != 0u ? 1.0 : 0.0));
        const vec3 world = (instance.model * vec4(corner, 1.0)).xyz;
        world_min = min(world_min, world);
        world_max = max(world_max, world);
    }
    for (int i = 0; i < 6; ++i) {
        const vec4 plane = camera.frustum_planes[i];
        const vec3 positive = mix(world_min, world_max, step(vec3(0.0), plane.xyz));
        if (dot(plane.xyz, positive) + plane.w < 0.0)
            return;
    }

    const float scale = max(length(instance.model[0].xyz), max(length(instance.model[1].xyz), length(instance.model[2].xyz)));
    if (mesh.lod_screen[0] > 0.0) {
        const vec3  center = (instance.model * vec4(0.5 * (mesh.local_min + mesh.local_max), 1.0)).xyz;
        const float radius = 0.5 * length(mesh.local_max - mesh.local_min) * scale;
        const float dist = distance(center, camera.camera_pos.xyz);
        const float screen = dist > radius ? radius * abs(camera.projection[1][1]) / dist : 1e9;
        if (screen < mesh.lod_screen[0])
            return;                                     // "Cull below": too small to draw at all
    }

    const ClusterData cluster = clusters[mesh_cluster.first_cluster + local_cluster];
    if (cluster.index_count < 3u || cluster.submesh >= mesh.submesh_count)
        return;

    // ---- level of detail: this cluster is detailed enough, its coarser replacement is not ---------------------------
    const float error_factor = scale * mesh_cluster.error_scale * pc.projection_scale;
    if (cluster.lod_error > 0.0
        && projected_error(cluster.lod_bounds, cluster.lod_error, instance.model, scale, error_factor) > pc.error_threshold)
        return;                                         // too coarse for this view: finer clusters are drawn instead
    if (cluster.parent_error < CLUSTER_ERROR_NONE
        && projected_error(cluster.parent_bounds, cluster.parent_error, instance.model, scale, error_factor) <= pc.error_threshold)
        return;                                         // the coarser replacement is good enough: it is drawn instead

    // ---- cluster culling --------------------------------------------------------------------------------------------
    const vec3  center = (instance.model * vec4(cluster.bounds.xyz, 1.0)).xyz;
    const float radius = cluster.bounds.w * scale;
    for (int i = 0; i < 6; ++i) {
        const vec4 plane = camera.frustum_planes[i];
        if (dot(plane.xyz, center) + plane.w < -radius)
            return;
    }

    // Backface cone: every triangle of the cluster faces away from the camera. Only for instances the CPU cleared for it
    // (rigid transform with uniform scale, no double-sided material)
    if ((pc.flags & FLAG_CONE_CULLING) != 0u && (instance.flags & INSTANCE_FLAG_CONE_CULL) != 0u) {
        const ivec4 cone = ivec4(int(cluster.cone << 24u) >> 24, int(cluster.cone << 16u) >> 24, int(cluster.cone << 8u) >> 24,
            int(cluster.cone) >> 24);
        if (cone.w < 127) {
            const vec3  axis = normalize(mat3(instance.model) * (vec3(cone.xyz) / 127.0));
            const float cutoff = float(cone.w) / 127.0;
            const vec3  to_cluster = center - camera.camera_pos.xyz;
            if (dot(to_cluster, axis) >= cutoff * length(to_cluster) + radius)
                return;
        }
    }

    // ---- emit -------------------------------------------------------------------------------------------------------
    const uint mat_slot = material_table[instance.material_index + cluster.submesh];
    if (mat_slot >= MAX_MATERIALS)
        return;

    const uvec2 range = material_ranges[mat_slot];
    if (range.y == 0u)
        return;                                         // translucent material: drawn by the sorted translucent pass
    const uint slot = atomicAdd(counts[mat_slot], 1u);
    if (slot >= range.y)
        return;                                         // cannot happen (CPU sized the range), but never write outside it

    // LOD nibble of firstInstance: the hierarchy level (render mode "Level of Detail" colours by it)
    const uint level = min(cluster.depth, 15u);

    const uint cmd = range.x + slot;
    commands[cmd].indexCount = cluster.index_count;
    commands[cmd].instanceCount = 1u;
    commands[cmd].firstIndex = mesh.index_offset + mesh_cluster.index_base + cluster.first_index;
    commands[cmd].vertexOffset = int(mesh.vertex_offset);
    commands[cmd].firstInstance = instance_index | (level << INSTANCE_LOD_SHIFT) | INSTANCE_HAS_LODS;
    draw_clusters[cmd] = mesh_cluster.first_cluster + local_cluster;

    const uint cluster_triangles = cluster.index_count / 3u;
    atomicAdd(stats.visible_draws, 1u);
    atomicAdd(stats.triangles, cluster_triangles);
    atomicAdd(stats.virtual_clusters, 1u);
    atomicAdd(stats.virtual_triangles, cluster_triangles);
}

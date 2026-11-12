#version 460

// Render modes "Virtual Geometry / ..." (scene_renderer_virtual_geometry_debug.inl). One pass redraws what the lit
// path draws, with the same indirect commands, and colours it:
//      VIEW_CLUSTERS   every cluster its own colour
//      VIEW_LEVELS     hierarchy level of the cluster (green = source triangles)
//      VIEW_TRIANGLES  every triangle its own colour (fragment shader, virtual_geometry_debug_triangles.frag.glsl)
//      VIEW_ERROR      projected simplification error of the cluster against the allowed error
// Cluster draws find their cluster through gl_DrawID: virtual_geometry_cull.comp writes, for every command, the index
// of the cluster it draws into a list parallel to the commands (binding 4). Meshes drawn with discrete LODs and
// procedural meshes are grey.
//
// Keep id_color() / level_color() in sync with GLT::asset::mesh::cluster_debug_color() / cluster_level_color()
// (core/src/asset/mesh_cluster.cpp): the mesh editor draws cluster bounds in the same colours.

layout(location = 0) in vec3 in_position;

layout(set = 0, binding = 0) uniform Camera {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
} camera;

struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    uint mesh_index;
    uint material_index;
    uint flags;
    uint _pad1;
};

// gpu_mesh_cluster_data (shared/type.h)
struct MeshClusterData {
    uint  first_cluster;
    uint  cluster_count;
    uint  index_base;
    float error_scale;
};

// GLT::asset::mesh::cluster (core/src/asset/mesh.h), 80 bytes
struct ClusterData {
    vec4  bounds;
    vec4  lod_bounds;
    vec4  parent_bounds;
    float lod_error;
    float parent_error;
    uint  first_index;
    uint  index_count;
    uint  submesh;
    uint  depth;
    uint  cone;
    uint  _pad;
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer    { InstanceData instances[]; };
layout(std430, set = 0, binding = 2) readonly buffer MeshClusterBuffer { MeshClusterData mesh_clusters[]; };
layout(std430, set = 0, binding = 3) readonly buffer ClusterBuffer     { ClusterData clusters[]; };
layout(std430, set = 0, binding = 4) readonly buffer DrawClusterBuffer { uint draw_clusters[]; };

layout(push_constant) uniform PushConstants {
    uint  view;             // VIEW_*
    uint  kind;             // KIND_*: what the current draw call draws
    uint  command_base;     // KIND_CLUSTERS: index of the indirect call's first command (= first entry of draw_clusters)
    float error_threshold;  // the cull pass's values, see virtual_geometry_cull.comp.glsl
    float projection_scale;
    float near_plane;
} pc;

const uint VIEW_CLUSTERS  = 1u;
const uint VIEW_LEVELS    = 2u;
const uint VIEW_TRIANGLES = 3u;
const uint VIEW_ERROR     = 4u;

const uint KIND_MESH       = 0u;    // discrete LOD draw (opaque or translucent)
const uint KIND_CLUSTERS   = 1u;    // virtual geometry: one command per cluster
const uint KIND_PROCEDURAL = 2u;

layout(location = 0) out vec3 out_world;
layout(location = 1) flat out vec3 out_color;
layout(location = 2) flat out uint out_seed;    // VIEW_TRIANGLES: per-draw seed of the triangle colours


uint hash(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

vec3 hsv(float h, float s, float v) {
    const vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k, s);
}

// A well separated colour per id
vec3 id_color(uint id) {
    const uint h = hash(id + 1u);
    return hsv(float(h & 0xFFFFu) / 65536.0, 0.55 + 0.35 * float((h >> 16u) & 0xFFu) / 255.0, 0.70 + 0.30 * float(h >> 24u) / 255.0);
}

// Green (source triangles), then through yellow, red, magenta and blue to cyan as the level gets coarser
vec3 level_color(uint depth) {
    return hsv(fract(1.0 / 3.0 - float(min(depth, 15u)) * 0.06), 0.85, 0.90);
}

// 0 = blue, 1 (at the allowed error) = red
vec3 error_color(float fraction) {
    return hsv((1.0 - clamp(fraction, 0.0, 1.0)) * 0.66, 0.85, 0.90);
}

// Same expression as virtual_geometry_cull.comp.glsl
float projected_error(vec4 sphere, float error, mat4 model, float scale, float error_factor) {
    const vec3  center = (model * vec4(sphere.xyz, 1.0)).xyz;
    const float dist = max(distance(center, camera.camera_pos.xyz) - sphere.w * scale, pc.near_plane);
    return error * error_factor / dist;
}


void main() {

    // gl_InstanceIndex includes firstInstance = instance index | LOD info (cull.comp.glsl)
    const InstanceData inst = instances[uint(gl_InstanceIndex) & 0xFFFFFu];
    const vec4 world = inst.model * vec4(in_position, 1.0);
    gl_Position = camera.view_proj * world;
    out_world = world.xyz;

    vec3 color = pc.kind == KIND_PROCEDURAL ? vec3(0.55, 0.55, 0.57) : vec3(0.30, 0.30, 0.33);
    uint seed = hash(uint(gl_InstanceIndex));

    if (pc.kind == KIND_CLUSTERS) {

        const uint entry = pc.command_base + uint(gl_DrawID);
        const uint cluster_index = entry < uint(draw_clusters.length()) ? draw_clusters[entry] : 0xFFFFFFFFu;
        const MeshClusterData mesh_cluster = mesh_clusters[inst.mesh_index];

        if (cluster_index < uint(clusters.length()) && cluster_index >= mesh_cluster.first_cluster) {

            const ClusterData cluster = clusters[cluster_index];
            const uint local_cluster = cluster_index - mesh_cluster.first_cluster;   // stable: independent of where the mesh sits in the pool
            seed = hash(seed ^ (local_cluster * 0x9E3779B1u));

            if (pc.view == VIEW_LEVELS) {
                color = level_color(cluster.depth);
            } else if (pc.view == VIEW_ERROR) {
                if (cluster.lod_error > 0.0) {
                    const float scale = max(length(inst.model[0].xyz), max(length(inst.model[1].xyz), length(inst.model[2].xyz)));
                    const float projected = projected_error(cluster.lod_bounds, cluster.lod_error, inst.model, scale,
                        scale * mesh_cluster.error_scale * pc.projection_scale);
                    color = error_color(projected / max(pc.error_threshold, 1e-9));
                } else {
                    color = vec3(0.16, 0.22, 0.42);         // source triangles: nothing was simplified
                }
            } else {
                color = id_color(local_cluster);            // VIEW_CLUSTERS, and VIEW_TRIANGLES without per-triangle colours
            }
        } else {
            color = vec3(1.0, 0.0, 1.0);                    // no cluster for this command: must not happen
        }
    }

    out_color = color;
    out_seed = seed;
}


#version 450

layout(local_size_x = 64) in;

// Shared limits (MAX_MATERIALS, MAX_SUBMESHES_PER_MESH, ...) are injected as #defines by the renderer's shader
// compiler from the C++ constants (util/shader_compiler.cpp). Do not redeclare them here.

struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    uint mesh_index;
    uint material_index;
    uint _pad0;
    uint _pad1;
};

struct MeshData {
    vec3 local_min;
    uint first_submesh;
    vec3 local_max;
    uint submesh_count;
    uint vertex_offset;
    uint index_offset;
    uint _pad0;
    uint _pad1;
};

struct SubmeshData {
    uint first_index;
    uint index_count;
    uint material_index;
    uint _pad0;
};

struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer  { InstanceData instances[]; };
layout(std430, set = 0, binding = 1) readonly buffer MeshBuffer      { MeshData meshes[]; };
layout(std430, set = 0, binding = 2) readonly buffer SubmeshBuffer   { SubmeshData submeshes[]; };
layout(std430, set = 0, binding = 3) writeonly buffer DrawCommandBuffer { DrawIndexedIndirectCommand commands[]; };
layout(std430, set = 0, binding = 4) buffer DrawCountBuffer          { uint counts[]; };
layout(set = 0, binding = 5) uniform CameraUBO {
    mat4  view_proj;
    mat4  view;
    vec4  camera_pos;
    vec4  frustum_planes[6];
    uvec4 flags;        // x = number of valid instances this frame (the SSBO holds stale data past it), y = write stats
} camera;
// Compact per-instance material list: instance.material_index is the offset of the instance's first submesh entry
layout(std430, set = 0, binding = 6) readonly buffer MaterialTable { uint material_table[]; };
// Per material slot: x = first command of the material's range, y = range capacity (CPU-computed upper bound of draws)
layout(std430, set = 0, binding = 7) readonly buffer MaterialRanges { uvec2 material_ranges[]; };
// Main-view statistics, read back on the host (only written when camera.flags.y != 0)
layout(std430, set = 0, binding = 8) buffer DrawStats {
    uint visible_draws;
    uint triangles;
    uint visible_instances;
    uint _pad;
} stats;


vec3 aabb_world_corner(vec3 mn, vec3 mx, mat4 model, uint ci) {

    vec3 c = mix(mn, mx, vec3(
        (ci & 1u) != 0u ? 1.0 : 0.0,
        (ci & 2u) != 0u ? 1.0 : 0.0,
        (ci & 4u) != 0u ? 1.0 : 0.0));
    return (model * vec4(c, 1.0)).xyz;
}


bool frustum_test(vec3 mn, vec3 mx) {

    for (int i = 0; i < 6; ++i) {
        vec4 p = camera.frustum_planes[i];
        vec3 positive = mix(mn, mx, step(vec3(0.0), p.xyz));
        if (dot(p.xyz, positive) + p.w < 0.0)
            return false;
    }
    return true;
}


void main() {

    const uint instance_index = gl_GlobalInvocationID.x;
    if (instance_index >= camera.flags.x || instance_index >= uint(instances.length()))
        return;

    const InstanceData instance = instances[instance_index];
    const MeshData mesh = meshes[instance.mesh_index];
    if (mesh.submesh_count == 0u)
        return;

    vec3 world_min = vec3( 1e30);
    vec3 world_max = vec3(-1e30);
    for (uint ci = 0u; ci < 8u; ++ci) {
        vec3 c = aabb_world_corner(mesh.local_min, mesh.local_max, instance.model, ci);
        world_min = min(world_min, c);
        world_max = max(world_max, c);
    }
    if (!frustum_test(world_min, world_max))
        return;

    uint emitted_draws = 0u;
    uint emitted_triangles = 0u;

    for (uint sm = 0u; sm < mesh.submesh_count; ++sm) {

        const SubmeshData submesh = submeshes[mesh.first_submesh + sm];
        if (submesh.index_count < 3u)
            continue;

        const uint mat_slot = material_table[instance.material_index + sm];
        if (mat_slot >= MAX_MATERIALS)
            continue;

        const uvec2 range = material_ranges[mat_slot];
        const uint slot = atomicAdd(counts[mat_slot], 1u);
        if (slot >= range.y)
            continue;                                   // cannot happen (CPU sized the range), but never write outside it

        const uint cmd = range.x + slot;
        commands[cmd].indexCount = submesh.index_count;
        commands[cmd].instanceCount = 1u;
        commands[cmd].firstIndex = mesh.index_offset + submesh.first_index;
        commands[cmd].vertexOffset = int(mesh.vertex_offset);
        commands[cmd].firstInstance = instance_index;

        emitted_draws++;
        emitted_triangles += submesh.index_count / 3u;
    }

    if (camera.flags.y != 0u && emitted_draws > 0u) {
        atomicAdd(stats.visible_draws, emitted_draws);
        atomicAdd(stats.triangles, emitted_triangles);
        atomicAdd(stats.visible_instances, 1u);
    }
}

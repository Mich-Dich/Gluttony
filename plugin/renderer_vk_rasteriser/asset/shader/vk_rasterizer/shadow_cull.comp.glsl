
#version 450

layout(local_size_x = 64) in;

struct InstanceData { mat4 model;
    mat4        normal_matrix;
    uint        mesh_index;
    uint        material_index;
    uint        _pad0;
    uint        _pad1;
};

struct MeshData { vec3 local_min;
    uint        first_submesh;
    vec3        local_max;
    uint        submesh_count;
    uint        vertex_offset;
    uint        index_offset;
    uint        _pad0;
    uint        _pad1;
};

struct SubmeshData {
    uint        first_index;
    uint        index_count;
    uint        material_index;
    uint        _pad0;
};

struct DrawIndexedIndirectCommand {
    uint        indexCount;
    uint        instanceCount;
    uint        firstIndex;
    int         vertexOffset;
    uint        firstInstance;
};

struct ShadowView {
    mat4        view_proj;
    vec4        atlas_rect;
    vec4        params;
    vec4        sphere;
    vec4        planes[6];
    vec4        _pad[3];
};

// Commands are bucketed per (view, material) because the draw pass binds one pipeline per material: the per-material
// ranges are the same as the main view's (material_ranges), repeated once per view (view_stride apart).
// Shared limits (MAX_MATERIALS, MAX_SUBMESHES_PER_MESH, ...) are injected as #defines by the renderer's shader
// compiler from the C++ constants (util/shader_compiler.cpp). Do not redeclare them here.

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer { InstanceData instances[]; };
layout(std430, set = 0, binding = 1) readonly buffer MeshBuffer { MeshData meshes[]; };
layout(std430, set = 0, binding = 2) readonly buffer SubmeshBuffer { SubmeshData submeshes[]; };
layout(std430, set = 0, binding = 3) writeonly buffer DrawCommandBuffer { DrawIndexedIndirectCommand commands[]; };
layout(std430, set = 0, binding = 4) buffer DrawCountBuffer { uint counts[]; };
layout(std430, set = 0, binding = 5) readonly buffer ShadowViewBuffer { ShadowView views[]; };
layout(std430, set = 0, binding = 6) readonly buffer MaterialTable { uint material_table[]; };   // see cull.comp.glsl
layout(std430, set = 0, binding = 7) readonly buffer MaterialRanges { uvec2 material_ranges[]; };

layout(push_constant) uniform PushConstants {
    uint instance_count;    // number of valid instances this frame (the SSBO holds stale data past it)
    uint view_stride;       // commands per view: view v's material ranges start at v * view_stride
} pc;



void main() {

    const uint view_index = gl_WorkGroupID.y;
    const uint instance_index = gl_GlobalInvocationID.x;
    if (instance_index >= pc.instance_count || instance_index >= uint(instances.length()))
        return;

    const ShadowView view = views[view_index];
    if (view.atlas_rect.z <= 0.0)
        return;                                         // no tile for this view

    const InstanceData instance = instances[instance_index];
    const MeshData mesh = meshes[instance.mesh_index];
    if (mesh.submesh_count == 0u)
        return;

    vec3 world_min = vec3( 1e30);
    vec3 world_max = vec3(-1e30);
    for (uint corner_index = 0u; corner_index < 8u; ++corner_index) {
        const vec3 corner = mix(mesh.local_min, mesh.local_max, vec3(
            (corner_index & 1u) != 0u ? 1.0 : 0.0,
            (corner_index & 2u) != 0u ? 1.0 : 0.0,
            (corner_index & 4u) != 0u ? 1.0 : 0.0));
        const vec3 world = (instance.model * vec4(corner, 1.0)).xyz;
        world_min = min(world_min, world);
        world_max = max(world_max, world);
    }

    // 1. Light sphere vs AABB
    const vec3 closest = clamp(view.sphere.xyz, world_min, world_max);
    const vec3 delta = closest - view.sphere.xyz;
    if (dot(delta, delta) > view.sphere.w * view.sphere.w)
        return;

    // 2. View frustum
    for (int i = 0; i < 6; ++i) {
        const vec4 plane = view.planes[i];
        const vec3 positive_vertex = mix(world_min, world_max, step(vec3(0.0), plane.xyz));
        if (dot(plane.xyz, positive_vertex) + plane.w < 0.0)
            return;
    }

    // 3. Small-caster rejection: smaller than ~1 shadow texel at its distance -> invisible in the map anyway
    const float radius = length(world_max - world_min) * 0.5;
    const float dist = distance((world_min + world_max) * 0.5, view.sphere.xyz);
    if (radius < dist * view.params.x)
        return;

    for (uint submesh_offset = 0u; submesh_offset < mesh.submesh_count; ++submesh_offset) {

        const SubmeshData submesh = submeshes[mesh.first_submesh + submesh_offset];
        if (submesh.index_count < 3u)
            continue;

        const uint mat_slot = material_table[instance.material_index + submesh_offset];
        if (mat_slot >= MAX_MATERIALS)
            continue;

        const uvec2 range = material_ranges[mat_slot];
        const uint slot = atomicAdd(counts[view_index * MAX_MATERIALS + mat_slot], 1u);
        if (slot >= range.y)
            continue;

        const uint cmd = view_index * pc.view_stride + range.x + slot;
        commands[cmd].indexCount = submesh.index_count;
        commands[cmd].instanceCount = 1u;
        commands[cmd].firstIndex = mesh.index_offset + submesh.first_index;
        commands[cmd].vertexOffset = int(mesh.vertex_offset);
        commands[cmd].firstInstance = instance_index;
    }
}

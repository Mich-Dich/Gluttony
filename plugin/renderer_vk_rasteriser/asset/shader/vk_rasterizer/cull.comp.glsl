
#version 450

layout(local_size_x = 64) in;

struct InstanceData {
    mat4                    model;
    mat4                    normal_matrix;
    uint                    mesh_index;
    uint                    material_index;
    uint                    _pad0;
    uint                    _pad1;
};

struct MeshData {
    vec3                    local_min;
    uint                    first_submesh;
    vec3                    local_max;
    uint                    submesh_count;
    uint                    vertex_offset;
    uint                    index_offset;
    uint                    _pad0;
    uint                    _pad1;
};

struct SubmeshData {
    uint                    first_index;
    uint                    index_count;
};

struct DrawIndexedIndirectCommand {
    uint                    indexCount;
    uint                    instanceCount;
    uint                    firstIndex;
    int                     vertexOffset;
    uint                    firstInstance;
};

layout(std430, set = 0, binding = 0) readonly buffer InstanceBuffer { InstanceData instances[]; };
layout(std430, set = 0, binding = 1) readonly buffer MeshBuffer { MeshData meshes[]; };
layout(std430, set = 0, binding = 2) readonly buffer SubmeshBuffer { SubmeshData submeshes[]; };
layout(std430, set = 0, binding = 3) writeonly buffer DrawCommandBuffer { DrawIndexedIndirectCommand commands[]; };
layout(std430, set = 0, binding = 4) buffer DrawCountBuffer { uint draw_count; };
layout(set = 0, binding = 5) uniform CameraUBO {
    mat4                    view_proj;
    mat4                    view;
    vec4                    camera_pos;
    vec4                    frustum_planes[6];
    uvec4                   flags;
} camera;



// 8 corners, no AABB transform matrix needed
vec3 aabb_world_corner(vec3 local_min, vec3 local_max, mat4 model, uint corner_index) {

    vec3 corner = mix(local_min, local_max, vec3(
        (corner_index & 1u) != 0u ? 1.0 : 0.0,
        (corner_index & 2u) != 0u ? 1.0 : 0.0,
        (corner_index & 4u) != 0u ? 1.0 : 0.0));
    return (model * vec4(corner, 1.0)).xyz;
}


bool frustum_test(vec3 world_min, vec3 world_max) {

    for (int plane_index = 0; plane_index < 6; ++plane_index) {
        vec4 plane = camera.frustum_planes[plane_index];
        // Positive vertex: the AABB corner that lies furthest in the direction of the plane's normal. If even that one is behind the
        // plane, the whole AABB is behind it
        vec3 positive_vertex = mix(world_min, world_max, step(vec3(0.0), plane.xyz));
        if (dot(plane.xyz, positive_vertex) + plane.w < 0.0)
            return false;
    }
    return true;
}



void main() {

    const uint instance_index = gl_GlobalInvocationID.x;
    if (instance_index >= uint(instances.length()))
        return;

    const InstanceData instance = instances[instance_index];
    const MeshData mesh = meshes[instance.mesh_index];

    if (mesh.submesh_count == 0u)
        return;

    // Transform the local AABB to world space, then test against the frustum.
    vec3 world_min = vec3( 1e30);
    vec3 world_max = vec3(-1e30);
    for (uint corner_index = 0u; corner_index < 8u; ++corner_index) {
        vec3 corner = aabb_world_corner(mesh.local_min, mesh.local_max, instance.model, corner_index);
        world_min = min(world_min, corner);
        world_max = max(world_max, corner);
    }

    if (!frustum_test(world_min, world_max))
        return;

    // Visible. Emit one command per submesh, each drawing a single instance.
    for (uint submesh_offset = 0u; submesh_offset < mesh.submesh_count; ++submesh_offset) {

        const SubmeshData submesh = submeshes[mesh.first_submesh + submesh_offset];
        if (submesh.index_count < 3u)
            continue;

        const uint command_index = atomicAdd(draw_count, 1u);
        if (command_index >= uint(commands.length()))
            continue;

        commands[command_index].indexCount = submesh.index_count;
        commands[command_index].instanceCount = 1u;
        commands[command_index].firstIndex = mesh.index_offset + submesh.first_index;
        commands[command_index].vertexOffset = int(mesh.vertex_offset);
        commands[command_index].firstInstance = instance_index;
    }
}

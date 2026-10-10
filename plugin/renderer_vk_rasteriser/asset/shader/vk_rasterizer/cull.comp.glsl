
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
    uvec4                   flags;                              // x = hiz_enabled
} camera;
layout(set = 0, binding = 6) uniform sampler2D hiz_pyramid;     // HiZ pyramid, full mip chain, sampled with textureLod



// Project an AABB corner into NDC. Returns false if the corner is behind the camera (w <= 0), in which case the caller falls back to no-occlusion
bool project_corner(vec3 local_corner, mat4 model, out vec3 ndc, out float clip_w) {

    vec4 clip = camera.view_proj * model * vec4(local_corner, 1.0);
    if (clip.w <= 1e-6)
        return false;
    ndc = clip.xyz / clip.w;
    clip_w = clip.w;
    return true;
}


// Returns true if the instance should be culled by HiZ. Everything is conservative: on any uncertainty, return false (keep the instance)
bool is_occluded_by_hiz(InstanceData instance, MeshData mesh) {

    if (camera.flags.x == 0u)
        return false;

    // Project all 8 AABB corners.
    vec3 ndc_min = vec3( 1e30);
    vec3 ndc_max = vec3(-1e30);
    for (uint corner_index = 0u; corner_index < 8u; ++corner_index) {

        vec3 local_corner = mix(mesh.local_min, mesh.local_max, vec3(
            (corner_index & 1u) != 0u ? 1.0 : 0.0,
            (corner_index & 2u) != 0u ? 1.0 : 0.0,
            (corner_index & 4u) != 0u ? 1.0 : 0.0));

        vec3 ndc;
        float clip_w;
        if (!project_corner(local_corner, instance.model, ndc, clip_w))
            return false;      // AABB crosses the near plane, can't safely test

        ndc_min = min(ndc_min, ndc);
        ndc_max = max(ndc_max, ndc);
    }

    // If the AABB's nearest corner is already past the far plane, nothing would be drawn anyway; let the frustum test handle it
    if (ndc_min.z > 1.0)
        return false;

    // Convert NDC to pyramid UV coordinates. The pyramid covers the same screen extents as the depth buffer, but at half resolution on mip 0
    vec2 uv_min = ndc_min.xy * 0.5 + 0.5;
    vec2 uv_max = ndc_max.xy * 0.5 + 0.5;
    uv_min = clamp(uv_min, vec2(0.0), vec2(1.0));
    uv_max = clamp(uv_max, vec2(0.0), vec2(1.0));

    // Choose a mip level so each pyramid texel covers roughly the same number of pixels as the AABB's larger screen-space extent
    // Mip N covers 2^(N+1) full-res pixels, so N ~= log2(size) - 1
    ivec2 pyramid_size = textureSize(hiz_pyramid, 0);
    vec2 screen_size_px = (uv_max - uv_min) * vec2(pyramid_size);
    float max_screen_dim = max(screen_size_px.x, screen_size_px.y);
    float mip_f = log2(max(max_screen_dim, 1.0)) - 1.0;
    int mip = clamp(int(mip_f), 0, int(textureQueryLevels(hiz_pyramid)) - 1);

    // Sample the 4 AABB corners and the center at that mip. Take the max. Under-sampling would let us incorrectly cull,
    // so we over-sample slightly by also taking the center
    vec2 center_uv = (uv_min + uv_max) * 0.5;
    float pyramid_max = 0.0;
    pyramid_max = max(pyramid_max, textureLod(hiz_pyramid, vec2(uv_min.x, uv_min.y), float(mip)).r);
    pyramid_max = max(pyramid_max, textureLod(hiz_pyramid, vec2(uv_max.x, uv_min.y), float(mip)).r);
    pyramid_max = max(pyramid_max, textureLod(hiz_pyramid, vec2(uv_min.x, uv_max.y), float(mip)).r);
    pyramid_max = max(pyramid_max, textureLod(hiz_pyramid, vec2(uv_max.x, uv_max.y), float(mip)).r);
    pyramid_max = max(pyramid_max, textureLod(hiz_pyramid, center_uv, float(mip)).r);

    // Occluded if the AABB's nearest NDC depth is behind the pyramid's furthest depth in that screen region.
    return ndc_min.z > pyramid_max;
}

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

    if (is_occluded_by_hiz(instance, mesh))
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

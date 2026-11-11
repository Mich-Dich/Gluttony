#version 450

// "Level of Detail" render mode: positions only, colour picked from the LOD cull.comp chose for the instance

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    uint mesh_index;
    uint material_index;
    uint lod_override;
    uint _pad1;
};

// Matches the prefix of camera_ubo_data
layout(set = 0, binding = 0) uniform Camera {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
} camera;
layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer { InstanceData instances[]; };
layout(std430, set = 0, binding = 2) readonly buffer InstanceLods { uint instance_lods[]; };

layout(push_constant) uniform PushConstants {
    uint source;            // 0 = instance_lods (asset meshes), 1 = procedural mesh
    uint _pad;
} pc;

layout(location = 0) flat out uint out_lod;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_view_dir;


void main() {

    const uint instance_index = uint(gl_InstanceIndex);
    const InstanceData instance = instances[instance_index];

    const vec4 world = instance.model * vec4(in_position, 1.0);
    gl_Position = camera.view_proj * world;

    out_lod = pc.source == 1u ? 0x200u
            : (instance_index < uint(instance_lods.length()) ? instance_lods[instance_index] : 0u);
    out_normal = mat3(instance.normal_matrix) * in_normal;
    out_view_dir = camera.camera_pos.xyz - world.xyz;
}

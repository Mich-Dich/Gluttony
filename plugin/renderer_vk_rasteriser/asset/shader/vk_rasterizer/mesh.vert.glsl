
#version 450

struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    uint mesh_index;
    uint material_index;
    uint _pad1;
    uint _pad2;
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer { InstanceData instances[]; } instance_buffer;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
} camera;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv0;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;
layout(location = 3) out vec4 out_tangent;
layout(location = 4) flat out uint out_material_index;

const uint INSTANCE_INDEX_BITS = 16u;   // keep in sync with types.h

void main() {

    // The cull shaders pack gl_InstanceIndex = instance_index | (material_index << 16). The shadow cull shaders leave the upper
    // bits at zero, which is harmless because the depth-only fragment shader ignores the material
    const uint packed_index = uint(gl_InstanceIndex);
    const InstanceData instance = instance_buffer.instances[packed_index & ((1u << INSTANCE_INDEX_BITS) - 1u)];
    out_material_index = packed_index >> INSTANCE_INDEX_BITS;

    const vec4 world_position = instance.model * vec4(in_position, 1.0);
    out_world_pos = world_position.xyz;
    out_normal = normalize(mat3(instance.normal_matrix) * in_normal);
    out_tangent = vec4(normalize(mat3(instance.model) * in_tangent.xyz), in_tangent.w);
    out_uv = in_uv0;

    gl_Position = camera.view_proj * world_position;
}

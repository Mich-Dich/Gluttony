
#version 450

// Per-instance data. Layout is std430; must match gpu_instance_data in C++ -----------------------
struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    uint material_index;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

layout(std430, set = 0, binding = 1) readonly buffer InstanceBuffer {
    InstanceData instances[];
} instance_buffer;

// Camera UBO, shared across all draws in a frame -------------------------------------------------
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
} camera;

// Vertex inputs ----------------------------------------------------------------------------------
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv0;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;



void main() {

    // gl_InstanceIndex already includes the firstInstance base that was passed to vkCmdDrawIndexed, so this is a direct
    // lookup into the per-frame instance array
    InstanceData instance = instance_buffer.instances[gl_InstanceIndex];

    vec4 world_position = instance.model * vec4(in_position, 1.0);
    out_world_pos = world_position.xyz;

    // The normal matrix is precomputed on the CPU and stored alongside the model matrix, so no inverse/transpose is needed here
    out_normal = normalize(mat3(instance.normal_matrix) * in_normal);

    out_uv = in_uv0;

    gl_Position = camera.view_proj * world_position;
}

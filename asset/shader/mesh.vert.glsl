
#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
} camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec4 in_tangent;
layout(location = 3) in vec2 in_uv0;

layout(location = 0) out vec3 out_world_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec2 out_uv;

void main() {
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    out_world_pos = world_pos.xyz;
    out_normal    = normalize(mat3(pc.model) * in_normal);
    out_uv        = in_uv0;
    gl_Position   = camera.view_proj * world_pos;
}

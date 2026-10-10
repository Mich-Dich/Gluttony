
#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
} camera;

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
    const vec3 light_dir   = normalize(vec3(0.4, 0.8, 0.5));
    const vec3 light_color = vec3(1.0, 0.95, 0.85);

    vec3 N = normalize(in_normal);
    float ndotl = max(dot(N, light_dir), 0.0);

    vec3 albedo = vec3(0.75, 0.72, 0.68);
    vec3 color  = albedo * 0.2 + albedo * ndotl * light_color;
    out_color   = vec4(color, 1.0);
}

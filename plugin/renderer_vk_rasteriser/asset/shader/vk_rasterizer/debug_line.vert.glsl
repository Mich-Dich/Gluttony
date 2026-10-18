
#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

// Matches the prefix of GLT::renderer::vk_rasterizer::camera_ubo_data
layout(set = 0, binding = 0) uniform Camera { mat4 view_proj; } camera;



void main() {

    gl_Position = camera.view_proj * vec4(in_position, 1.0);
    out_color = in_color;
}

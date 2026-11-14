#version 450

// Virtual geometry debug views (see virtual_geometry_debug.vert.glsl): one flat colour per draw, shaded by how much
// the triangle faces the camera so the shape stays readable. The normal comes from the triangle itself, so every
// triangle is flat: what is drawn is what is there

layout(set = 0, binding = 0) uniform Camera {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
} camera;

layout(location = 0) in vec3 in_world;
layout(location = 1) flat in vec3 in_color;
layout(location = 2) flat in uint in_seed;

layout(location = 0) out vec4 out_color;

void main() {

    const vec3  normal = cross(dFdx(in_world), dFdy(in_world));
    const float area = length(normal);
    const vec3  to_camera = camera.camera_pos.xyz - in_world;
    const float dist = length(to_camera);
    const float facing = (area > 0.0 && dist > 0.0) ? abs(dot(normal / area, to_camera / dist)) : 1.0;

    out_color = vec4(in_color * (0.40 + 0.60 * facing), 1.0);
}

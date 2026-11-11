#version 450

// Render mode "Triangle Density", count pass. The mesh index buffers are drawn as POINT lists: every triangle corner
// becomes one 1-pixel point at its vertex, so each triangle adds 3 points = 3 x (1/3) to the pixel(s) it touches.
// Summed (additive R16F) and blurred, that is triangles per pixel, independent of how large the triangles are

layout(location = 0) in vec3 in_position;

layout(set = 0, binding = 0) uniform Camera {
    mat4  view_proj;
    mat4  view;
} camera;

struct InstanceData {
    mat4 model;
    mat4 normal_matrix;
    uint mesh_index;
    uint material_index;
    uint flags;
    uint _pad1;
};
layout(std430, set = 0, binding = 1) readonly buffer Instances { InstanceData instances[]; };

layout(location = 0) out float out_view_depth;

void main() {

    // gl_InstanceIndex includes firstInstance = instance index | LOD info (cull.comp.glsl)
    const InstanceData inst = instances[uint(gl_InstanceIndex) & 0xFFFFFu];
    const vec4 world = inst.model * vec4(in_position, 1.0);
    gl_Position = camera.view_proj * world;
    gl_PointSize = 1.0;
    out_view_depth = -(camera.view * world).z;
}

#version 450

// Virtual geometry debug view "Triangles": like virtual_geometry_debug.frag.glsl, but every triangle gets its own
// colour. gl_PrimitiveID restarts for each indirect command, so it is mixed with the per-draw seed of the vertex shader
// (instance + cluster). Own file because gl_PrimitiveID in a fragment shader needs the geometryShader device feature:
// without it the renderer uses the plain fragment shader for this view (cluster colours)

layout(set = 0, binding = 0) uniform Camera {
    mat4 view_proj;
    mat4 view;
    vec4 camera_pos;
} camera;

layout(location = 0) in vec3 in_world;
layout(location = 1) flat in vec3 in_color;
layout(location = 2) flat in uint in_seed;

layout(location = 0) out vec4 out_color;

uint hash(uint x) {
    x ^= x >> 16u; x *= 0x7feb352du;
    x ^= x >> 15u; x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

vec3 hsv(float h, float s, float v) {
    const vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k, s);
}

void main() {

    const vec3  normal = cross(dFdx(in_world), dFdy(in_world));
    const float area = length(normal);
    const vec3  to_camera = camera.camera_pos.xyz - in_world;
    const float dist = length(to_camera);
    const float facing = (area > 0.0 && dist > 0.0) ? abs(dot(normal / area, to_camera / dist)) : 1.0;

    const uint h = hash(in_seed ^ (uint(gl_PrimitiveID) * 0x9E3779B1u));
    const vec3 color = hsv(float(h & 0xFFFFu) / 65536.0, 0.50 + 0.35 * float((h >> 16u) & 0xFFu) / 255.0, 0.70 + 0.30 * float(h >> 24u) / 255.0);

    out_color = vec4(color * (0.45 + 0.55 * facing), 1.0);
}

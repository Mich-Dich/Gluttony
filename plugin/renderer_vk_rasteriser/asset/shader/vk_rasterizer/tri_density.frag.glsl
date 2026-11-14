#version 450

// Count pass of "Triangle Density": 1/3 per triangle corner, only for corners on (or in front of) the visible surface.
// The comparison is done in linear depth with a tolerance because a vertex rarely lies exactly at the pixel centre's depth

layout(set = 0, binding = 0) uniform Camera {
    mat4  view_proj;
    mat4  view;
    vec4  camera_pos;
    vec4  frustum_planes[6];
    uvec4 flags;
    mat4  projection;
    mat4  inv_projection;
} camera;

layout(set = 0, binding = 2) uniform sampler2D depth_tex;

layout(location = 0) in float in_view_depth;
layout(location = 0) out float out_count;

float linearize(float d) {
    const vec4 v = camera.inv_projection * vec4(0.0, 0.0, d, 1.0);
    return -v.z / v.w;
}

void main() {

    const float surface = texelFetch(depth_tex, ivec2(gl_FragCoord.xy), 0).r;
    if (surface < 1.0 && in_view_depth > linearize(surface) * 1.02 + 0.02)
        discard;                                        // hidden behind the visible surface
    out_count = 1.0 / 3.0;
}

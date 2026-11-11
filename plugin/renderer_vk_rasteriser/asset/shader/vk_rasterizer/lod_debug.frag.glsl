#version 450

// "Level of Detail" render mode: flat colour per LOD. A small facing term (not lighting) keeps the silhouette readable

layout(location = 0) flat in uint in_lod;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_view_dir;

layout(location = 0) out vec4 out_color;

const vec3 LOD_COLORS[8] = vec3[8](
    vec3(0.20, 0.80, 0.25),     // 0 green
    vec3(0.95, 0.85, 0.15),     // 1 yellow
    vec3(0.98, 0.52, 0.12),     // 2 orange
    vec3(0.90, 0.18, 0.15),     // 3 red
    vec3(0.88, 0.22, 0.75),     // 4 magenta
    vec3(0.55, 0.30, 0.92),     // 5 purple
    vec3(0.22, 0.42, 0.95),     // 6 blue
    vec3(0.20, 0.82, 0.90)      // 7 cyan
);


void main() {

    vec3 color;
    if ((in_lod & 0x200u) != 0u)
        color = vec3(0.92);                         // procedural
    else if ((in_lod & 0x100u) != 0u)
        color = vec3(0.50);                         // mesh without LOD levels
    else
        color = LOD_COLORS[min(in_lod & 0xFFu, 7u)];

    const float l2n = dot(in_normal, in_normal);
    const float l2v = dot(in_view_dir, in_view_dir);
    const float facing = (l2n > 1e-12 && l2v > 1e-12) ? abs(dot(in_normal * inversesqrt(l2n), in_view_dir * inversesqrt(l2v))) : 1.0;
    out_color = vec4(color * (0.72 + 0.28 * facing), 1.0);
}

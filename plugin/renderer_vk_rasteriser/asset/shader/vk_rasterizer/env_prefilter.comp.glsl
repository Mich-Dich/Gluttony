#version 460

// GGX-prefiltered environment for specular IBL (split-sum, N = V = R assumption). One dispatch per mip; roughness grows
// linearly with the mip index. The source sky contains no sun disk, so it is smooth and a modest sample count is enough

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform samplerCube source_sky;
layout(set = 0, binding = 1, rgba16f) uniform writeonly image2DArray out_mip;

layout(push_constant) uniform PushConstants {
    float roughness;
    uint  size;             // texels per face edge of this mip
    uint  sample_count;
    uint  _pad;
} pc;

const float PI = 3.14159265359;


vec3 cube_dir(uint face, vec2 uv) {
    switch (face) {
        case 0u: return normalize(vec3( 1.0, -uv.y, -uv.x));
        case 1u: return normalize(vec3(-1.0, -uv.y,  uv.x));
        case 2u: return normalize(vec3( uv.x,  1.0,  uv.y));
        case 3u: return normalize(vec3( uv.x, -1.0, -uv.y));
        case 4u: return normalize(vec3( uv.x, -uv.y,  1.0));
        default: return normalize(vec3(-uv.x, -uv.y, -1.0));
    }
}


vec2 hammersley(uint i, uint n) {
    uint bits = i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return vec2(float(i) / float(n), float(bits) * 2.3283064365386963e-10);
}


vec3 importance_sample_ggx(vec2 xi, vec3 n, float a) {
    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
    vec3 h = vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);
    return normalize(tangent * h.x + bitangent * h.y + n * h.z);
}


void main() {

    const uvec3 id = gl_GlobalInvocationID;
    if (id.x >= pc.size || id.y >= pc.size)
        return;

    const vec2 uv = (vec2(id.xy) + 0.5) / float(pc.size) * 2.0 - 1.0;
    const vec3 n = cube_dir(id.z, uv);

    if (pc.roughness < 0.01) {
        imageStore(out_mip, ivec3(id), vec4(textureLod(source_sky, n, 0.0).rgb, 1.0));
        return;
    }

    const float a = pc.roughness * pc.roughness;
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < pc.sample_count; ++i) {
        vec3 h = importance_sample_ggx(hammersley(i, pc.sample_count), n, a);
        vec3 l = normalize(2.0 * dot(n, h) * h - n);
        float n_dot_l = dot(n, l);
        if (n_dot_l > 0.0) {
            sum += textureLod(source_sky, l, 0.0).rgb * n_dot_l;
            weight += n_dot_l;
        }
    }
    imageStore(out_mip, ivec3(id), vec4(sum / max(weight, 1e-4), 1.0));
}

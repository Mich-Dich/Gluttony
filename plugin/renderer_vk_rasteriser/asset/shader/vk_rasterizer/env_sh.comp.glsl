#version 460

// Projects the sky cubemap onto 9 spherical-harmonics coefficients (L2) and bakes the clamped-cosine convolution
// (Ramamoorthi & Hanrahan) divided by PI into them. Evaluating the result at a normal N gives irradiance(N) / PI, so a
// Lambert surface's ambient diffuse is simply albedo * eval(N) (matches the engine's PI-scaled light convention).
// One workgroup; samples a SAMPLES x SAMPLES grid per face with texel solid-angle weights

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform samplerCube source_sky;
layout(std430, set = 0, binding = 1) writeonly buffer ShBuffer { vec4 coefficients[9]; } sh_out;

const uint SAMPLES = 32u;
const uint TOTAL = 6u * SAMPLES * SAMPLES;
const float PI = 3.14159265359;

shared vec3 s_coeffs[64][9];
shared float s_weight[64];


vec3 cube_dir(uint face, vec2 uv) {
    switch (face) {
        case 0u: return vec3( 1.0, -uv.y, -uv.x);
        case 1u: return vec3(-1.0, -uv.y,  uv.x);
        case 2u: return vec3( uv.x,  1.0,  uv.y);
        case 3u: return vec3( uv.x, -1.0, -uv.y);
        case 4u: return vec3( uv.x, -uv.y,  1.0);
        default: return vec3(-uv.x, -uv.y, -1.0);
    }
}


void main() {

    const uint tid = gl_LocalInvocationID.x;
    vec3 c[9];
    for (uint k = 0u; k < 9u; ++k) c[k] = vec3(0.0);
    float weight_sum = 0.0;

    for (uint i = tid; i < TOTAL; i += 64u) {
        const uint face = i / (SAMPLES * SAMPLES);
        const uint texel = i % (SAMPLES * SAMPLES);
        const vec2 uv = (vec2(texel % SAMPLES, texel / SAMPLES) + 0.5) / float(SAMPLES) * 2.0 - 1.0;
        const vec3 unnormalized = cube_dir(face, uv);
        const float len2 = dot(unnormalized, unnormalized);           // 1 + u^2 + v^2
        const float d_omega = 4.0 / (float(SAMPLES * SAMPLES) * len2 * sqrt(len2));
        const vec3 d = unnormalized * inversesqrt(len2);
        const vec3 L = textureLod(source_sky, d, 0.0).rgb * d_omega;

        c[0] += L * 0.282095;
        c[1] += L * 0.488603 * d.y;
        c[2] += L * 0.488603 * d.z;
        c[3] += L * 0.488603 * d.x;
        c[4] += L * 1.092548 * d.x * d.y;
        c[5] += L * 1.092548 * d.y * d.z;
        c[6] += L * 0.315392 * (3.0 * d.z * d.z - 1.0);
        c[7] += L * 1.092548 * d.x * d.z;
        c[8] += L * 0.546274 * (d.x * d.x - d.y * d.y);
        weight_sum += d_omega;
    }

    for (uint k = 0u; k < 9u; ++k) s_coeffs[tid][k] = c[k];
    s_weight[tid] = weight_sum;
    barrier();

    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (tid < stride) {
            for (uint k = 0u; k < 9u; ++k) s_coeffs[tid][k] += s_coeffs[tid + stride][k];
            s_weight[tid] += s_weight[tid + stride];
        }
        barrier();
    }

    if (tid == 0u) {
        // Normalise the discrete solid angles to exactly 4 PI, then convolve with the clamped cosine (A_l) / PI
        const float norm = 4.0 * PI / max(s_weight[0], 1e-6);
        const float band[9] = float[9](1.0, 2.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0, 0.25, 0.25, 0.25, 0.25, 0.25);
        for (uint k = 0u; k < 9u; ++k)
            sh_out.coefficients[k] = vec4(s_coeffs[0][k] * norm * band[k], 0.0);
    }
}

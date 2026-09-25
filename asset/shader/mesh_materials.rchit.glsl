#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec3 payload;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 0, binding = 1) uniform CameraUBO {
    mat4 view_inv;
    mat4 proj_inv;
    vec4 sun_direction;   // xyz = direction TO the sun
    vec4 sun_color;       // xyz = color, w = intensity
} cam;

struct Material {
    vec4  base_color;
    float roughness;
    float metallic;
    uint  vertex_base;
    uint  index_base;
};

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
    float pad0, pad1;
};

layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer { Material materials[]; };
layout(set = 0, binding = 4, std430) readonly buffer VertexBuffer   { Vertex   vertices[];  };
layout(set = 0, binding = 5, std430) readonly buffer IndexBuffer    { uint     indices[];   };

// ---------------------------------------------------------------------------------------
// AO configuration
// ---------------------------------------------------------------------------------------
const int   AO_SAMPLES    = 8;
const float AO_RADIUS     = 30.0;     // world-space occlusion radius
const float AO_RAY_BIAS   = 0.005;    // push origin off the surface

// ---------------------------------------------------------------------------------------
// Tiny hash-based RNG — good enough for AO. Not cryptographic, not high quality,
// but cheap and deterministic per-pixel.
// ---------------------------------------------------------------------------------------
uint pcg_hash(uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand_float(inout uint seed) {
    seed = pcg_hash(seed);
    return float(seed) * (1.0 / 4294967296.0);   // [0, 1)
}

// Cosine-weighted direction on the hemisphere around n.
vec3 cosine_hemisphere(inout uint seed, vec3 n) {
    const float r1  = rand_float(seed);
    const float r2  = rand_float(seed);
    const float phi = 6.28318530718 * r1;
    const float r   = sqrt(r2);
    const float x   = r * cos(phi);
    const float y   = r * sin(phi);
    const float z   = sqrt(max(0.0, 1.0 - r2));

    // Orthonormal basis around n (branchless "up" selection)
    const vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    const vec3 t  = normalize(cross(up, n));
    const vec3 b  = cross(n, t);

    return t * x + b * y + n * z;
}

void main() {
    // --- material + triangle lookup -----------------------------------------------------
    const uint mat_idx = gl_InstanceCustomIndexEXT + gl_GeometryIndexEXT;
    const Material mat = materials[mat_idx];

    const uint prim = gl_PrimitiveID;
    const uvec3 idx = uvec3(
        indices[mat.index_base + prim * 3 + 0],
        indices[mat.index_base + prim * 3 + 1],
        indices[mat.index_base + prim * 3 + 2]);

    const Vertex v0 = vertices[mat.vertex_base + idx.x];
    const Vertex v1 = vertices[mat.vertex_base + idx.y];
    const Vertex v2 = vertices[mat.vertex_base + idx.z];

    // Barycentric interpolation (attribs.x = u for v1, attribs.y = v for v2 in Vulkan)
    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    const vec3 n_local =
        bary.x * vec3(v0.nx, v0.ny, v0.nz) +
        bary.y * vec3(v1.nx, v1.ny, v1.nz) +
        bary.z * vec3(v2.nx, v2.ny, v2.nz);

    // For now our transforms are identity; if you later add per-instance rotation/scale,
    // normal-matrix-transform this: normalize(mat3(transpose(inverse(gl_ObjectToWorldEXT))) * n_local)
    const vec3 N = normalize(n_local);

    // --- world-space hit point ----------------------------------------------------------
    const vec3 hit_world =
        gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // --- AO -----------------------------------------------------------------------------
    // Per-pixel, deterministic seed. Same value every frame → static noise pattern.
    uint seed = uint(gl_LaunchIDEXT.x) * 1973u
              ^ uint(gl_LaunchIDEXT.y) * 9277u
              ^ 26699u;

    float occlusion = 0.0;
    for (int i = 0; i < AO_SAMPLES; ++i) {

        const vec3 dir = cosine_hemisphere(seed, N);
        const vec3 org = hit_world + N * AO_RAY_BIAS;

        // Reset payload to "unoccluded" before the trace; AO miss/hit overwrite it.
        payload = vec3(0.0);

        traceRayEXT(
            topLevelAS,
            gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
            0xFF,
            1u,                    // sbtRecordOffset  → AO hit group
            0u,                    // sbtRecordStride  → offset alone selects the hit group (do NOT walk by geometry)
            1u,                    // missIndex        → AO miss shader
            org,
            0.001,
            dir,
            AO_RADIUS,
            0);

        occlusion += payload.x;
    }

    const float ao = 1.0 - (occlusion / float(AO_SAMPLES));

    // --- direct lighting (Lambert against the sun) --------------------------------------
    const vec3  L     = normalize(cam.sun_direction.xyz);
    const float NdotL = max(dot(N, L), 0.0);
    const vec3 sun_rgb = cam.sun_color.rgb * cam.sun_color.w;
    const vec3 ambient = mat.base_color.rgb * 0.15 * ao;   // AO modulates the ambient term
    const vec3 diffuse = mat.base_color.rgb * sun_rgb * NdotL;

    payload = ambient + diffuse;
}

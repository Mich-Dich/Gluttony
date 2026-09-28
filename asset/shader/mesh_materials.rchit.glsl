#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec3 payload;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 0, binding = 1) uniform CameraUBO {
    mat4 view_inv;
    mat4 proj_inv;
    vec4 sun_direction;   // xyz = direction TO the sun
    vec4 sun_color;       // xyz = color, w = intensity
} cam;

// ---------------------------------------------------------------------------------------
// GPU structs - MUST match the C++ layouts in renderer.inl
// ---------------------------------------------------------------------------------------
struct GpuMaterial {
    vec4  base_color;         // 16
    vec3  emissive;           // 12
    float roughness;          //  4
    float metallic;           //  4
    float reflectance;        //  4
    float normal_scale;       //  4
    float occlusion_strength; //  4
    uint  textures[6];        // 24   bindless indices, 0 == checkerboard fallback
    uint  flags;              //  4
    uint  _pad;               //  4   -> sizeof == 80
};

struct GpuGeometry {
    uint vertex_base;
    uint index_base;
    uint material_index;
    uint _pad;
};

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
    float pad0, pad1;
};

layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer { GpuMaterial materials[]; };
layout(set = 0, binding = 4, std430) readonly buffer VertexBuffer   { Vertex   vertices[];  };
layout(set = 0, binding = 5, std430) readonly buffer IndexBuffer    { uint     indices[];   };
layout(set = 0, binding = 6)          uniform sampler2D              textures[1024];
layout(set = 0, binding = 7, std430) readonly buffer GeometryBuffer { GpuGeometry geometries[]; };

// ---------------------------------------------------------------------------------------
// Texture slot indices - MUST match GLT::asset::material::texture_slot
// ---------------------------------------------------------------------------------------
const uint SLOT_BASE_COLOR         = 0u;
const uint SLOT_METALLIC_ROUGHNESS = 1u;
const uint SLOT_NORMAL             = 2u;
const uint SLOT_EMISSIVE           = 3u;
const uint SLOT_OCCLUSION          = 4u;
const uint SLOT_HEIGHT             = 5u;   // reserved, unused

// ---------------------------------------------------------------------------------------
// AO configuration
// ---------------------------------------------------------------------------------------
const int   AO_SAMPLES  = 8;
const float AO_RADIUS   = 30.0;     // world-space occlusion radius
const float AO_RAY_BIAS = 0.005;    // push origin off the surface

// ---------------------------------------------------------------------------------------
// Tiny hash-based RNG - good enough for AO.
// ---------------------------------------------------------------------------------------
uint pcg_hash(uint state) {
    state = state * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand_float(inout uint seed) {
    seed = pcg_hash(seed);
    return float(seed) * (1.0 / 4294967296.0);
}

vec3 cosine_hemisphere(inout uint seed, vec3 n) {
    const float r1  = rand_float(seed);
    const float r2  = rand_float(seed);
    const float phi = 6.28318530718 * r1;
    const float r   = sqrt(r2);
    const float x   = r * cos(phi);
    const float y   = r * sin(phi);
    const float z   = sqrt(max(0.0, 1.0 - r2));

    const vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    const vec3 t  = normalize(cross(up, n));
    const vec3 b  = cross(n, t);

    return t * x + b * y + n * z;
}

void main() {
    // --- per-instance geometry record ----------------------------------------------------
    // gl_InstanceCustomIndexEXT is the base offset into the geometry buffer for this
    // instance; gl_GeometryIndexEXT walks the submeshes the BLAS laid out in the same order.
    const GpuGeometry geom = geometries[gl_InstanceCustomIndexEXT + gl_GeometryIndexEXT];
    const GpuMaterial mat  = materials[geom.material_index];

    // --- triangle fetch ------------------------------------------------------------------
    const uint prim = gl_PrimitiveID;
    const uvec3 idx = uvec3(
        indices[geom.index_base + prim * 3u + 0u],
        indices[geom.index_base + prim * 3u + 1u],
        indices[geom.index_base + prim * 3u + 2u]);

    const Vertex v0 = vertices[geom.vertex_base + idx.x];
    const Vertex v1 = vertices[geom.vertex_base + idx.y];
    const Vertex v2 = vertices[geom.vertex_base + idx.z];

    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    // --- interpolated attributes ---------------------------------------------------------
    const vec3 n_local = bary.x * vec3(v0.nx, v0.ny, v0.nz)
                       + bary.y * vec3(v1.nx, v1.ny, v1.nz)
                       + bary.z * vec3(v2.nx, v2.ny, v2.nz);

    const vec3 t_local = bary.x * vec3(v0.tx, v0.ty, v0.tz)
                       + bary.y * vec3(v1.tx, v1.ty, v1.tz)
                       + bary.z * vec3(v2.tx, v2.ty, v2.tz);

    const float bitangent_sign = bary.x * v0.tw + bary.y * v1.tw + bary.z * v2.tw;

    const vec2 uv = bary.x * vec2(v0.u, v0.v)
                  + bary.y * vec2(v1.u, v1.v)
                  + bary.z * vec2(v2.u, v2.v);

    // --- world-space normal --------------------------------------------------------------
    const mat3 normal_matrix = transpose(mat3(gl_WorldToObjectEXT));
    vec3 N = normalize(normal_matrix * n_local);

    // --- material sampling ---------------------------------------------------------------
    // Base color: sampled unconditionally. When the material has no base-color texture,
    // mat.textures[SLOT_BASE_COLOR] == 0 and the checkerboard fallback shows through,
    // making an untextured material visually obvious.
    const vec4 base_tex = texture(textures[nonuniformEXT(mat.textures[SLOT_BASE_COLOR])], uv);
    const vec3 albedo   = mat.base_color.rgb * base_tex.rgb;

    // Metallic-roughness: only applied when a texture is bound. The checkerboard
    // fallback is not an identity multiply, so an untextured material must use its
    // scalar parameters directly.
    float roughness = mat.roughness;
    float metallic  = mat.metallic;
    if (mat.textures[SLOT_METALLIC_ROUGHNESS] != 0u) {
        const vec2 mr = texture(textures[nonuniformEXT(mat.textures[SLOT_METALLIC_ROUGHNESS])], uv).gb;
        roughness *= mr.x;
        metallic  *= mr.y;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic,  0.0,  1.0);

    // Occlusion: same reasoning. No texture -> full occlusion (1.0).
    float occ_mat = 1.0;
    if (mat.textures[SLOT_OCCLUSION] != 0u) {
        const float occ_tex = texture(textures[nonuniformEXT(mat.textures[SLOT_OCCLUSION])], uv).r;
        occ_mat = mix(1.0, occ_tex, mat.occlusion_strength);
    }

    // Emissive: same reasoning. No texture -> scalar emissive stands alone.
    vec3 emissive = mat.emissive;
    if (mat.textures[SLOT_EMISSIVE] != 0u)
        emissive *= texture(textures[nonuniformEXT(mat.textures[SLOT_EMISSIVE])], uv).rgb;

    // --- normal mapping ------------------------------------------------------------------
    if (mat.textures[SLOT_NORMAL] != 0u) {
        vec3 n_tangent = texture(textures[nonuniformEXT(mat.textures[SLOT_NORMAL])], uv).xyz * 2.0 - 1.0;
        n_tangent.xy *= mat.normal_scale;

        // Gram-Schmidt orthogonalize T against N, then rebuild B
        vec3 T_world = normalize(normal_matrix * t_local);
        T_world = normalize(T_world - N * dot(N, T_world));
        const vec3 B_world = cross(N, T_world) * bitangent_sign;

        const mat3 TBN = mat3(T_world, B_world, N);
        N = normalize(TBN * n_tangent);
    }

    // --- world-space hit point -----------------------------------------------------------
    const vec3 hit_world = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // --- AO ------------------------------------------------------------------------------
    uint seed = uint(gl_LaunchIDEXT.x) * 1973u
              ^ uint(gl_LaunchIDEXT.y) * 9277u
              ^ 26699u;

    float occlusion = 0.0;
    for (int i = 0; i < AO_SAMPLES; ++i) {
        const vec3 dir = cosine_hemisphere(seed, N);
        const vec3 org = hit_world + N * AO_RAY_BIAS;

        payload = vec3(0.0);
        traceRayEXT(
            topLevelAS,
            gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
            0xFF,
            1u,    // sbtRecordOffset  -> AO hit group
            0u,    // sbtRecordStride  -> offset alone selects the hit group
            1u,    // missIndex        -> AO miss shader
            org, 0.001, dir, AO_RADIUS, 0);
        occlusion += payload.x;
    }
    const float ao = (1.0 - occlusion / float(AO_SAMPLES)) * occ_mat;

    // --- direct lighting (Lambert diffuse + cheap Blinn-Phong specular) ------------------
    const vec3  L      = normalize(cam.sun_direction.xyz);
    const float NdotL  = max(dot(N, L), 0.0);

    const vec3  V      = normalize(-gl_WorldRayDirectionEXT);
    const vec3  H      = normalize(L + V);
    const float NdotH  = max(dot(N, H), 0.0);
    const float VdotH  = max(dot(V, H), 0.0);

    const vec3 sun_rgb = cam.sun_color.rgb * cam.sun_color.w;

    // Fresnel-Schlick with F0 pulled toward albedo by metallic
    const vec3 F0 = mix(vec3(0.04), albedo, metallic);
    const vec3 F  = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    // Roughness-controlled lobe. Not GGX, but cheap and monotonic in roughness.
    const float shininess = mix(4.0, 256.0, 1.0 - roughness);
    const float spec_term = pow(NdotH, shininess) * (1.0 - roughness) * mat.reflectance;

    const vec3 diffuse  = albedo * (1.0 - metallic) * sun_rgb * NdotL;
    const vec3 specular = F * spec_term * sun_rgb * NdotL;

    // AO modulates only the ambient term
    const vec3 ambient  = albedo * 0.15 * ao * (1.0 - metallic);

    payload = ambient + diffuse + specular + emissive;
}

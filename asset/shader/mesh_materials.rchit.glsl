#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 0, binding = 1) uniform CameraUBO {
    mat4  view_inv;
    mat4  proj_inv;
    vec4  sun_direction;   // xyz = direction TO the sun
    vec4  sun_color;       // xyz = color, w = intensity
    uvec4 accum_params;    // unused here; must exist for the block to match the rgen
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
const uint SLOT_BASE_COLOR = 0u;
const uint SLOT_METALLIC_ROUGHNESS = 1u;
const uint SLOT_NORMAL = 2u;
const uint SLOT_EMISSIVE = 3u;
const uint SLOT_OCCLUSION = 4u;
const uint SLOT_HEIGHT = 5u;   // reserved, unused

// ---------------------------------------------------------------------------------------
// AO configuration
// ---------------------------------------------------------------------------------------
const int   AO_SAMPLES  = 8;
const float AO_RADIUS   = 30.0;     // world-space occlusion radius
const float AO_RAY_BIAS = 0.005;    // push origin off the surface

// ---------------------------------------------------------------------------------------
// Soft sun shadow configuration
// ---------------------------------------------------------------------------------------
const float SUN_ANGULAR_RADIUS = 0.035;    // tan(half-angle); ~2 deg
const int   SUN_SAMPLES        = 8;        // rays per pixel for the shadow test
const float SHADOW_RAY_TMAX    = 10000.0;  // effectively "to infinity" for the sun

// ---------------------------------------------------------------------------------------
// Indirect bounce configuration
// ---------------------------------------------------------------------------------------
// MAX_BOUNCES is the deepest shading level reached. A value of 3 means the primary
// hit plus three levels of indirect bounce (bounce 0, 1, 2, 3). The deepest chit
// does not trace further, so its own shadow test is the only nested ray it issues.
//
// If you change this, also update max_recursion_depth in renderer.inl. The needed
// depth is 1 (rgen) + MAX_BOUNCES + 1 (a chit) + 1 (nested shadow from the deepest
// chit) = MAX_BOUNCES + 3.
const int MAX_BOUNCES = 3;

// ---------------------------------------------------------------------------------------
// Tiny hash-based RNG - good enough for AO, shadows and one-ray GI.
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
    // --- current bounce depth ------------------------------------------------------------
    // This is the value the caller set before tracing into us. Bounce 0 = primary ray.
    const int my_bounce = int(payload.w);

    // --- per-instance geometry record ----------------------------------------------------
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
    const vec4 base_tex = texture(textures[nonuniformEXT(mat.textures[SLOT_BASE_COLOR])], uv);
    const vec3 albedo   = mat.base_color.rgb * base_tex.rgb;

    float roughness = mat.roughness;
    float metallic  = mat.metallic;
    if (mat.textures[SLOT_METALLIC_ROUGHNESS] != 0u) {
        const vec2 mr = texture(textures[nonuniformEXT(mat.textures[SLOT_METALLIC_ROUGHNESS])], uv).gb;
        roughness *= mr.x;
        metallic  *= mr.y;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic,  0.0,  1.0);

    float occ_mat = 1.0;
    if (mat.textures[SLOT_OCCLUSION] != 0u) {
        const float occ_tex = texture(textures[nonuniformEXT(mat.textures[SLOT_OCCLUSION])], uv).r;
        occ_mat = mix(1.0, occ_tex, mat.occlusion_strength);
    }

    vec3 emissive = mat.emissive;
    if (mat.textures[SLOT_EMISSIVE] != 0u)
        emissive *= texture(textures[nonuniformEXT(mat.textures[SLOT_EMISSIVE])], uv).rgb;

    // --- normal mapping ------------------------------------------------------------------
    if (mat.textures[SLOT_NORMAL] != 0u) {
        vec3 n_tangent = texture(textures[nonuniformEXT(mat.textures[SLOT_NORMAL])], uv).xyz * 2.0 - 1.0;
        n_tangent.xy *= mat.normal_scale;

        vec3 T_world = normalize(normal_matrix * t_local);
        T_world = normalize(T_world - N * dot(N, T_world));
        const vec3 B_world = cross(N, T_world) * bitangent_sign;

        const mat3 TBN = mat3(T_world, B_world, N);
        N = normalize(TBN * n_tangent);
    }

    // --- world-space hit point -----------------------------------------------------------
    const vec3 hit_world = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // --- RNG seed ------------------------------------------------------------------------
    // Vary per-pixel, per-bounce, AND per-frame. The frame term is what makes temporal accumulation work: without it,
    // every frame produces bit-identical noise and the running average converges to a single noisy sample instead of the true mean
    //
    // cam.accum_params.x is the number of samples already accumulated in the accum buffer, i.e. the frame index within the
    // current accumulation run. It increments by one each frame while the camera is still, and resets to 0 when it isn't, which
    // is exactly the decorrelation we want.
    uint seed = uint(gl_LaunchIDEXT.x)     * 1973u
              ^ uint(gl_LaunchIDEXT.y)     * 9277u
              ^ uint(my_bounce)            * 31337u
              ^ cam.accum_params.x         * 71923u
              ^ 26699u;

    // --- AO (bounce 0 only) --------------------------------------------------------------
    // AO is a stand-in for sky visibility. Running it at every bounce would compound the estimate and darken interiors far
    // too much, so it's gated to the primary hit
    float ao_geometric = 1.0;
    if (my_bounce == 0) {
        float occlusion = 0.0;
        for (int i = 0; i < AO_SAMPLES; ++i) {
            const vec3 dir = cosine_hemisphere(seed, N);
            const vec3 org = hit_world + N * AO_RAY_BIAS;

            payload = vec4(0.0, 0.0, 0.0, float(my_bounce));    // occluded-test scratch
            traceRayEXT(
                topLevelAS,
                gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                0xFF,
                1u, 0u, 1u,                                     // AO hit group + AO miss
                org, 0.001, dir, AO_RADIUS, 0);
            occlusion += payload.x;
        }
        ao_geometric = 1.0 - occlusion / float(AO_SAMPLES);
    }
    const float ao = ao_geometric * occ_mat;

    // --- soft sun shadow (every bounce) --------------------------------------------------
    // A surface in sun shadow still reflects ambient and sky. If we skipped the shadow test at bounces > 0, indirect light would
    // wrap around corners and leak, so we pay one extra ray per bounce
    const vec3  L         = normalize(cam.sun_direction.xyz);
    const float NdotL_raw = dot(N, L);
    float shadow          = 0.0;

    if (NdotL_raw > 0.0) {

        const vec3 up = abs(L.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        const vec3 t  = normalize(cross(up, L));
        const vec3 b  = cross(L, t);

        const vec3 org = hit_world + N * AO_RAY_BIAS;

        float lit = 0.0;
        for (int i = 0; i < SUN_SAMPLES; ++i) {

            const float r   = SUN_ANGULAR_RADIUS * sqrt(rand_float(seed));
            const float phi = 6.28318530718 * rand_float(seed);
            const vec3  dir = normalize(L + t * (r * cos(phi)) + b * (r * sin(phi)));

            payload = vec4(0.0, 0.0, 0.0, float(my_bounce));    // occluded-test scratch
            traceRayEXT(
                topLevelAS,
                gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                0xFF,
                1u, 0u, 1u,                                     // AO hit group + AO miss
                org, 0.001, dir, SHADOW_RAY_TMAX, 0);

            lit += (payload.x < 0.5) ? 1.0 : 0.0;
        }
        shadow = 1.0 - lit / float(SUN_SAMPLES);
    }

    // --- direct lighting -----------------------------------------------------------------
    const float NdotL = max(NdotL_raw, 0.0) * (1.0 - shadow);

    const vec3  V     = normalize(-gl_WorldRayDirectionEXT);
    const vec3  H     = normalize(L + V);
    const float NdotH = max(dot(N, H), 0.0);
    const float VdotH = max(dot(V, H), 0.0);

    const vec3 sun_rgb = cam.sun_color.rgb * cam.sun_color.w;

    const vec3 F0 = mix(vec3(0.04), albedo, metallic);
    const vec3 F  = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    const float shininess = mix(4.0, 256.0, 1.0 - roughness);
    const float spec_term = pow(NdotH, shininess) * (1.0 - roughness) * mat.reflectance;

    const vec3 diffuse  = albedo * (1.0 - metallic) * sun_rgb * NdotL;
    const vec3 specular = F * spec_term * sun_rgb * NdotL;
    const vec3 ambient  = albedo * 0.15 * ao * (1.0 - metallic);

    vec3 result = ambient + diffuse + specular + emissive;

    // --- indirect bounce (emissive + one-bounce GI) --------------------------------------
    // One cosine-weighted ray per hit. The path carries no payload state beyond the bounce counter; on return, its radiance is
    // what we received from that direction.
    //
    // For a Lambertian BRDF with cosine-weighted sampling, the rendering equation
    //     L_o = albedo/pi * Integral( L_i * cos(theta) domega )
    // reduces to just
    //     L_o ~= albedo * L_i
    // because the pdf cos(theta)/pi cancels the cos(theta) and the pi. So the only weighting we apply is the current surface's albedo
    //
    // The sky is a valid hit for this ray - the rmiss shader returns the sky colour, which means upward-facing surfaces pick up sky
    // light naturally. If the scene becomes too bright overall, dial down the 0.15 ambient term above; it's a heuristic that predates
    // the sky bounce
    if (my_bounce < MAX_BOUNCES) {

        const vec3 bounce_dir = cosine_hemisphere(seed, N);
        const vec3 bounce_org = hit_world + N * AO_RAY_BIAS;

        // Reset the payload for the nested trace and bump the bounce counter.
        payload = vec4(0.0, 0.0, 0.0, float(my_bounce + 1));

        traceRayEXT(
            topLevelAS,
            gl_RayFlagsOpaqueEXT,       // no terminate-on-first-hit; we want the closest shader to run fully
            0xFF,
            0u, 0u, 0u,                 // primary hit group + primary miss
            bounce_org, 0.001, bounce_dir, SHADOW_RAY_TMAX, 0);

        result += albedo * payload.xyz;
    }

    payload = vec4(result, float(my_bounce));
}

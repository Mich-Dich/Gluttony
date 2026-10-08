
#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 attribs;

layout(set = 0, binding = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 0, binding = 1) uniform CameraUBO {
    mat4            view_inv;
    mat4            proj_inv;
    vec4            sun_direction;   // xyz = direction TO the sun
    vec4            sun_color;       // xyz = color, w = intensity
    mat4            prev_view_proj;  // previous frame's (proj * view); unused here
    uvec4           temporal;        // x=reset, y=max_history, z=write_idx, w=unused
} cam;

// ---------------------------------------------------------------------------------------
// GPU structs - MUST match the C++ layouts in renderer.inl
// ---------------------------------------------------------------------------------------
struct GpuMaterial {
    vec4            base_color;         // 16
    vec3            emissive;           // 12
    float           roughness;          //  4
    float           metallic;           //  4
    float           reflectance;        //  4
    float           normal_scale;       //  4
    float           occlusion_strength; //  4
    uint            textures[6];        // 24   bindless indices, 0 == checkerboard fallback
    uint            flags;              //  4
    uint            _pad;               //  4   -> sizeof == 80
};

struct GpuGeometry {
    uint            vertex_base;
    uint            index_base;
    uint            material_index;
    uint            _pad;
};

struct Vertex {
    float           px, py, pz;
    float           nx, ny, nz;
    float           tx, ty, tz, tw;
    float           u, v;
    float           pad0, pad1;
};

layout(set = 0, binding = 3, std430) readonly buffer MaterialBuffer { GpuMaterial materials[]; };
layout(set = 0, binding = 4, std430) readonly buffer VertexBuffer   { Vertex   vertices[];  };
layout(set = 0, binding = 5, std430) readonly buffer IndexBuffer    { uint     indices[];   };
layout(set = 0, binding = 6)          uniform sampler2D              textures[1024];
layout(set = 0, binding = 7, std430) readonly buffer GeometryBuffer { GpuGeometry geometries[]; };

layout(set = 0, binding = 10, rgba32f) uniform coherent image2D gbuffer_pos_0;
layout(set = 0, binding = 11, rgba32f) uniform coherent image2D gbuffer_pos_1;
layout(set = 0, binding = 12, rgba16f) uniform coherent image2D gbuffer_nrm_0;
layout(set = 0, binding = 13, rgba16f) uniform coherent image2D gbuffer_nrm_1;

// Texture slot indices - MUST match GLT::asset::material::texture_slot ------------------------------------------------
const uint          SLOT_BASE_COLOR = 0u;
const uint          SLOT_METALLIC_ROUGHNESS = 1u;
const uint          SLOT_NORMAL = 2u;
const uint          SLOT_EMISSIVE = 3u;
const uint          SLOT_OCCLUSION = 4u;
const uint          SLOT_HEIGHT = 5u;               // reserved, unused

// AO configuration ----------------------------------------------------------------------------------------------------
const int           AO_SAMPLES = 8;
const float         AO_RADIUS = 30.0;               // world-space occlusion radius
const float         AO_RAY_BIAS = 0.005;            // push origin off the surface

// Soft sun shadow configuration ---------------------------------------------------------------------------------------
const float         SUN_ANGULAR_RADIUS = 0.035;     // tan(half-angle); ~2 deg
const int           SUN_SAMPLES = 8;                // rays per pixel for the shadow test
const float         SHADOW_RAY_TMAX = 10000.0;      // effectively "to infinity" for the sun

// Indirect bounce configuration ---------------------------------------------------------------------------------------
// MAX_BOUNCES is the deepest shading level reached. A value of 3 means the primary hit plus three levels of indirect
// bounce (bounce 0, 1, 2, 3). The deepest chit does not trace further, so its own shadow test is the only nested ray it issues
//
// If you change this, also update max_recursion_depth in renderer.inl. The needed depth is 1 (rgen) + MAX_BOUNCES + 1 (a chit)
// + 1 (nested shadow from the deepest chit) = MAX_BOUNCES + 3
const int MAX_BOUNCES = 3;

// Number of indirect rays traced per chit at bounce 0
//
// The count halves each level: 4 at b0, 2 at b1, 1 at b2, and bounce 3 doesn't trace at all. The reason is that indirect
// cost multiplies: a flat 4 at every level means 4 * 4 * 4 = 64 indirect paths per primary ray, whereas 4 * 2 * 1 = 8 stays
// tractable while still giving the primary bounce - the one you actually see - the full 4x sample rate
//
// The deepest bounce contributes the smallest fraction of the final colour, so cutting it there costs almost nothing
// visually and saves the most rays
const int INDIRECT_SAMPLES_BASE = 4;

// Tiny hash-based RNG - good enough for AO, shadows and GI ------------------------------------------------------------
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

    // current bounce depth
    const int my_bounce = int(payload.w);

    // per-instance geometry record
    const GpuGeometry geom = geometries[gl_InstanceCustomIndexEXT + gl_GeometryIndexEXT];
    const GpuMaterial mat  = materials[geom.material_index];

    // triangle fetch
    const uint prim = gl_PrimitiveID;
    const uvec3 idx = uvec3(
        indices[geom.index_base + prim * 3u + 0u],
        indices[geom.index_base + prim * 3u + 1u],
        indices[geom.index_base + prim * 3u + 2u]);

    const Vertex v0 = vertices[geom.vertex_base + idx.x];
    const Vertex v1 = vertices[geom.vertex_base + idx.y];
    const Vertex v2 = vertices[geom.vertex_base + idx.z];

    const vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

    // interpolated attributes -----------------------------------------------------------------------------------------
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

    // world-space normal ----------------------------------------------------------------------------------------------
    const mat3 normal_matrix = transpose(mat3(gl_WorldToObjectEXT));
    vec3 N = normalize(normal_matrix * n_local);

    // material sampling -----------------------------------------------------------------------------------------------
    const vec4 base_tex = texture(textures[nonuniformEXT(mat.textures[SLOT_BASE_COLOR])], uv);
    const vec3 albedo = mat.base_color.rgb * base_tex.rgb;

    float roughness = mat.roughness;
    float metallic = mat.metallic;
    if (mat.textures[SLOT_METALLIC_ROUGHNESS] != 0u) {
        const vec2 mr = texture(textures[nonuniformEXT(mat.textures[SLOT_METALLIC_ROUGHNESS])], uv).gb;
        roughness *= mr.x;
        metallic *= mr.y;
    }
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic,  0.0,  1.0);

    float occ_mat = 1.0;
    if (mat.textures[SLOT_OCCLUSION] != 0u) {
        const float occ_tex = texture(textures[nonuniformEXT(mat.textures[SLOT_OCCLUSION])], uv).r;
        occ_mat = mix(1.0, occ_tex, mat.occlusion_strength);
    }

    vec3 emissive = mat.emissive;
    if (mat.textures[SLOT_EMISSIVE] != 0u)
        emissive *= texture(textures[nonuniformEXT(mat.textures[SLOT_EMISSIVE])], uv).rgb;

    // normal mapping --------------------------------------------------------------------------------------------------
    if (mat.textures[SLOT_NORMAL] != 0u) {
        vec3 n_tangent = texture(textures[nonuniformEXT(mat.textures[SLOT_NORMAL])], uv).xyz * 2.0 - 1.0;
        n_tangent.xy *= mat.normal_scale;

        vec3 T_world = normalize(normal_matrix * t_local);
        T_world = normalize(T_world - N * dot(N, T_world));
        const vec3 B_world = cross(N, T_world) * bitangent_sign;

        const mat3 TBN = mat3(T_world, B_world, N);
        N = normalize(TBN * n_tangent);
    }

    // world-space hit point -------------------------------------------------------------------------------------------
    const vec3 hit_world = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

    // RNG seed --------------------------------------------------------------------------------------------------------
    // Vary per-pixel, per-bounce, per-frame, AND per-incoming-ray-direction.
    //
    // The direction term is essential now that a single chit fires multiple indirect rays: without it, all N siblings
    // inherited the same seed and ran identical AO and shadow tests, and the extra samples did nothing. Hashing the incoming
    // ray direction (which is different for every sibling) decorrelates them
    //
    // The frame term is what makes temporal accumulation work: without it, every frame produces bit-identical noise and
    // the running average converges to a single noisy sample instead of the true mean
    uint seed = uint(gl_LaunchIDEXT.x)     * 1973u
              ^ uint(gl_LaunchIDEXT.y)     * 9277u
              ^ uint(my_bounce)            * 31337u
              ^ cam.temporal.w             * 71923u
              ^ floatBitsToUint(gl_WorldRayDirectionEXT.x) * 40499u
              ^ floatBitsToUint(gl_WorldRayDirectionEXT.y) * 51137u
              ^ floatBitsToUint(gl_WorldRayDirectionEXT.z) * 62473u
              ^ 26699u;

    // AO (bounce 0 only) ----------------------------------------------------------------------------------------------
    float ao_geometric = 1.0;
    if (my_bounce == 0) {
        float occlusion = 0.0;
        for (int i = 0; i < AO_SAMPLES; ++i) {
            const vec3 dir = cosine_hemisphere(seed, N);
            const vec3 org = hit_world + N * AO_RAY_BIAS;

            payload = vec4(0.0, 0.0, 0.0, float(my_bounce));
            traceRayEXT(
                topLevelAS,
                gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                0xFF,
                1u, 0u, 1u,
                org, 0.001, dir, AO_RADIUS, 0);
            occlusion += payload.x;
        }
        ao_geometric = 1.0 - occlusion / float(AO_SAMPLES);
    }
    const float ao = ao_geometric * occ_mat;

    // soft sun shadow (every bounce) ----------------------------------------------------------------------------------
    const vec3  L = normalize(cam.sun_direction.xyz);
    const float NdotL_raw = dot(N, L);
    float shadow = 0.0;

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

            payload = vec4(0.0, 0.0, 0.0, float(my_bounce));
            traceRayEXT(
                topLevelAS,
                gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                0xFF,
                1u, 0u, 1u,
                org, 0.001, dir, SHADOW_RAY_TMAX, 0);

            lit += (payload.x < 0.5) ? 1.0 : 0.0;
        }
        shadow = 1.0 - lit / float(SUN_SAMPLES);
    }

    // direct lighting -------------------------------------------------------------------------------------------------
    const float NdotL = max(NdotL_raw, 0.0) * (1.0 - shadow);

    const vec3  V = normalize(-gl_WorldRayDirectionEXT);
    const vec3  H = normalize(L + V);
    const float NdotH = max(dot(N, H), 0.0);
    const float VdotH = max(dot(V, H), 0.0);

    const vec3 sun_rgb = cam.sun_color.rgb * cam.sun_color.w;

    const vec3 F0 = mix(vec3(0.04), albedo, metallic);
    const vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    const float shininess = mix(4.0, 256.0, 1.0 - roughness);
    const float spec_term = pow(NdotH, shininess) * (1.0 - roughness) * mat.reflectance;

    const vec3 diffuse = albedo * (1.0 - metallic) * sun_rgb * NdotL;
    const vec3 specular = F * spec_term * sun_rgb * NdotL;
    const vec3 ambient = albedo * 0.15 * ao * (1.0 - metallic);

    vec3 result = ambient + diffuse + specular + emissive;

    // indirect bounce (emissive + multi-sample GI) --------------------------------------------------------------------
    // At each bounce level we trace `indirect_samples` cosine-weighted rays and average them. The estimator is unchanged
    // from the single-ray version:             L_o ~= albedo * mean( L_i )
    //
    // because cosine-weighted sampling makes the pdf cancel the BRDF and the cosine term. Just replace "L_i" with 
    // "the average of N independent L_i" and the formula holds. indirect_samples halves with each bounce: 
    // see INDIRECT_SAMPLES_BASE for why
    const int indirect_samples = max(1, INDIRECT_SAMPLES_BASE >> my_bounce);

    if (my_bounce < MAX_BOUNCES) {

        vec3 gi = vec3(0.0);
        for (int i = 0; i < indirect_samples; ++i) {

            const vec3 bounce_dir = cosine_hemisphere(seed, N);
            const vec3 bounce_org = hit_world + N * AO_RAY_BIAS;

            payload = vec4(0.0, 0.0, 0.0, float(my_bounce + 1));

            traceRayEXT(
                topLevelAS,
                gl_RayFlagsOpaqueEXT,
                0xFF,
                0u, 0u, 0u,
                bounce_org, 0.001, bounce_dir, SHADOW_RAY_TMAX, 0);

            gi += payload.xyz;
        }
        result += albedo * gi / float(indirect_samples);
    }

    // write the primary hit's world position AND normal into the G-buffer ---------------------------------------------
    // Only bounce 0 is a primary visibility hit. Bounce 1+ chits are shading off surfaces that already have a primary
    // hit at this pixel — writing them would corrupt the reprojection data
    if (my_bounce == 0) {
        const ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);
        const vec4 nrm_packed = vec4(N * 0.5 + 0.5, 1.0);   // [-1,1] -> [0,1]

        if (cam.temporal.z == 0u) {
            imageStore(gbuffer_pos_0, pixel, vec4(hit_world, 1.0));
            imageStore(gbuffer_nrm_0, pixel, nrm_packed);
        } else {
            imageStore(gbuffer_pos_1, pixel, vec4(hit_world, 1.0));
            imageStore(gbuffer_nrm_1, pixel, nrm_packed);
        }
    }

    payload = vec4(result, float(my_bounce));
}

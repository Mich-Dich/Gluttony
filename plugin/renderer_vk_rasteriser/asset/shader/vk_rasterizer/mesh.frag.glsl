
#version 450
#extension GL_EXT_nonuniform_qualifier : require

// ---- set 0: per frame ------------------------------------------------------------------------------------------------
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4                view_proj;
    mat4                view;
    vec4                camera_pos;
    vec4                frustum_planes[6];
    uvec4               flags;
    mat4                projection;
    mat4                inv_projection;
    uvec4               cluster_dims;       // x = grid_x, y = grid_y, z = grid_z
    vec4                cluster_depth;      // x = near, y = far, z = render_w, w = render_h
    mat4                light_view_proj;
    vec4                shadow_params;      // x = world units/texel, y = normal bias (texels), z = depth bias, w = 1/resolution
} camera;

struct Light {
    vec4                position_radius;       // xyz = world pos, w = radius
    vec4                color_intensity;       // rgb = color, a = intensity
    vec4                direction_inner_cos;   // xyz = world dir, w = cos(inner cone)
    vec4                params;                // x = cos(outer cone), y = type (0=dir, 1=point, 2=spot), z = shadow (dir: flag, local: first view + 1)
};

struct ShadowView {
    mat4                view_proj;
    vec4                atlas_rect;            // xy = uv offset, zw = uv scale (zw == 0: no tile)
    vec4                params;                // x = texel world size per unit distance, y = normal bias (texels), z = light-ward bias (texels), w = 1/atlas size
    vec4                sphere;
    vec4                planes[6];
    vec4                _pad[3];               // std430 stride = 256 bytes, same as SHADOW_VIEW_STRIDE
};

layout(std430, set = 0, binding = 2) readonly buffer LightBuffer { Light lights[]; } light_buffer;
layout(std430, set = 0, binding = 3) readonly buffer LightGrid { uvec2 grid[]; } light_grid;
layout(std430, set = 0, binding = 4) readonly buffer LightIndexList { uint indices[]; } light_index_list;
layout(set = 0, binding = 5) uniform sampler2DShadow shadow_map;
layout(std430, set = 0, binding = 6) readonly buffer ShadowViews { ShadowView views[]; } shadow_views;
layout(set = 0, binding = 7) uniform sampler2DShadow shadow_atlas;
layout(set = 0, binding = 8) uniform sampler material_sampler;

// ---- set 1: materials + bindless textures ----------------------------------------------------------------------------
struct Material {
    vec4                base_color;
    vec4                emissive_roughness;       // rgb = emissive factor, a = roughness
    vec4                metallic_reflectance;     // x = metallic, y = reflectance, z = normal scale, w = occlusion strength
    uvec4               textures_a;               // base_color, metallic_roughness, normal, emissive
    uvec4               textures_b;               // occlusion, height, flags, template id
};

layout(std430, set = 1, binding = 0) readonly buffer MaterialBuffer { Material materials[]; } material_buffer;
layout(set = 1, binding = 1) uniform texture2D material_textures[4096];      // MAX_TEXTURES

layout(location = 0) in vec3 in_world_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec4 in_tangent;
layout(location = 4) flat in uint in_material_index;

layout(location = 0) out vec4 out_color;

const float PI = 3.14159265359;
const vec3  AMBIENT = vec3(0.03);               // linear. Replaced by IBL in a later step
const bool  OUTPUT_TRANSFORM = true;            // tonemap + sRGB encode. scene_color is UNORM; set false to get the old raw output

vec4 sample_material_texture(uint index, vec2 uv) {
    return texture(sampler2D(material_textures[nonuniformEXT(index)], material_sampler), uv);
}

// Positive view-space Z from window-space depth (matches the light culling shader)
float linearize_depth(float d) {
    const vec4 view = camera.inv_projection * vec4(0.0, 0.0, d, 1.0);
    return -view.z / view.w;
}

// ---- shadows ---------------------------------------------------------------------------------------------------------

// Directional: 1 = lit, 0 = shadowed
float sample_shadow(vec3 world_pos, vec3 N, float ndotl) {

    const float normal_offset = camera.shadow_params.x * camera.shadow_params.y * (1.0 - ndotl);
    const vec4 light_clip = camera.light_view_proj * vec4(world_pos + N * normal_offset, 1.0);
    const vec3 ndc = light_clip.xyz / light_clip.w;

    const vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0)
        return 1.0;

    const float ref_depth = ndc.z - camera.shadow_params.z;
    const float texel = camera.shadow_params.w;

    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += textureLod(shadow_map, vec3(uv + vec2(x, y) * texel, ref_depth), 0.0);
    return sum / 9.0;
}

// Cube face from the light->fragment direction: +X -X +Y -Y +Z -Z (must match SHADOW_FACE_DIRECTIONS on the CPU)
uint point_face(vec3 d) {
    const vec3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0u : 1u;
    if (a.y >= a.z)               return d.y > 0.0 ? 2u : 3u;
    return d.z > 0.0 ? 4u : 5u;
}

// Point/spot atlas lookup: 1 = lit, 0 = shadowed
float sample_local_shadow(uint view_index, vec3 world_pos, vec3 N, vec3 L, float ndotl, float dist) {

    const ShadowView v = shadow_views.views[view_index];
    if (v.atlas_rect.z <= 0.0)
        return 1.0;

    const float texel_world = dist * v.params.x;
    const vec3 biased = world_pos + N * (texel_world * v.params.y * (1.0 - ndotl)) + L * (texel_world * v.params.z);

    const vec4 clip = v.view_proj * vec4(biased, 1.0);
    if (clip.w <= 0.0)
        return 1.0;

    const vec3 ndc = clip.xyz / clip.w;
    const vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || ndc.z > 1.0)
        return 1.0;

    const vec2 atlas_uv = v.atlas_rect.xy + uv * v.atlas_rect.zw;
    const float t = v.params.w;
    const vec2 lo = v.atlas_rect.xy + 0.5 * t;
    const vec2 hi = v.atlas_rect.xy + v.atlas_rect.zw - 0.5 * t;

    float s = 0.0;
    s += textureLod(shadow_atlas, vec3(clamp(atlas_uv + vec2(-0.5, -0.5) * t, lo, hi), ndc.z), 0.0);
    s += textureLod(shadow_atlas, vec3(clamp(atlas_uv + vec2( 0.5, -0.5) * t, lo, hi), ndc.z), 0.0);
    s += textureLod(shadow_atlas, vec3(clamp(atlas_uv + vec2(-0.5,  0.5) * t, lo, hi), ndc.z), 0.0);
    s += textureLod(shadow_atlas, vec3(clamp(atlas_uv + vec2( 0.5,  0.5) * t, lo, hi), ndc.z), 0.0);
    return s * 0.25;
}

// ---- PBR -------------------------------------------------------------------------------------------------------------

float D_GGX(float NoH, float a) {
    const float a2 = a * a;
    const float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    const float a2 = a * a;
    const float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    const float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 F_Schlick(float VoH, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

vec3 output_transform(vec3 c) {
    if (!OUTPUT_TRANSFORM)
        return c;
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);   // ACES fit (Narkowicz)
    return pow(c, vec3(1.0 / 2.2));
}

void main() {

    const Material mat = material_buffer.materials[in_material_index];

    // Material fetches first, in uniform control flow, so the implicit derivatives are valid
    const vec4  base_sample = sample_material_texture(mat.textures_a.x, in_uv);
    const vec4  mr_sample = sample_material_texture(mat.textures_a.y, in_uv);
    const vec4  normal_sample = sample_material_texture(mat.textures_a.z, in_uv);
    const vec3  emissive_sample = sample_material_texture(mat.textures_a.w, in_uv).rgb;
    const float occlusion_sample = sample_material_texture(mat.textures_b.x, in_uv).r;

    const vec3  base_color = mat.base_color.rgb * base_sample.rgb;
    const float roughness = clamp(mat.emissive_roughness.a * mr_sample.g, 0.045, 1.0);      // glTF packing: G = roughness, B = metallic
    const float metallic = clamp(mat.metallic_reflectance.x * mr_sample.b, 0.0, 1.0);
    const float reflectance = mat.metallic_reflectance.y;
    const float ao = mix(1.0, occlusion_sample, mat.metallic_reflectance.w);
    const vec3  emissive = mat.emissive_roughness.rgb * emissive_sample;

    // Geometric normal (used for shadow biasing) and shading normal (normal map)
    const vec3 Ng = normalize(in_normal);
    vec3 N = Ng;
    if (dot(in_tangent.xyz, in_tangent.xyz) > 1e-8) {
        const vec3 T = normalize(in_tangent.xyz - Ng * dot(Ng, in_tangent.xyz));
        const vec3 B = cross(Ng, T) * in_tangent.w;
        const vec2 nxy = normal_sample.xy * 2.0 - 1.0;                                      // z is reconstructed: works for BC5 and RGB
        const vec3 tn = normalize(vec3(nxy * mat.metallic_reflectance.z, sqrt(max(1.0 - dot(nxy, nxy), 0.0))));
        N = normalize(T * tn.x + B * tn.y + Ng * tn.z);
    }

    const vec3 V = normalize(camera.camera_pos.xyz - in_world_pos);
    const float NoV = max(dot(N, V), 1e-4);

    const vec3 diffuse_color = base_color * (1.0 - metallic);
    const vec3 f0 = mix(vec3(0.16 * reflectance * reflectance), base_color, metallic);
    const float a = roughness * roughness;

    // ---- Cluster lookup ----------------------------------------------------------------------------------------------
    const uvec3 grid_dims = camera.cluster_dims.xyz;
    const uvec2 tile = uvec2(gl_FragCoord.xy / vec2(16.0, 16.0));   // CLUSTER_TILE_SIZE_X/Y

    const float near_z = camera.cluster_depth.x;
    const float far_z = camera.cluster_depth.y;
    const float lz = clamp(linearize_depth(gl_FragCoord.z), near_z, far_z);

    const float slice_f = float(grid_dims.z) * log(lz / near_z) / log(far_z / near_z);
    const uint slice = uint(clamp(slice_f, 0.0, float(grid_dims.z - 1u)));
    const uint cluster_id = tile.x + tile.y * grid_dims.x + slice * grid_dims.x * grid_dims.y;

    const uvec2 entry = light_grid.grid[cluster_id];

    // ---- Forward+ accumulation ---------------------------------------------------------------------------------------
    vec3 Lo = vec3(0.0);

    for (uint i = 0u; i < entry.y; ++i) {

        const Light light = light_buffer.lights[light_index_list.indices[entry.x + i]];
        const uint type = uint(light.params.y);

        vec3 L;
        float attenuation = 1.0;
        float light_dist = 0.0;

        if (type == 0u) {
            L = normalize(-light.direction_inner_cos.xyz);
        } else {
            const vec3 to_light = light.position_radius.xyz - in_world_pos;
            const float dist = length(to_light);
            const float radius = light.position_radius.w;
            L = to_light / max(dist, 1e-4);
            light_dist = dist;

            const float x = clamp(dist / radius, 0.0, 1.0);
            const float window = 1.0 - x * x;
            attenuation = window * window;

            if (type == 2u) {
                const float cd = dot(-L, normalize(light.direction_inner_cos.xyz));
                const float cos_outer = light.params.x;
                const float cos_inner = light.direction_inner_cos.w;
                const float spot = clamp((cd - cos_outer) / max(cos_inner - cos_outer, 1e-4), 0.0, 1.0);
                attenuation *= spot * spot;
            }
        }

        const float NoL = clamp(dot(N, L), 0.0, 1.0);
        if (NoL <= 0.0 || attenuation <= 0.0)
            continue;

        if (light.params.z > 0.5) {
            const float ndotl_geo = clamp(dot(Ng, L), 0.0, 1.0);
            if (type == 0u) {
                attenuation *= sample_shadow(in_world_pos, Ng, ndotl_geo);
            } else if (attenuation > 0.002) {
                uint view_index = uint(light.params.z) - 1u;
                if (type == 1u)
                    view_index += point_face(in_world_pos - light.position_radius.xyz);
                attenuation *= sample_local_shadow(view_index, in_world_pos, Ng, L, ndotl_geo, light_dist);
            }
        }

        const vec3 H = normalize(V + L);
        const float NoH = clamp(dot(N, H), 0.0, 1.0);
        const float VoH = clamp(dot(V, H), 0.0, 1.0);

        const vec3 specular = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F_Schlick(VoH, f0);

        // Light intensity keeps the engine's existing convention (a white diffuse surface facing the light reflects `intensity`),
        // i.e. radiance is pre-multiplied by PI: Lo = (diffuse/PI + specular) * (radiance * PI) * NoL
        const vec3 radiance = light.color_intensity.rgb * light.color_intensity.a * attenuation;
        Lo += (diffuse_color + specular * PI) * radiance * NoL;
    }

    const vec3 ambient = AMBIENT * (diffuse_color + f0 * mix(0.5, 0.1, roughness)) * ao;

    out_color = vec4(output_transform(ambient + Lo + emissive), 1.0);
}

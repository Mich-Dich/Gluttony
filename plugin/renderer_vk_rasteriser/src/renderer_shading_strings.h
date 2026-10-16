
#pragma once


// Prefix + suffixes textually concatenated with each material's Slang source before compilation
// Concat contract:         source = prefix + "\n" + material_source + "\n" + suffix
// Naming rule: every symbol declared here MUST start with `renderer_`. Material-authored code shares the translation unit;
// anything unprefixed risks colliding



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::shading_strings {

    // CONSTANTS =======================================================================================================

    inline constexpr std::string_view prefix = R"SLANG(
import material_contract_v0;

// ---- set 0: frame-scoped -------------------------------------------------------------
struct RendererInstance {
    float4x4                                                    model;
    float4x4                                                    normal_matrix;
    uint                                                        mesh_index;
    uint                                                        material_index;
    uint                                                        _pad0;
    uint                                                        _pad1;
};

struct RendererLight {
    float4                                                      position_radius;
    float4                                                      color_intensity;
    float4                                                      direction_inner_cos;
    float4                                                      params;
};

struct RendererShadowView {
    float4x4                                                    view_proj;
    float4                                                      atlas_rect;
    float4                                                      params;
    float4                                                      sphere;
    float4                                                      planes[6];
    float4                                                      _pad[3];
};

struct RendererCamera {
    float4x4                                                    view_proj;
    float4x4                                                    view;
    float4                                                      camera_pos;
    float4                                                      frustum_planes[6];
    uint4                                                       flags;
    float4x4                                                    projection;
    float4x4                                                    inv_projection;
    uint4                                                       cluster_dims;
    float4                                                      cluster_depth;      // x=near y=far z=render_w w=render_h
    float4x4                                                    light_view_proj;
    float4                                                      shadow_params;      // x=world_per_texel y=normal_bias z=depth_bias w=1/res
};

[[vk::binding(0, 0)]] ConstantBuffer<RendererCamera>            renderer_camera;
[[vk::binding(1, 0)]] StructuredBuffer<RendererInstance>        renderer_instances;
[[vk::binding(2, 0)]] StructuredBuffer<RendererLight>           renderer_lights;
[[vk::binding(3, 0)]] StructuredBuffer<uint2>                   renderer_light_grid;
[[vk::binding(4, 0)]] StructuredBuffer<uint>                    renderer_light_list;
[[vk::binding(5, 0)]] Texture2D                                 renderer_shadow_map;
[[vk::binding(6, 0)]] StructuredBuffer<RendererShadowView>      renderer_shadow_views;
[[vk::binding(7, 0)]] Texture2D                                 renderer_shadow_atlas;
[[vk::binding(8, 0)]] SamplerComparisonState                    renderer_shadow_sampler;

// ---- set 1: per-material textures ----------------------------------------------------
[[vk::binding(1, 1)]] Texture2D                                 renderer_tex_base_color;
[[vk::binding(2, 1)]] Texture2D                                 renderer_tex_metallic_roughness;
[[vk::binding(3, 1)]] Texture2D                                 renderer_tex_normal;
[[vk::binding(4, 1)]] Texture2D                                 renderer_tex_emissive;
[[vk::binding(5, 1)]] Texture2D                                 renderer_tex_occlusion;
[[vk::binding(6, 1)]] Texture2D                                 renderer_tex_height;
[[vk::binding(7, 1)]] SamplerState                              renderer_material_sampler;

// ---- interpolants --------------------------------------------------------------------
struct renderer_vertex_outputs {
    [[vk::location(0)]] float3                                  position_ws;
    [[vk::location(1)]] float3                                  normal_ws;
    [[vk::location(2)]] float4                                  tangent_ws;
    [[vk::location(3)]] float2                                  uv0;
};

// ---- helpers -------------------------------------------------------------------------
static const float                                              RENDERER_PI = 3.14159265359;
static const float3                                             RENDERER_AMBIENT = float3(0.03, 0.03, 0.03);

float renderer_linearize_depth(float d) {

    float4 view = mul(renderer_camera.inv_projection, float4(0.0, 0.0, d, 1.0));
    return -view.z / view.w;
}

uint renderer_point_face(float3 d) {

    float3 a = abs(d);
    if (a.x >= a.y && a.x >= a.z) return d.x > 0.0 ? 0u : 1u;
    if (a.y >= a.z)               return d.y > 0.0 ? 2u : 3u;
    return d.z > 0.0 ? 4u : 5u;
}

float renderer_sample_shadow(float3 world_pos, float3 N, float ndotl) {

    float normal_offset = renderer_camera.shadow_params.x * renderer_camera.shadow_params.y * (1.0 - ndotl);
    float4 lc = mul(renderer_camera.light_view_proj, float4(world_pos + N * normal_offset, 1.0));
    float3 ndc = lc.xyz / lc.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0)
        return 1.0;
    float ref_depth = ndc.z - renderer_camera.shadow_params.z;
    float texel = renderer_camera.shadow_params.w;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += renderer_shadow_map.SampleCmpLevelZero(renderer_shadow_sampler, uv + float2(x, y) * texel, ref_depth);
    return sum / 9.0;
}

float renderer_sample_local_shadow(uint view_index, float3 world_pos, float3 N, float3 L, float ndotl, float dist) {

    RendererShadowView v = renderer_shadow_views[view_index];
    if (v.atlas_rect.z <= 0.0) return 1.0;

    float texel_world = dist * v.params.x;
    float3 biased = world_pos + N * (texel_world * v.params.y * (1.0 - ndotl))
                              + L * (texel_world * v.params.z);
    float4 clip = mul(v.view_proj, float4(biased, 1.0));
    if (clip.w <= 0.0) return 1.0;

    float3 ndc = clip.xyz / clip.w;
    float2 uv = ndc.xy * 0.5 + 0.5;
    if (any(uv < 0.0) || any(uv > 1.0) || ndc.z > 1.0) return 1.0;

    float2 atlas_uv = v.atlas_rect.xy + uv * v.atlas_rect.zw;
    float  t = v.params.w;
    float2 lo = v.atlas_rect.xy + 0.5 * t;
    float2 hi = v.atlas_rect.xy + v.atlas_rect.zw - 0.5 * t;

    float s = 0.0;
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2(-0.5,-0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2( 0.5,-0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2(-0.5, 0.5)*t, lo, hi), ndc.z);
    s += renderer_shadow_atlas.SampleCmpLevelZero(renderer_shadow_sampler, clamp(atlas_uv + float2( 0.5, 0.5)*t, lo, hi), ndc.z);
    return s * 0.25;
}

float renderer_D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (RENDERER_PI * d * d);
}

float renderer_V_SmithGGXCorrelated(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

float3 renderer_F_Schlick(float VoH, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
}

float3 renderer_output_transform(float3 c) {
    c = clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0);
    return pow(c, float3(1.0 / 2.2));
}
)SLANG";


    inline constexpr std::string_view vertex_suffix = R"SLANG(

// ---- vertex stage --------------------------------------------------------------------
struct renderer_vert_attributes {
    [[vk::location(0)]] float3          position;
    [[vk::location(1)]] float3          normal;
    [[vk::location(2)]] float4          tangent;
    [[vk::location(3)]] float2          uv0;
};

struct renderer_vert_result {
    float4                              clip_pos : SV_Position;
    renderer_vertex_outputs             varyings;
};

[shader("vertex")]
renderer_vert_result vertex_main(renderer_vert_attributes v, uint instance_id : SV_InstanceID,
    uint base_instance : SV_StartInstanceLocation) {

    RendererInstance inst = renderer_instances[base_instance + instance_id];

    vertex_input vi;
    vi.position_ws = mul(inst.model, float4(v.position, 1.0)).xyz;
    vi.normal_ws = normalize(mul(inst.normal_matrix, float4(v.normal, 0.0)).xyz);
    vi.tangent_ws = float4(normalize(mul(inst.model, float4(v.tangent.xyz, 0.0)).xyz), v.tangent.w);
    vi.uv0 = v.uv0;
    vi.uv1 = float2(0.0, 0.0);
    vi.color = float3(1.0, 1.0, 1.0);

    vertex_output vo = passthrough(vi);

    renderer_vert_result o;
    o.clip_pos = mul(renderer_camera.view_proj, float4(vo.position_ws, 1.0));
    o.varyings.position_ws = vo.position_ws;
    o.varyings.normal_ws = vo.normal_ws;
    o.varyings.tangent_ws = vo.tangent_ws;
    o.varyings.uv0 = vo.uv0;
    return o;
}
)SLANG";


    inline constexpr std::string_view vertex_suffix_with_transform = R"SLANG(

// ---- vertex stage --------------------------------------------------------------------
struct renderer_vert_attributes {
    [[vk::location(0)]] float3          position;
    [[vk::location(1)]] float3          normal;
    [[vk::location(2)]] float4          tangent;
    [[vk::location(3)]] float2          uv0;
};

struct renderer_vert_result {
    float4                              clip_pos : SV_Position;
    renderer_vertex_outputs             varyings;
};

// Same binding as the fragment stage: set 1, binding 0. Vertex visibility is enabled
// on the descriptor set layout (see step 3)
[[vk::binding(0, 1)]] ConstantBuffer<material_params> renderer_material_params;

[shader("vertex")]
renderer_vert_result vertex_main(renderer_vert_attributes v, uint instance_id : SV_InstanceID,
    uint base_instance : SV_StartInstanceLocation) {

    RendererInstance inst = renderer_instances[base_instance + instance_id];

    vertex_input vi;
    vi.position_ws = mul(inst.model, float4(v.position, 1.0)).xyz;
    vi.normal_ws = normalize(mul(inst.normal_matrix, float4(v.normal, 0.0)).xyz);
    vi.tangent_ws = float4(normalize(mul(inst.model, float4(v.tangent.xyz, 0.0)).xyz), v.tangent.w);
    vi.uv0 = v.uv0;
    vi.uv1 = float2(0.0, 0.0);
    vi.color = float3(1.0, 1.0, 1.0);

    // inout: a material may write back to `p`. We take a local copy so the UBO itself
    // is never mutated. Per the contract, edits are scoped to this one vertex.
    material_params p = renderer_material_params;
    vertex_output vo = vertex_transform(vi, p);

    renderer_vert_result o;
    o.clip_pos = mul(renderer_camera.view_proj, float4(vo.position_ws, 1.0));
    o.varyings.position_ws = vo.position_ws;
    o.varyings.normal_ws = vo.normal_ws;
    o.varyings.tangent_ws = vo.tangent_ws;
    o.varyings.uv0 = vo.uv0;
    return o;
}
)SLANG";


    inline constexpr std::string_view fragment_suffix = R"SLANG(

// ---- fragment stage ------------------------------------------------------------------
[[vk::binding(0, 1)]] ConstantBuffer<material_params> renderer_material_params;

material_textures renderer_assemble_textures() {
    material_textures t;
    t.base_color = renderer_tex_base_color;
    t.metallic_roughness = renderer_tex_metallic_roughness;
    t.normal = renderer_tex_normal;
    t.emissive = renderer_tex_emissive;
    t.occlusion = renderer_tex_occlusion;
    t.height = renderer_tex_height;
    t.sampler = renderer_material_sampler;
    return t;
}

[shader("fragment")]
float4 fragment_main(renderer_vertex_outputs i, float4 sv_pos : SV_Position) : SV_Target {

    material_textures tex = renderer_assemble_textures();

    surface_input s = {};
    s.position_ws = i.position_ws;
    s.normal_ws = normalize(i.normal_ws);
    s.tangent_ws = i.tangent_ws;
    s.uv0 = i.uv0;
    s.uv1 = float2(0.0, 0.0);
    s.color = float3(1.0, 1.0, 1.0);
    s.view_dir_ws = normalize(renderer_camera.camera_pos.xyz - i.position_ws);
    s.screen_uv = sv_pos.xy / renderer_camera.cluster_depth.zw;
    s.view_depth = renderer_linearize_depth(sv_pos.z);
    s.ddx_ws = ddx(i.position_ws);
    s.ddy_ws = ddy(i.position_ws);

    surface_output o = surface_evaluate(s, renderer_material_params, tex);

    const float3 Ng = normalize(i.normal_ws);
    const float3 N = normalize(o.normal_ws);
    const float3 V = s.view_dir_ws;
    const float  NoV = max(dot(N, V), 1e-4);

    const float3 diffuse_color = o.base_color * (1.0 - o.metallic);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), o.base_color, o.metallic);
    const float  a = o.roughness * o.roughness;

    // ---- cluster lookup -----------------------------------------------------------
    uint3  grid_dims = renderer_camera.cluster_dims.xyz;
    uint2  tile = uint2(sv_pos.xy / float2(16.0, 16.0));
    float  near_z = renderer_camera.cluster_depth.x;
    float  far_z = renderer_camera.cluster_depth.y;
    float  lz = clamp(s.view_depth, near_z, far_z);
    float  slice_f = float(grid_dims.z) * log(lz / near_z) / log(far_z / near_z);
    uint   slice = uint(clamp(slice_f, 0.0, float(grid_dims.z - 1u)));
    uint   cluster = tile.x + tile.y * grid_dims.x + slice * grid_dims.x * grid_dims.y;
    uint2  entry = renderer_light_grid[cluster];

    // ---- Forward+ -----------------------------------------------------------------
    float3 Lo = float3(0.0, 0.0, 0.0);

    for (uint li = 0u; li < entry.y; ++li) {

        RendererLight light = renderer_lights[renderer_light_list[entry.x + li]];
        uint  type = uint(light.params.y);
        float3 L;
        float attenuation = 1.0;
        float light_dist = 0.0;

        if (type == 0u) {
            L = normalize(-light.direction_inner_cos.xyz);
        } else {
            float3 to_light = light.position_radius.xyz - i.position_ws;
            float  dist = length(to_light);
            float  radius = light.position_radius.w;
            L = to_light / max(dist, 1e-4);
            light_dist = dist;
            float x = clamp(dist / radius, 0.0, 1.0);
            float window = 1.0 - x * x;
            attenuation = window * window;
            if (type == 2u) {
                float cd = dot(-L, normalize(light.direction_inner_cos.xyz));
                float cos_outer = light.params.x;
                float cos_inner = light.direction_inner_cos.w;
                float spot = clamp((cd - cos_outer) / max(cos_inner - cos_outer, 1e-4), 0.0, 1.0);
                attenuation *= spot * spot;
            }
        }

        float NoL = clamp(dot(N, L), 0.0, 1.0);
        if (NoL <= 0.0 || attenuation <= 0.0) continue;

        if (light.params.z > 0.5) {
            float ndotl_geo = clamp(dot(Ng, L), 0.0, 1.0);
            if (type == 0u) {
                attenuation *= renderer_sample_shadow(i.position_ws, Ng, ndotl_geo);
            } else if (attenuation > 0.002) {
                uint view_index = uint(light.params.z) - 1u;
                if (type == 1u)
                    view_index += renderer_point_face(i.position_ws - light.position_radius.xyz);
                attenuation *= renderer_sample_local_shadow(view_index, i.position_ws, Ng, L, ndotl_geo, light_dist);
            }
        }

        float3 H = normalize(V + L);
        float  NoH = clamp(dot(N, H), 0.0, 1.0);
        float  VoH = clamp(dot(V, H), 0.0, 1.0);
        float3 spec = renderer_D_GGX(NoH, a) * renderer_V_SmithGGXCorrelated(NoV, NoL, a) * renderer_F_Schlick(VoH, f0);
        float3 radiance = light.color_intensity.rgb * light.color_intensity.a * attenuation;
        Lo += (diffuse_color + spec * RENDERER_PI) * radiance * NoL;
    }

    float3 ambient = RENDERER_AMBIENT * (diffuse_color + f0 * lerp(0.5, 0.1, o.roughness)) * o.ao;
    float3 final_c = renderer_output_transform(ambient + Lo + o.emissive);

    return float4(final_c, o.opacity);
}
)SLANG";


    inline constexpr std::string_view depth_suffix = R"SLANG(

// ---- depth-only fragment stage ------------------------------------------------------
[[vk::binding(0, 1)]] ConstantBuffer<material_params> renderer_material_params;

material_textures renderer_assemble_textures() {
    material_textures t;
    t.base_color = renderer_tex_base_color;
    t.metallic_roughness = renderer_tex_metallic_roughness;
    t.normal = renderer_tex_normal;
    t.emissive = renderer_tex_emissive;
    t.occlusion = renderer_tex_occlusion;
    t.height = renderer_tex_height;
    t.sampler = renderer_material_sampler;
    return t;
}

[shader("fragment")]
void depth_main(renderer_vertex_outputs i, float4 sv_pos : SV_Position) {

    material_textures tex = renderer_assemble_textures();

    surface_input s = {};
    s.position_ws = i.position_ws;
    s.normal_ws = normalize(i.normal_ws);
    s.tangent_ws = i.tangent_ws;
    s.uv0 = i.uv0;
    s.uv1 = float2(0.0, 0.0);
    s.color = float3(1.0, 1.0, 1.0);
    s.view_dir_ws = normalize(renderer_camera.camera_pos.xyz - i.position_ws);
    s.screen_uv = sv_pos.xy / renderer_camera.cluster_depth.zw;
    s.view_depth = renderer_linearize_depth(sv_pos.z);
    s.ddx_ws = float3(0.0, 0.0, 0.0);
    s.ddy_ws = float3(0.0, 0.0, 0.0);

    if (!alpha_test(s, renderer_material_params, tex))
        discard;
}
)SLANG";


    inline constexpr std::string_view depth_noop_suffix = R"SLANG(

// ---- depth-only fragment stage (opaque materials) -----------------------------------
[shader("fragment")]
void depth_main(renderer_vertex_outputs i, float4 sv_pos : SV_Position) {

    // Opaque materials write depth only. Slang eliminates a fragment entry point whose body
    // has no observable effect, and the resulting SPIR-V is malformed (the parser hits
    // SPV_END_OF_STREAM at pipeline creation). The unreachable `discard` below is a marker:
    // it tells the compiler this is a real fragment shader that may kill fragments, so the
    // entry point survives DCE with a well-formed body.
    //
    // The compiler cannot prove the branch dead: `sv_pos.xy` are fragment coordinates in
    // pixels, and the compiler has no knowledge of the render target's extent.
    if (sv_pos.x > 1e30 || sv_pos.y > 1e30)
        discard;
}
)SLANG";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

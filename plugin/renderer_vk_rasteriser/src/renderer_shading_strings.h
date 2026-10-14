
#pragma once


// Prefix + suffixes textually concatenated with each material's Slang source before compilation. DO NOT load these from
// disk — they're baked into the renderer binary so a mismatch between an installed renderer and its shading library is impossible
//
// Concat contract (see renderer_shader_compiler.inl):     source = prefix + "\n" + material_source + "\n" + suffix
//
// Naming rule: every symbol declared in the prefix or suffixes MUST be prefixed with `renderer_` (or live inside a `renderer_*`
// struct). Material-authored code shares the translation unit, and anything unprefixed risks colliding.


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::shading_strings {

    // CONSTANTS =======================================================================================================

    inline constexpr std::string_view prefix = R"SLANG(
import material_contract_v0;

// ---- set 0 per-material bindings ----------------------------------------------------
[[vk::binding(2, 0)]] Texture2D    renderer_tex_base_color;
[[vk::binding(3, 0)]] Texture2D    renderer_tex_metallic_roughness;
[[vk::binding(4, 0)]] Texture2D    renderer_tex_normal;
[[vk::binding(5, 0)]] Texture2D    renderer_tex_emissive;
[[vk::binding(6, 0)]] Texture2D    renderer_tex_occlusion;
[[vk::binding(7, 0)]] Texture2D    renderer_tex_height;
[[vk::binding(8, 0)]] SamplerState renderer_material_sampler;

// Shared vertex-stage output / fragment-stage input. Locations must match the pipeline's
// vertex input layout (mesh.vert.glsl today; Phase 5 replaces that with this file).
struct renderer_vertex_outputs {
    [[vk::location(0)]] float3 position_ws;
    [[vk::location(1)]] float3 normal_ws;
    [[vk::location(2)]] float4 tangent_ws;
    [[vk::location(3)]] float2 uv0;
};
)SLANG";


    inline constexpr std::string_view vertex_suffix = R"SLANG(

// ---- vertex stage -------------------------------------------------------------------
struct renderer_vert_attributes {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float4 tangent;
    [[vk::location(3)]] float2 uv0;
};

struct renderer_vert_result {
    float4                  clip_pos : SV_Position;
    renderer_vertex_outputs varyings;
};

[shader("vertex")]
renderer_vert_result vertex_main(renderer_vert_attributes v) {

    vertex_input vi;
    vi.position_ws = v.position;
    vi.normal_ws = normalize(v.normal);
    vi.tangent_ws = v.tangent;
    vi.uv0 = v.uv0;
    vi.uv1 = float2(0.0, 0.0);
    vi.color = float3(1.0, 1.0, 1.0);

    // Phase 5: call vertex_transform(vi, ...) when the material declares it.
    // Phase 1: always passthrough.
    vertex_output vo = passthrough(vi);

    renderer_vert_result o;
    o.clip_pos = mul(globals.view_projection, float4(vo.position_ws, 1.0));
    o.varyings.position_ws = vo.position_ws;
    o.varyings.normal_ws = vo.normal_ws;
    o.varyings.tangent_ws = vo.tangent_ws;
    o.varyings.uv0 = vo.uv0;
    return o;
}
)SLANG";


    inline constexpr std::string_view fragment_suffix = R"SLANG(

// ---- fragment stage -----------------------------------------------------------------
[[vk::binding(1, 0)]] ConstantBuffer<material_params> renderer_material_params;

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
    s.view_dir_ws = normalize(globals.camera_position - i.position_ws);
    s.screen_uv = sv_pos.xy / globals.render_size;
    s.view_depth = 0.0;
    s.ddx_ws = ddx(i.position_ws);
    s.ddy_ws = ddy(i.position_ws);

    surface_output o = surface_evaluate(s, renderer_material_params, tex);

    // Phase 5: run the renderer's lighting loop on `o` before output.
    // Phase 1: emit the surface as-is so the concat path is provable end-to-end.
    return float4(o.base_color, o.opacity);
}
)SLANG";


    inline constexpr std::string_view depth_suffix = R"SLANG(

// ---- depth-only fragment stage (masked materials only) ------------------------------
[[vk::binding(1, 0)]] ConstantBuffer<material_params> renderer_material_params;

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
    s.view_dir_ws = normalize(globals.camera_position - i.position_ws);
    s.screen_uv = sv_pos.xy / globals.render_size;
    s.view_depth = 0.0;
    s.ddx_ws = float3(0.0, 0.0, 0.0);
    s.ddy_ws = float3(0.0, 0.0, 0.0);

    if (!alpha_test(s, renderer_material_params, tex))
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

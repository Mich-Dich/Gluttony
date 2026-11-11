
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::modes {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using GLT::reflect::annotations::display_name;
    using GLT::reflect::annotations::tooltip;
    using GLT::reflect::annotations::category;


    [[=display_name{"Lit"}]]
    [[=tooltip{"Full shading with lighting"}]]
    [[=category{"Shading"}]]
    inline constexpr GLT::render::render_mode Lit{ .id = 0, .is_default = true };


    // [[=display_name{"Unlit"}]]
    // [[=tooltip{"Flat surface color, no lighting"}]]
    // [[=category{"Shading"}]]
    // inline constexpr GLT::render::render_mode Unlit{ .id = 1 };


    [[=display_name{"Light Culling"}]]
    [[=tooltip{"Heatmap of lights per cluster (debug)"}]]
    [[=category{"Debug"}]]
    inline constexpr GLT::render::render_mode Light_cull{ .id = 2 };

    [[=display_name{"Level of Detail"}]]
    [[=tooltip{"Meshes coloured by the LOD they are drawn with: green 0, yellow-green 1, yellow 2, orange 3, red 4, magenta 5, purple 6, blue 7. Dark grey = no LODs generated, light grey = procedural"}]]
    [[=category{"Debug"}]]
    inline constexpr GLT::render::render_mode Lod{ .id = 3 };

    [[=display_name{"Triangle Density"}]]
    [[=tooltip{"Heatmap of triangles per pixel over a flat, unlit world (blue = sparse, red = one or more triangles per pixel). Blur radius and range: renderer settings, Debug"}]]
    [[=category{"Debug"}]]
    inline constexpr GLT::render::render_mode Tri_density{ .id = 4 };

    // STATIC VARIABLES ================================================================================================

    inline constexpr auto mode_table = GLT::render::make_render_mode_entries<^^modes>();

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

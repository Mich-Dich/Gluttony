
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

    // STATIC VARIABLES ================================================================================================

    inline constexpr auto mode_table = GLT::render::make_render_mode_entries<^^modes>();

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

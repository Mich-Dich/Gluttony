
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_ray::modes {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using GLT::reflect::annotations::display_name;
    using GLT::reflect::annotations::tooltip;
    using GLT::reflect::annotations::category;


    [[=display_name{"Lit"}]]
    [[=tooltip{"Full shading with lighting"}]]
    [[=category{"Shading"}]]
    inline constexpr GLT::render::render_mode lit{ .id = 0, .is_default = true };


    [[=display_name{"Unlit"}]]
    [[=tooltip{"Flat surface color, no lighting"}]]
    [[=category{"Shading"}]]
    inline constexpr GLT::render::render_mode unlit{ .id = 1 };


    [[=display_name{"Albedo"}]]
    [[=category{"Material"}]]
    inline constexpr GLT::render::render_mode albedo{ .id = 2 };


    [[=display_name{"Normals"}]]
    [[=tooltip{"World-space normals, remapped to [0,1]"}]]
    [[=category{"Geometry"}]]
    inline constexpr GLT::render::render_mode normals{ .id = 3 };


    [[=display_name{"Depth"}]]
    [[=category{"Geometry"}]]
    inline constexpr GLT::render::render_mode depth{ .id = 4 };

    // STATIC VARIABLES ================================================================================================

    inline constexpr auto mode_table = GLT::render::make_render_mode_entries<^^modes>();

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

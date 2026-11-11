
#pragma once

#include "reflection/annotations.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::settings {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using GLT::reflect::annotations::display_name;
    using GLT::reflect::annotations::tooltip;
    using GLT::reflect::annotations::category;
    using GLT::reflect::annotations::range;
    using GLT::reflect::annotations::name;


    // Stable name required - two plugins can't both be "renderer.vk_rasterizer.visual"
    struct [[=name{"renderer.vk_rasterizer.visual"}]] visual {

        // ---- Post Process -------------------------------------------------------

        [[=category{"Post Process"}]]
        [[=display_name{"Exposure (EV)"}]]
        [[=tooltip{"Exposure compensation in stops. Each +1 doubles the brightness before tonemapping"}]]
        [[=range{-10.0, 10.0}]]
        f32 exposure_ev = 0.0f;


        [[=category{"Post Process"}]]
        [[=display_name{"Tonemapper"}]]
        [[=tooltip{"0 = ACES (fitted)   1 = AgX   2 = Reinhard   3 = None (clamp)"}]]
        [[=range{0.0, 3.0}]]
        u32 tonemapper = 0u;

        // ---- Debug views --------------------------------------------------------
        [[=category{"Debug"}]]
        [[=display_name{"Triangle density blur (px)"}]]
        [[=tooltip{"Render mode 'Triangle Density': gaussian blur radius of the heatmap in pixels (0 = raw per-pixel counts)"}]]
        [[=range{0.0, 32.0}]]
        u32 tri_density_radius = 4u;

        [[=category{"Debug"}]]
        [[=display_name{"Triangle density max (tris / px)"}]]
        [[=tooltip{"Render mode 'Triangle Density': triangles per pixel shown as full red. The scale is logarithmic over 10 stops below it"}]]
        [[=range{0.01, 16.0}]]
        f32 tri_density_max = 1.0f;

        // Sky, sun and ambient light are world components now ("sky" / "ambient_light" / directional "light" on the
        // world entity), see GLT::world::environment_snapshot
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

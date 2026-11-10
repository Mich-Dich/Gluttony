
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

        // Sky, sun and ambient light are world components now ("sky" / "ambient_light" / directional "light" on the
        // world entity), see GLT::world::environment_snapshot
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

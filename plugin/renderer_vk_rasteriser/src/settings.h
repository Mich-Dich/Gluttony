
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

        // ---- Ambient ------------------------------------------------------------

        [[=category{"Ambient"}]]
        [[=display_name{"Intensity"}]]
        [[=tooltip{"Scale of the sky/ground ambient light (stand-in for image based lighting until the sky pass exists)"}]]
        [[=range{0.0, 20.0}]]
        f32 ambient_intensity = 1.0f;


        [[=category{"Ambient"}]]
        [[=display_name{"Sky R"}]]
        [[=range{0.0, 1.0}]]
        f32 ambient_sky_r = 0.040f;


        [[=category{"Ambient"}]]
        [[=display_name{"Sky G"}]]
        [[=range{0.0, 1.0}]]
        f32 ambient_sky_g = 0.045f;


        [[=category{"Ambient"}]]
        [[=display_name{"Sky B"}]]
        [[=range{0.0, 1.0}]]
        f32 ambient_sky_b = 0.055f;


        [[=category{"Ambient"}]]
        [[=display_name{"Ground R"}]]
        [[=range{0.0, 1.0}]]
        f32 ambient_ground_r = 0.020f;


        [[=category{"Ambient"}]]
        [[=display_name{"Ground G"}]]
        [[=range{0.0, 1.0}]]
        f32 ambient_ground_g = 0.018f;


        [[=category{"Ambient"}]]
        [[=display_name{"Ground B"}]]
        [[=range{0.0, 1.0}]]
        f32 ambient_ground_b = 0.016f;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

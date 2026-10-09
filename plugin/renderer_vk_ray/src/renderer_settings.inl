
#pragma once

#include "reflection/annotations.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_ray::settings {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    using GLT::reflect::annotations::display_name;
    using GLT::reflect::annotations::tooltip;
    using GLT::reflect::annotations::category;
    using GLT::reflect::annotations::range;
    using GLT::reflect::annotations::name;


    // Stable name required - two plugins can't both be "renderer_vk_ray.visual"
    struct [[=name{"renderer_vk_ray.visual"}]] visual {

        // ---- Ambient Occlusion --------------------------------------------------

        [[=category{"Ambient Occlusion"}]]
        [[=display_name{"Samples"}]]
        [[=tooltip{"AO shadow rays cast per hit"}]]
        [[=range{1.0, 64.0}]]
        u32 ao_samples = 4u;


        [[=category{"Ambient Occlusion"}]]
        [[=display_name{"Radius"}]]
        [[=range{0.0, 500.0}]]
        f32 ao_radius = 30.0f;


        [[=category{"Ambient Occlusion"}]]
        [[=display_name{"Ray Bias"}]]
        [[=range{0.0, 0.1}]]
        f32 ao_ray_bias = 0.005f;

        // ---- Lighting -----------------------------------------------------------

        [[=category{"Lighting"}]]
        [[=display_name{"Indirect Samples"}]]
        [[=range{1.0, 16.0}]]
        u32 indirect_samples_base = 3u;

        // ---- Temporal -----------------------------------------------------------

        [[=category{"Temporal"}]]
        [[=display_name{"Max History"}]]
        [[=range{1.0, 256.0}]]
        u32 temporal_max_history = 64u;


        [[=category{"Temporal"}]]
        [[=display_name{"Clip K"}]]
        [[=range{0.0, 10.0}]]
        f32 temporal_clip_k = 2.0f;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

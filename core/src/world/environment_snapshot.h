#pragma once

#include <glm/glm.hpp>

#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Per-frame, copyable snapshot of the world-level environment (sky, sun, ambient). Produced by the world from the
    // components on its world entity (i_world::world_entity()), consumed by renderers. Same contract as light_snapshot:
    // no references, safe to hold across the update/draw boundary, renderer-agnostic units
    struct environment_snapshot {

        // Procedural sky (world component "sky"). has_sky == false: the renderer clears to its background colour and
        // uses the hemisphere ambient below
        bool                            has_sky{ false };
        f32                             sky_intensity{ 1.f };               // sky radiance relative to the sun
        f32                             sky_haze{ 1.f };                    // aerosol (Mie) density scale
        f32                             sky_ground_albedo{ 0.3f };          // reflectance below the horizon
        f32                             sun_disk_size{ 0.53f };             // angular diameter in degrees, 0 = hidden

        // Ambient (world component "ambient_light"). Scales image based lighting when there is a sky; without a sky
        // it is a hemisphere: sky colour from above, ground colour from below
        f32                             ambient_intensity{ 1.f };
        glm::vec3                       ambient_sky_color{ 0.040f, 0.045f, 0.055f };
        glm::vec3                       ambient_ground_color{ 0.020f, 0.018f, 0.016f };

        // Sun: the directional light on the world entity, else the first directional light in the world
        bool                            has_sun{ false };
        glm::vec3                       sun_direction{ 0.f, -1.f, 0.f };    // direction the light travels (light_snapshot convention)
        glm::vec3                       sun_color{ 1.f };                   // linear RGB
        f32                             sun_intensity{ 1.f };
        entity_id                       sun_entity{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

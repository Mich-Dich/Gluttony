
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Directional is listed even though you scoped step 1 to point/spot. It costs nothing to reserve the value now,
    // and every renderer you'll ever write wants a sun. Drop it from your menu / UI for step 1 if you like; the enum
    // stays stable forever.
    enum class light_type : u8 {

        directional = 0,
        point,
        spot,
    };


    // Per-frame, copyable snapshot of a single light. Produced by the world (from a light entity's transform + `light`
    // component), consumed by the renderer. Contains no references; safe to hold across the update/draw boundary.
    //
    // Contract mirrors [camera_snapshot]:
    //   - world fills these, renderer copies them
    //   - neither side keeps a pointer into the other's storage
    //   - angles are radians (transform convention), not degrees (fov convention). Renderer converts to cosines.
    struct light_snapshot {

        glm::vec3                       position{ 0.f };                // world space; ignored by directional
        glm::vec3                       direction{ 0.f, -1.f, 0.f };    // world space, unit length; spot + directional only
        glm::vec3                       color{ 1.f };                   // linear RGB
        f32                             intensity{ 1.f };               // unitless; renderer maps to its shading model

        f32                             radius{ 10.f };                 // point + spot; ignored by directional
        f32                             inner_cone_angle{ 0.1f };       // radians; spot only
        f32                             outer_cone_angle{ 0.3f };       // radians; spot only

        light_type                      type{ light_type::point };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

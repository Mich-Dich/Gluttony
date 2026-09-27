
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Per-frame, copyable snapshot of the active camera. Produced by the world (from the active camera entity's transform + `camera` component)
    // and consumed by the renderer. Contains no references; safe to hold across the update/draw boundary
    struct camera_snapshot {

        glm::mat4                           view{ 1.f };            // world -> view space (i.e. inverse(world))
        glm::vec3                           position{ 0.f };        // world-space camera position
        f32                                 fov{ 45.f };            // vertical, degrees
        f32                                 near_plane{ 0.1f };
        f32                                 far_plane{ 100.f };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // How often an entity's world transform is expected to change. A PERFORMANCE HINT, never a lock:
    //
    //   fixed    - placed once (level geometry, props, most lights). The world caches its render data (instance / light
    //              snapshot) and only recomputes it when the entity is edited, so a frame costs nothing per fixed entity
    //   movable  - moves often (physics, scripts, animation). Recomputed every frame
    //
    // A fixed entity CAN still be moved (editor gizmo, i_world setters, scripts): the world patches its cached entry, which
    // costs about as much as one movable entity for that frame. Moving many fixed entities every frame works, it is just
    // the slow path. Mark those movable instead
    //
    // Effective mobility is inherited: a fixed child of a movable parent moves with it and is treated as movable. Physics
    // bodies with dynamic / kinematic motion are always treated as movable
    enum class mobility : u8 {

        fixed = 0,
        movable,
    };

    // CONSTANTS =======================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

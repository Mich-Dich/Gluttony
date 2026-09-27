
#pragma once

#include "asset/mesh.h"
#include "world/camera_snapshot.h" 



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Optional side-interface a world plugin can implement so the engine can pull a renderable snapshot out of it without knowing about ECS.
    //
    // Same discovery pattern as i_world_inspector:
    //     if (auto* scene = world->as<i_world_scene>()) { ... }
    //
    // Contract:
    //   - Called once per frame during the update phase, from the update thread.
    //   - [out] is cleared first; the implementation should reuse its capacity.
    //   - Must be safe to call while the renderer is *not* touching the world.
    //   - The returned handles are stable for the frame; the renderer will translate them to GPU resources itself.
    class i_world_scene {
    public:

        virtual ~i_world_scene() = default;


        virtual void gather_scene(std::vector<GLT::asset::mesh::instance>& out) const = 0;


        // Fill [out] from the active camera. Returns false if there is no active camera (the renderer keeps whatever view it last had)
        [[nodiscard]] virtual bool get_camera_view(camera_snapshot& out) const = 0;


        // Select which entity drives the view. INVALID_ENTITY disables camera tracking (the renderer freezes)
        virtual void set_active_camera(entity_id id) = 0;


        [[nodiscard]] virtual entity_id get_active_camera() const noexcept = 0;

    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

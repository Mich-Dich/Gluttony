
#pragma once

#include "asset/mesh.h"
#include "world/camera_snapshot.h" 
#include "world/light_snapshot.h"
#include "world/environment_snapshot.h"
#include "world/entity.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct entity_position {
        entity_id                                           id{};
        glm::vec3                                           world_position{ 0.f };
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Optional side-interface a world plugin can implement so the engine can pull a renderable snapshot out of it without
    // knowing about ECS
    //
    // Same discovery pattern as i_world_inspector:         if (auto* scene = world->as<i_world_scene>()) { ... }
    //
    // Contract:
    //   - Called once per frame during the update phase, from the update thread
    //   - [out] is cleared first; the implementation should reuse its capacity
    //   - Must be safe to call while the renderer is *not* touching the world
    //   - The returned handles are stable for the frame; the renderer will translate them to GPU resources itself
    class i_world_scene {
    public:

        virtual ~i_world_scene() = default;


        virtual void gather_scene(std::vector<GLT::asset::mesh::instance>& out) const = 0;


        // Zero-copy variant of gather_scene() + gather_lights(): refreshes the world's own render cache and returns views
        // into it. The spans stay valid until the next call or until the world changes. Layout:
        //
        //   instances = [ fixed (cached) ... | movable (recomputed this call) ... ]      first [fixed_instance_count]
        //   lights    = [ world sun (if any) | fixed ... | movable ... ]                 same rule
        //
        // [fixed_version] changes whenever any fixed entry changed (structure or a patched entity), so a consumer can keep
        // derived data (GPU buffers, sort keys) for the fixed prefix while it stays the same.
        // Returns false if the world has no cache; callers then use gather_scene() / gather_lights()
        struct scene_view {
            std::span<const GLT::asset::mesh::instance>     instances{};
            std::span<const GLT::world::light_snapshot>     lights{};
            u32                                             fixed_instance_count = 0;
            u32                                             fixed_light_count = 0;
            u64                                             fixed_version = 0;
        };
        [[nodiscard]] virtual bool view_scene(scene_view& /*out*/) { return false; }


        virtual void gather_lights(std::vector<GLT::world::light_snapshot>& out) const = 0;

        // World-level environment (sky, sun, ambient) from the world entity's components. [out] is reset first.
        // Worlds without a world entity keep the default (no sky, default ambient, sun = none)
        virtual void gather_environment(GLT::world::environment_snapshot& out) const { out = {}; }


        // World-space position of every entity that owns a transform. Used by editors that want a spatial overview (top-down views, minimaps)
        // without pulling the full render snapshot. Order is unspecified. [out] is cleared first
        virtual void gather_entity_positions(std::vector<entity_position>& out) const = 0;


        // Fill [out] from the active camera. Returns false if there is no active camera (the renderer keeps whatever view it last had)
        [[nodiscard]] virtual bool get_camera_view(camera_snapshot& out) const = 0;

        [[nodiscard]] virtual bool get_camera_view(world::entity_id camera, world::camera_snapshot& out) = 0;

        // Select which entity drives the view. INVALID_ENTITY disables camera tracking (the renderer freezes)
        virtual void set_active_camera(entity_id id) = 0;


        [[nodiscard]] virtual entity_id get_active_camera() const noexcept = 0;

    };

}

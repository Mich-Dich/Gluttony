
#pragma once

#include <glm/glm.hpp>

#include <asset/type.h>
#include <asset/asset_ref.h>
#include <asset/audio.h>
#include <world/entity.h>
#include <world/light_snapshot.h>

#include "component_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt::component {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Per-entity local transform. World transform is computed by walking the hierarchy on demand (see ecs_world_plugin::world_transform).
    struct transform {

        // glm::mat4                                   transform{ 1.f };
        glm::vec3                                   position{ 0.f };
        glm::vec3                                   rotation{ 0.f };   // euler, radians
        glm::vec3                                   scale{ 1.f };
    };
    static_assert(sizeof(transform) == 36);
    static_assert(std::is_trivially_copyable_v<transform>);



    // Editor-visible display name. Non-trivial (std::string), so it goes through the codec's custom serializer path.
    struct name {

        std::string                                 name;
    };
    static_assert(sizeof(name) == 32);



    // Parent + children links. Both sides are kept consistent by ecs_world_plugin::set_parent(); never mutate this directly.
    struct hierarchy {

        entity_id                                   parent{ INVALID_ENTITY };
        std::vector<entity_id>                      children;
    };
    static_assert(sizeof(hierarchy) == 32);



    // Renders a single mesh. material_override may be INVALID_HANDLE, in which case the renderer uses whatever the mesh's submeshes reference.
    struct mesh {

        GLT::asset::asset_ref                       mesh{};
        GLT::asset::asset_ref                       material_override{};
        bool                                        visible{ true };
    };


    // Makes this entity a camera. Position + rotation come from `transform` (like mesh_renderer); this holds only the projection parameters
    // A world can have many cameras; the active one is chosen via i_world_scene::set_active_camera()
    struct camera {
        f32                                         fov{ 45.f };
        f32                                         near_plane{ 0.1f };
        f32                                         far_plane{ 1000.f };
    };
    static_assert(std::is_trivially_copyable_v<camera>);


    // Makes this entity a light. Position and forward direction come from the entity's [transform] (same convention as camera)
    // A light with no transform sits at origin facing -Z
    struct light {

        glm::vec3                                   color{ 1.f, 1.f, 1.f };      // linear RGB
        f32                                         intensity{ 1.f };

        f32                                         radius{ 10.f };             // point + spot
        f32                                         inner_cone_angle{ 0.1f };   // radians, spot only
        f32                                         outer_cone_angle{ 0.3f };   // radians, spot only

        GLT::world::light_type                      type{ GLT::world::light_type::point };
    };
    static_assert(std::is_trivially_copyable_v<light>);


    // Plays a single clip. Looping / attenuation / etc. live in `config` so the entity doesn't need to copy them on spawn.
    struct audio_source {

        GLT::asset::asset_ref                       clip{};
        GLT::asset::audio::source_config            config{};
        bool                                        autoplay{ false };
    };


    // Marker: this entity's local transform is NOT combined with its parent's.
    // Useful for UI overlays, skyboxs, editor gizmos parented to something but meant to stay in world space.
    struct no_inherit_transform {};
    static_assert(std::is_trivially_copyable_v<no_inherit_transform>);

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    void register_all_component_descriptors(component_registry& reg);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}


#pragma once

#include <glm/glm.hpp>

#include "asset/type.h"
#include "asset/audio.h"
#include "world/entity.h"
#include "component_registry.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Per-entity local transform. World transform is computed by walking the hierarchy on demand (see ecs_world_plugin::world_transform).
    struct transform {

        glm::vec3                                   position{ 0.f };
        glm::vec3                                   rotation{ 0.f };   // euler, radians
        glm::vec3                                   scale{ 1.f };
    };
    static_assert(sizeof(transform) == 36);
    static_assert(std::is_trivially_copyable_v<transform>);



    // Editor-visible display name. Non-trivial (std::string), so it goes through the codec's custom serializer path.
    struct name_component {

        std::string                                 name;
    };
    static_assert(sizeof(name_component) == 32);



    // Parent + children links. Both sides are kept consistent by ecs_world_plugin::set_parent(); never mutate this directly.
    struct hierarchy {

        entity_id                                   parent{ INVALID_ENTITY };
        std::vector<entity_id>                      children;
    };
    static_assert(sizeof(hierarchy) == 32);



    // Renders a single mesh. material_override may be INVALID_HANDLE, in which case the renderer uses whatever the mesh's submeshes reference.
    struct mesh_renderer {

        GLT::asset::handle                          mesh{};
        GLT::asset::handle                          material_override{};
        bool                                        visible{ true };
        u8                                          _pad[7]{};
    };
    static_assert(sizeof(mesh_renderer) == 24);
    static_assert(std::is_trivially_copyable_v<mesh_renderer>);


    // Plays a single clip. Looping / attenuation / etc. live in `config` so the entity doesn't need to copy them on spawn.
    struct audio_source {

        GLT::asset::handle                          clip{};
        GLT::asset::audio::source_config            config{};
        bool                                        autoplay{ false };
        u8                                          _pad[7]{};
    };
    static_assert(sizeof(audio_source) == 80);
    static_assert(std::is_trivially_copyable_v<audio_source>);


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

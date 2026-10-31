#pragma once

#include <world/i_world_inspector.h>
#include <world/entity.h>
#include <world/component.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {
    class ecs_world;
}

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Per-world descriptor table. Each ecs_world owns exactly one of these. Descriptors are built once by
    // ecs_world_plugin::register_component_type() and replayed into each world by ecs_world::install_component_type().
    class component_registry {
    public:

        explicit component_registry(ecs_world& world) noexcept;


        void register_component(GLT::world::component::descriptor desc);


        void unregister_component(u64 hash);


        [[nodiscard]] std::span<const GLT::world::component::descriptor> all() const noexcept;


        [[nodiscard]] const GLT::world::component::descriptor* find(u64 hash) const noexcept;


        [[nodiscard]] std::vector<const GLT::world::component::descriptor*> on(entity_id id) const;


        [[nodiscard]] bool add(entity_id id, u64 hash);


        [[nodiscard]] bool remove(entity_id id, u64 hash);


        void copy(entity_id from, entity_id to);

    private:

        ecs_world&                                              m_world;
        std::vector<GLT::world::component::descriptor>           m_descriptors;
    };

}

// [component_registry.inl] is included at the bottom of [ecs_world.h].


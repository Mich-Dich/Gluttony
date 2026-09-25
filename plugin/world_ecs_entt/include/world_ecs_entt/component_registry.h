
#pragma once

#include <world/i_world_inspector.h>
#include <world/entity.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {
    class ecs_world_plugin;
}

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Registry that maps a component type to a descriptor. Owned by the
    // ecs_world_plugin. Registration is one entry per component type and
    // happens once during on_load().
    //
    // The registry knows the ECS (it holds a reference to the plugin), so
    // registration lambdas can be tiny - just name/category/draw. The
    // templated add<T>/remove<T>/has<T> are filled in by the helper.
    class component_registry {
    public:

        explicit component_registry(ecs_world_plugin& plugin) noexcept;


        template<typename T>
        void register_component(std::string_view name, std::string_view category, 
            std::function<void(entity_id)> draw, // may be empty (runtime-only)
            std::function<std::string(entity_id)> summary = {});


        // Descriptor access -------------------------------------------------------------------------------------------

        [[nodiscard]] std::span<const GLT::world::component_descriptor> all() const noexcept;


        [[nodiscard]] const GLT::world::component_descriptor* find(u64 hash) const noexcept;


        [[nodiscard]] std::vector<const GLT::world::component_descriptor*> on(entity_id id) const;

        // Mutation ----------------------------------------------------------------------------------------------------

        [[nodiscard]] bool add(entity_id id, u64 hash);


        [[nodiscard]] bool remove(entity_id id, u64 hash);


        void copy(entity_id from, entity_id to);

    private:

        ecs_world_plugin&                                       m_plugin;
        std::vector<GLT::world::component_descriptor>           m_descriptors;
    };

}

// NOTE: [component_registry.inl] is included at the bottom of [world.h], after ecs_world_plugin has been fully defined.
// Including it here would only see a forward declaration and fail to compile the lambdas.

#pragma once

#include "entity_codec.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world::world_ecs_entt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    template<typename T>
    void component_registry::register_component(std::string_view name, std::string_view category,
        std::function<void(ecs_world&, entity_id)> draw, std::function<std::string(ecs_world&, entity_id)> summary, 
        std::vector<u64> required) {

        static_assert(std::is_default_constructible_v<T>,
            "Registered components must be default-constructible for the \"Add Component\" menu.");

        GLT::world::component_descriptor descriptor{};
        descriptor.hash = component_name_hash(name);
        descriptor.name = name;
        descriptor.category = category;
        descriptor.can_remove = [](entity_id) { return true; };
        descriptor.required = std::move(required);

        descriptor.has = [this](entity_id id) {
            const auto entity = m_world.entt_of(id);
            return entity != entt::null && m_world.registry().all_of<T>(entity);
        };

        descriptor.add = [this](entity_id id) {
            const auto entity = m_world.entt_of(id);
            if (entity != entt::null)
                m_world.registry().emplace_or_replace<T>(entity);
        };

        descriptor.remove = [this](entity_id id) {
            const auto entity = m_world.entt_of(id);
            if (entity != entt::null)
                m_world.registry().remove<T>(entity);
        };

        // Wrap the user's lambdas so the shared descriptor-field type is preserved.
        if (draw)
            descriptor.draw = [this, fn = std::move(draw)](entity_id id) { fn(m_world, id); };

        if (summary)
            descriptor.summary = [this, fn = std::move(summary)](entity_id id) { return fn(m_world, id); };

        m_descriptors.push_back(std::move(descriptor));
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}


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

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    template<typename T>
    void component_registry::register_component(std::string_view name, std::string_view category, std::function<void(entity_id)> draw,
        std::function<std::string(entity_id)> summary, std::vector<u64> required) {

        static_assert(std::is_default_constructible_v<T>, "Registered components must be default-constructible for the \"Add Component\" menu.");

        GLT::world::component_descriptor descriptor{
            .hash = component_name_hash(name),
            .name = name,
            .category = category,
    
            .has = [this](entity_id id) {
                const auto entity = m_plugin.entt_of(id);
                return entity != entt::null && m_plugin.registry().all_of<T>(entity);
            },
    
            .add = [this](entity_id id) {
                const auto entity = m_plugin.entt_of(id);
                if (entity != entt::null)
                    m_plugin.registry().emplace_or_replace<T>(entity);
            },
    
            .draw = std::move(draw),
            .can_remove = [](entity_id) { return true; },
    
            .remove = [this](entity_id id) {
                const auto entity = m_plugin.entt_of(id);
                if (entity != entt::null)
                    m_plugin.registry().remove<T>(entity);
            },
    
            .summary = std::move(summary),
            .required = std::move(required),
        };

        m_descriptors.push_back(std::move(descriptor));
    }

}

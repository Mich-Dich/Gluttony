
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
        std::function<std::string(entity_id)> summary) {


        static_assert(std::is_default_constructible_v<T>, "Registered components must be default-constructible for the "
            "\"Add Component\" menu.");

        GLT::world::component_descriptor descriptor{};

        descriptor.hash = component_name_hash(name);
        descriptor.name = name;
        descriptor.category = category;
        descriptor.draw = std::move(draw);
        descriptor.summary = std::move(summary);

        descriptor.has = [this](entity_id id) {
            const auto entity = m_plugin.entt_of(id);
            return entity != entt::null && m_plugin.registry().all_of<T>(entity);
        };

        descriptor.add = [this](entity_id id) {
            const auto entity = m_plugin.entt_of(id);
            if (entity != entt::null)
                m_plugin.registry().emplace_or_replace<T>(entity);
        };

        descriptor.remove = [this](entity_id id) {
            const auto entity = m_plugin.entt_of(id);
            if (entity != entt::null)
                m_plugin.registry().remove<T>(entity);
        };

        descriptor.can_remove = [](entity_id) { return true; };

        m_descriptors.push_back(std::move(descriptor));
    }

}

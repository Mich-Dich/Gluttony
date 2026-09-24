
#pragma once

// This file is #included at the bottom of ecs_world_plugin.h. It relies on ecs_world_plugin being fully defined by the time it's reached,
// so it must not be included independently.

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

    template<typename T, typename... Args>
    entity_builder& entity_builder::add(Args&&... args) {

        auto& reg = m_plugin->registry();
        reg.emplace_or_replace<T>(m_plugin->entt_of(m_id), std::forward<Args>(args)...);
        return *this;
    }


    template<typename T>
    entity_builder& entity_builder::add_or_replace(T&& component) {

        auto& reg = m_plugin->registry();
        reg.emplace_or_replace<T>(m_plugin->entt_of(m_id), std::forward<T>(component));
        return *this;
    }


    template<typename T>
    entity_builder& entity_builder::remove() {

        m_plugin->registry().remove<T>(m_plugin->entt_of(m_id));
        return *this;
    }


    template<typename T>
    bool entity_builder::has() const { return m_plugin->registry().all_of<T>(m_plugin->entt_of(m_id)); }


    template<typename T>
    T& entity_builder::get() { return m_plugin->registry().get<T>(m_plugin->entt_of(m_id)); }


    template<typename T>
    const T& entity_builder::get() const { return m_plugin->registry().get<T>(m_plugin->entt_of(m_id)); }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}

#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::script {

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

    // session_resource ------------------------------------------------------------------------------------------------

    template <typename T>
    T& script_context::session_resource(std::string_view key) { return session_resource<T>(key, [] { return T{}; }); }


    template <typename T, typename Factory>
    T& script_context::session_resource(std::string_view key, Factory&& factory) {

        using F = std::remove_reference_t<Factory>;
        F f = std::forward<Factory>(factory);
        void* p = m_host->session_resource_erased(key, reflect::fnv1a_32(reflect::type_name(^^T)), sizeof(T), alignof(T),
            [](void* out, void* fp) { new (out) T((*static_cast<F*>(fp))()); }, &f, m_registrant);
        return *static_cast<T*>(p);
    }

    // sibling ---------------------------------------------------------------------------------------------------------

    template <typename S>
    S* script_context::sibling(std::string_view instance_name) noexcept {

        if (!m_host)
            return nullptr;
        return static_cast<S*>(m_host->find_sibling_erased(
            reflect::type_name(^^S), m_entity, instance_name, m_registrant));
    }

    // sleep_until_event -----------------------------------------------------------------------------------------------

    template <typename E>
    void script_context::sleep_until_event() noexcept {

        if (m_host) m_host->sleep_until_event_erased(
            reflect::fnv1a_32(reflect::type_name(^^E)));
    }

    // wake_sibling ----------------------------------------------------------------------------------------------------

    template <typename S>
    void script_context::wake_sibling(std::string_view instance_name) noexcept {

        if (m_host)
            m_host->wake_sibling_erased(reflect::type_name(^^S), m_entity, instance_name);
    }
    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}

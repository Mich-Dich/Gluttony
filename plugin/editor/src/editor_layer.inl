#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

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

    template<typename window_type, typename... args>
    void editor_layer::add_window(args&&... arguments) {

        static_assert(std::is_base_of<base_window, window_type>::value, "[window_type] must derive from editor_window");
        auto window = GLT::create_unique_ref<window_type>(std::forward<args>(arguments)...);
        LOG(trace, "Created new window [{}]", window->get_window_title())
        if (m_dockspace_id != 0)
            window->dock_to(m_dockspace_id);
        m_windows.emplace_back(std::move(window));
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}

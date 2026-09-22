#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

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

    template<typename controller_type, typename... args>
    requires std::derived_from<controller_type, GLT::world::controller>
    void world_layer::set_controller(args&&... arguments) {
        
        static_assert(std::is_base_of<GLT::world::controller, controller_type>::value, "[controller_type] must derive from GLT::world::controller");
        auto ctrl = GLT::create_ref<controller_type>(std::forward<args>(arguments)...);
        VALIDATE(ctrl, return, "Set controller", "Failed to create controller")

        m_controller = std::move(ctrl);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}

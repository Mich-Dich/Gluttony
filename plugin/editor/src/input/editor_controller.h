
#pragma once

#include <world/controller.h>
#include <input_manager_default/controller.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {
    class camera;
}

namespace GLT::editor::input {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class editor_controller final : public GLT::input::input_manager_default::controller {
    public:

        editor_controller();
        ~editor_controller();

        void update(const f32 delta_time) override;

    private:

        f32                                 m_move_speed = .15f;
        handle                              m_move_action_handle = INVALID_HANDLE;
        handle                              m_look_action_handle = INVALID_HANDLE;
        handle                              m_scroll_action_handle = INVALID_HANDLE;
        GLT::ref<GLT::world::camera>        m_camera{};
    };

}

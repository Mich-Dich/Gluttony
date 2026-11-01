
#pragma once

#include "../../core/src/util/timing/interval_controller.h"

#include "UI.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::launcher {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class app {
    public:

        app();

        ~app();

        FORCE_INLINE_R static app& get() { return *s_instance; }

        void run();

    private:

        void init_window();

        void shutdown_window();


        static app*                                                 s_instance;
        bool                                                        m_running = true;
        version                                                     m_version{};
        GLT::platform::window_handle                                m_main_window{};
        util::interval_controller                                   m_fps_controller{};
        f32                                                         m_delta_time = 0.f;
        f32                                                         m_total_time = 0.f;
        UI                                                          m_ui;

    };

}

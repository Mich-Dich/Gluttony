
#pragma once

#include "util/timing/interval_controller.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {
    class i_window_plugin;
}
namespace GLT::render {
    class i_renderer_plugin;
}


namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class application {
    public:
        
        application(int argc, char* argv[]);
        ~application();

        GETTER(ref<GLT::platform::i_window_plugin>,     window, mp_window)

        FORCE_INLINE_R static application& get()	    { return *s_instance; }

        void run();
        
        void set_target_fps(const f32 fps);

    private:
        
        static application*			                    s_instance;
        version                                         m_version{};
        ref<GLT::platform::i_window_plugin>             mp_window{};
        ref<GLT::render::i_renderer_plugin>             mp_renderer{};
        bool                                            m_running = true;
        util::interval_controller                       m_fps_controller{};

    };

}

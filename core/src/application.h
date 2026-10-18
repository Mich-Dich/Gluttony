
#pragma once

#include "util/timing/interval_controller.h"
#include "config/project.h"

#include "debug/profiler.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {
    class i_window_plugin;
}

namespace GLT::render {
    class i_renderer_plugin;
}

namespace GLT::audio {
    class i_audio_plugin;
}

namespace GLT {
    class window_close_event;
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

        application(const std::filesystem::path& project_path);
        ~application();

        DEFAULT_GETTER_REF(layer_stack,                 layer_stack)
        DEFAULT_GETTER_CC(util::interval_controller,    fps_controller)
        DEFAULT_GETTER_CC(f32,                          delta_time)
        DEFAULT_SETTER(f32,                             delta_time)
        DEFAULT_GETTER_CC(f32,                          total_time)
        DEFAULT_SETTER(f32,                             total_time)
        DEFAULT_GETTER_C(project,                       project)
        DEFAULT_GETTER_REF(project,                     project)
        GETTER(std::filesystem::path,                   project_path,   m_project.project_path)

        FORCE_INLINE_R static application& get()	    { return *s_instance; }

        void run();

        void load_world(const std::filesystem::path& world_path, const bool override_current);

        void set_target_fps(const f32 fps);

        void shutdown();

    private:

        static application*			                    s_instance;
        version                                         m_version{};
        util::interval_controller                       m_fps_controller{};
        layer_stack                                     m_layer_stack{};
        u32                                             m_focus_fps = 60;
        u32                                             m_none_focus_fps = 30;
        f32                                             m_delta_time = 0.f;
        f32                                             m_total_time = 0.f;
        project                                         m_project{};
        debug::application_stats                        m_application_stats{};

    };

}

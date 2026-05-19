
#include "util/pch.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "plugin_system/plugin_manager.h"
#include "plugin_system/i_window_plugin.h"
#include "plugin_system/i_renderer_plugin.h"

#include "application.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    application*                application::s_instance = nullptr;

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    application::application(int argc, char* argv[]) {

        // PROFILE_APPLICATION_FUNCTION();
        ASSERT(!s_instance, "", "Application already exists");
        s_instance = this;
        
        platform::window_attributes attributes;
        config::serialize_window_attributes(attributes, serializer::option::load_from_file);

        plugin_manager::load_plugins(plugin_manager::load_phase::pre_application);
        mp_window = plugin_manager::get_plugin_ref<platform::i_window_plugin>(plugin_manager::targeted_interface::window);
        mp_window->create(attributes);

        plugin_manager::load_plugins(plugin_manager::load_phase::post_window);
        mp_renderer = plugin_manager::get_plugin_ref<render::i_renderer_plugin>(plugin_manager::targeted_interface::renderer);
        mp_renderer->create();

        set_target_fps(30);         // DEBUG-ONLY - TODO: load from config
        const auto unused_handle = event_bus::subscribe<window_close_event>([this](window_close_event& event) {
            m_running = false;
        });

        LOG_INIT
        plugin_manager::load_plugins(plugin_manager::load_phase::post_application_run);
    }


    application::~application() {

        plugin_manager::load_plugins(plugin_manager::load_phase::pre_application_shutdown);

        mp_renderer->destroy();

        platform::window_attributes attributes = mp_window->get_window_attributes();
        config::serialize_window_attributes(attributes, serializer::option::save_to_file);
        mp_window->destroy();
        mp_window.reset();
        
        plugin_manager::load_plugins(plugin_manager::load_phase::post_application_shutdown);
        s_instance = nullptr;
        LOG_SHUTDOWN
    }

    // CLASS PUBLIC ====================================================================================================

    void application::run() {

        plugin_manager::load_plugins(plugin_manager::load_phase::pre_application_run);
        mp_window->show(true);       // show window now

        while (m_running) {

            mp_window->poll_events();        // update internal state

            // update the layerStack

            mp_renderer->begin_frame();
            // render the layerStack
            mp_renderer->end_frame();
            
            m_fps_controller.limit();
        }
        plugin_manager::load_plugins(plugin_manager::load_phase::post_application_run);
    }


    void application::set_target_fps(const f32 fps) {

        const u64 time = (1 / fps) * 1000000;
        m_fps_controller.set_target_interval_duration(std::chrono::microseconds(time));
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

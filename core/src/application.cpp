
#include "util/pch.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "plugin_system/plugin_manager.h"
#include "plugin_system/i_window_plugin.h"
#include "plugin_system/i_renderer_plugin.h"
#include "plugin_system/i_game_loop_base.h"
#include "config/imgui_config.h"

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
        config::serialize_window_attributes(attributes, serializer::option::load);
        imgui_config::init();

        plugin_manager::load_plugins(plugin_manager::load_phase::pre_application);
        mp_window = plugin_manager::get_plugin_ref<platform::i_window_plugin>(plugin_manager::interface::window);
        ASSERT(mp_window, "", "Failed to load window plugin")
        mp_window->create(attributes);

        plugin_manager::load_plugins(plugin_manager::load_phase::post_window);
        mp_renderer = plugin_manager::get_plugin_ref<render::i_renderer_plugin>(plugin_manager::interface::renderer);
        ASSERT(mp_renderer, "", "Failed to load render plugin")
        mp_renderer->create();

        set_target_fps(30);                 // DEBUG-ONLY - TODO: load from config

        m_close_event_sub_handle = event_bus::subscribe<window_close_event>(std::bind_front(&application::on_window_close_event, this));

        mp_game_loop_base = plugin_manager::get_plugin_ref<i_game_loop_base>(plugin_manager::interface::game_loop);

        LOG_INIT
        plugin_manager::load_plugins(plugin_manager::load_phase::application_ready);
    }


    application::~application() {

        event_bus::unsubscribe(m_close_event_sub_handle);

        plugin_manager::load_plugins(plugin_manager::load_phase::pre_application_shutdown);

        mp_renderer->destroy();

        platform::window_attributes attributes = mp_window->get_window_attributes();
        config::serialize_window_attributes(attributes, serializer::option::save);
        mp_window->destroy();
        mp_window.reset();
        
        imgui_config::shutdown();
        plugin_manager::load_plugins(plugin_manager::load_phase::post_application_shutdown);
        s_instance = nullptr;
        LOG_SHUTDOWN
    }

    // CLASS PUBLIC ====================================================================================================

    void application::run() {

        plugin_manager::load_plugins(plugin_manager::load_phase::pre_application_run);
        mp_window->show(true);              // show window now

        while (m_running) {

            mp_window->poll_events();       // update internal state

            for (auto layer = m_layer_stack.begin(); layer != m_layer_stack.end(); )
                (*--layer)->update(m_delta_time);

            GLT::event_bus::post<update_event>(m_delta_time);       // all systems can subscribe to this (eg: plugins)
            
            mp_renderer->begin_frame();     // start frame + start imgui frame
            for (auto layer = m_layer_stack.end(); layer != m_layer_stack.begin(); )
                (*--layer)->render_imgui(m_delta_time);
            mp_renderer->draw_frame();      // finish imgui stuff and render world

            m_delta_time = m_fps_controller.limit();
        }
        plugin_manager::load_plugins(plugin_manager::load_phase::post_application_run);
    }


    void application::set_target_fps(const f32 fps) {

        const u64 time = (1 / fps) * 1000000;
        m_fps_controller.set_target_interval_duration(std::chrono::microseconds(time));
    }


    void application::on_window_close_event(const window_close_event& event) {

        m_running = false;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

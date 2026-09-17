
#include "util/pch.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "plugin_system/plugin_manager.h"
#include "plugin_system/i_window_plugin.h"
#include "plugin_system/i_renderer_plugin.h"
#include "plugin_system/i_game_loop_base.h"
#include "plugin_system/i_audio_plugin.h"
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

    application::application(const std::filesystem::path& project_path) {

        // PROFILE_APPLICATION_FUNCTION();
        ASSERT(!s_instance, "", "Application already exists");
        s_instance = this;

        thread_pool::init();

        m_project.serialize_projects_data(project_path, GLT::serializer::option::load);
        set_target_fps(30);                 // DEBUG-ONLY - TODO: load from config
        imgui_config::init();

        plugin_manager::load_plugins(plugin_manager::phase::pre_application);
        plugin_manager::unload_plugins(plugin_manager::phase::pre_application);

        mp_window = plugin_manager::get_plugin_ref<platform::i_window_plugin>(plugin_manager::interface::window);
        ASSERT(mp_window, "", "Failed to load window plugin")
        platform::window_attributes attributes;
        platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::load);
        mp_window->create(attributes);

        plugin_manager::load_plugins(plugin_manager::phase::post_window);
        plugin_manager::unload_plugins(plugin_manager::phase::post_window);

        mp_audio = plugin_manager::get_plugin_ref<GLT::audio::i_audio_plugin>(plugin_manager::interface::audio);
        ASSERT(mp_audio, "", "Failed to load audio plugin")
        mp_audio->create();

        mp_renderer = plugin_manager::get_plugin_ref<render::i_renderer_plugin>(plugin_manager::interface::renderer);
        ASSERT(mp_renderer, "", "Failed to load render plugin")
        mp_renderer->create();

        plugin_manager::load_plugins(plugin_manager::phase::application_ready);
        plugin_manager::unload_plugins(plugin_manager::phase::application_ready);

        mp_game_loop_base = plugin_manager::get_plugin_ref<i_game_loop_base>(plugin_manager::interface::game_loop);

        m_close_event_sub_handle = event_bus::subscribe<window_close_event>(std::bind_front(&application::on_window_close_event, this));
        LOG_INIT
    }


    application::~application() {

        event_bus::unsubscribe(m_close_event_sub_handle);

        plugin_manager::load_plugins(plugin_manager::phase::pre_application_shutdown);
        plugin_manager::unload_plugins(plugin_manager::phase::pre_application_shutdown);

        mp_renderer->destroy();
        mp_renderer.reset();
        mp_audio->destroy();
        mp_audio.reset();

        platform::window_attributes attributes = mp_window->get_window_attributes();
        platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::save);
        mp_window->destroy();
        mp_window.reset();

        imgui_config::shutdown();
        plugin_manager::load_plugins(plugin_manager::phase::post_application_shutdown);
        plugin_manager::unload_plugins(plugin_manager::phase::post_application_shutdown);

        thread_pool::wait_for_all();                 // drain any stragglers
        thread_pool::pump_main_thread();             // drain callbacks posted during the wait
        thread_pool::shutdown();                     // join workers

        s_instance = nullptr;
        LOG_SHUTDOWN
    }

    // CLASS PUBLIC ====================================================================================================

    void application::run() {

        plugin_manager::load_plugins(plugin_manager::phase::pre_application_run);
        plugin_manager::unload_plugins(plugin_manager::phase::pre_application_run);
        mp_window->show(true);                                              // show window now

        while (m_running) {

            // update --------------------------------------------------------------------------------------------------
            mp_window->poll_events();                                       // update internal state
            thread_pool::pump_main_thread();                                // run deferred UI updates
            for (auto layer = m_layer_stack.end(); layer != m_layer_stack.begin(); )
                (*--layer)->update(m_delta_time);
            GLT::event_bus::post<update_event>(m_delta_time);               // all systems can subscribe to this (eg: plugins)            
            mp_audio->update_3d_audio();

            // draw ----------------------------------------------------------------------------------------------------
            mp_renderer->begin_frame();                                     // start frame + start imgui frame
            for (auto layer = m_layer_stack.begin(); layer != m_layer_stack.end(); )
                (*layer++)->render_imgui(m_delta_time);
            mp_renderer->draw_frame();                                      // finish imgui stuff and render world

            // stats ---------------------------------------------------------------------------------------------------
            m_delta_time = m_fps_controller.limit();
            m_application_stats.frame_time_ms = m_delta_time;
            m_application_stats.cpu_time_ms = 0.f;                          // TODO: set value
            m_application_stats.fps = (m_delta_time > 0.0f) ? (1000.0f / m_delta_time) : 0.0f;
            m_application_stats.render = mp_renderer->get_render_stats();
            debug::update_app_stats(m_application_stats);
        }

        plugin_manager::load_plugins(plugin_manager::phase::post_application_run);
        plugin_manager::unload_plugins(plugin_manager::phase::post_application_run);
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

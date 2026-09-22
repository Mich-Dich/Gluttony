
#include "util/pch.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "plugin_system/plugin_manager.h"
#include "plugin_system/i_window_plugin.h"
#include "plugin_system/i_renderer_plugin.h"
#include "plugin_system/i_game_loop_plugin.h"
#include "plugin_system/i_audio_plugin.h"
#include "config/imgui_config.h"
#include "world/world_layer.h"

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

        m_project.serialize_projects_data(project_path, GLT::serializer::option::load);
        set_target_fps(60);                                             // DEBUG-ONLY - TODO: load from config
        imgui_config::init();

        m_layer_stack.push_layer<GLT::world::world_layer>();            // first layer is the game world

        plugin_manager::load_plugins(plugin_manager::phase::pre_application);
        plugin_manager::unload_plugins(plugin_manager::phase::pre_application);

        mp_window = GLT::platform::get_window_ref();
        ASSERT(mp_window, "", "Failed to load window plugin")
        platform::window_attributes attributes;
        platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::load);
        mp_window->create(attributes);

        mp_audio = GLT::audio::manager::get_ref();
        ASSERT(mp_audio, "", "Failed to load audio plugin")
        mp_audio->create();

        mp_renderer = GLT::render::renderer::get_ref();
        ASSERT(mp_renderer, "", "Failed to load render plugin")
        mp_renderer->create();

        plugin_manager::load_plugins(plugin_manager::phase::application_ready);
        plugin_manager::unload_plugins(plugin_manager::phase::application_ready);

        LOG_INIT
    }


    application::~application() {

        plugin_manager::load_plugins(plugin_manager::phase::pre_application_shutdown);
        plugin_manager::unload_plugins(plugin_manager::phase::pre_application_shutdown);

        mp_renderer->destroy();
        mp_audio->destroy();

        platform::window_attributes attributes = mp_window->get_window_attributes();
        platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::save);
        mp_window->destroy();

        imgui_config::shutdown();
        plugin_manager::load_plugins(plugin_manager::phase::post_application_shutdown);
        plugin_manager::unload_plugins(plugin_manager::phase::post_application_shutdown);

        s_instance = nullptr;
        LOG_SHUTDOWN
    }

    // CLASS PUBLIC ====================================================================================================

    void application::run() {

        plugin_manager::load_plugins(plugin_manager::phase::pre_application_run);
        plugin_manager::unload_plugins(plugin_manager::phase::pre_application_run);

        auto game_loop = GLT::game_loop::get_ref();
        ASSERT(game_loop, "", "Failed to load game_loop plugin");
        auto close_sub = event_bus::subscribe_scoped<window_close_event>(           // unsubscribes automatically, even on exception
            [game_loop](const window_close_event&) { game_loop->request_stop(); }
        );

        game_loop::context ctx{
            m_delta_time,
            m_layer_stack,
            mp_window,
            mp_renderer,
            mp_audio,
            m_fps_controller,
            m_application_stats,
        };
        game_loop->init(ctx);
        game_loop->run(ctx);
        game_loop->shutdown(ctx);

        plugin_manager::load_plugins(plugin_manager::phase::post_application_run);
        plugin_manager::unload_plugins(plugin_manager::phase::post_application_run);
    }


    void application::set_target_fps(const f32 fps) {

        const u64 time = (1 / fps) * 1000000;
        m_fps_controller.set_target_interval_duration(std::chrono::microseconds(time));
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

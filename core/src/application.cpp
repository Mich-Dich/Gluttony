
#include "util/pch.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "render/i_renderer.h"
#include "plugin_system/plugin_manager.h"
#include "platform/i_window.h"
#include "plugin_system/i_game_loop_plugin.h"
#include "plugin_system/i_audio_plugin.h"
#include "config/imgui_config.h"
#include "asset/i_asset_registry.h"
#include "world/world_layer.h"
#include "world/i_world.h"

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

        ASSERT(!s_instance, "", "Application already exists");
        s_instance = this;

        m_project.serialize_projects_data(project_path, GLT::serializer::option::load);
        set_target_fps(60);
        imgui_config::init();

        plugin_manager::enter_phase(plugin_manager::phase::pre_application);

        m_layer_stack.push_layer<GLT::world::world_layer>();

        auto* window = GLT::plugin_manager::get_plugin_raw<GLT::platform::i_window_plugin>(GLT::plugin_manager::interface::window);
        ASSERT(window, "", "Failed to load window plugin")
        platform::window_attributes attributes;
        platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::load);
        window->create(attributes);

        auto* audio = GLT::plugin_manager::get_plugin_raw<GLT::audio::i_audio_plugin>(GLT::plugin_manager::interface::audio);
        ASSERT(audio, "", "Failed to load audio plugin")
        audio->create();

        auto* renderer = GLT::plugin_manager::get_plugin_raw<GLT::render::i_renderer_plugin>(GLT::plugin_manager::interface::renderer);
        ASSERT(renderer, "", "Failed to load render plugin")
        renderer->create();

        plugin_manager::enter_phase(plugin_manager::phase::application_ready);

        LOG_INIT
    }


    application::~application() {

        plugin_manager::enter_phase(plugin_manager::phase::pre_application_shutdown);

        auto* renderer = GLT::plugin_manager::get_plugin_raw<GLT::render::i_renderer_plugin>(GLT::plugin_manager::interface::renderer);
        auto* audio = GLT::plugin_manager::get_plugin_raw<GLT::audio::i_audio_plugin>(GLT::plugin_manager::interface::audio);
        auto* window = GLT::plugin_manager::get_plugin_raw<GLT::platform::i_window_plugin>(GLT::plugin_manager::interface::window);

        if (renderer)
            renderer->destroy();

        if (audio)
            audio->destroy();

        if (window) {
            auto attributes = window->get_window_attributes();
            platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::save);
            window->destroy();
        }

        m_project.serialize_projects_data(GLT::serializer::option::save);
        imgui_config::shutdown();

        plugin_manager::enter_phase(plugin_manager::phase::post_application_shutdown);

        s_instance = nullptr;
        LOG_SHUTDOWN
    }

    // CLASS PUBLIC ====================================================================================================

    void application::run() {

        plugin_manager::enter_phase(plugin_manager::phase::pre_application_run);

        auto* game_loop = GLT::game_loop::get_ptr();
        ASSERT(game_loop, "", "Failed to load game_loop plugin");

        auto close_sub = event_bus::subscribe_scoped<window_close_event>(
            [game_loop](const window_close_event&) { game_loop->request_stop(); }
        );

        game_loop::context ctx{
            m_delta_time,
            m_total_time,
            m_layer_stack,
            m_fps_controller,
            m_application_stats,
        };

        game_loop->init(ctx);
        game_loop->run(ctx);
        game_loop->shutdown(ctx);

        plugin_manager::enter_phase(plugin_manager::phase::post_application_run);
    }


    void application::load_world(const std::filesystem::path& world_path, const bool override_current) {

        auto* world_layer = m_layer_stack.get<GLT::world::world_layer>();
        VALIDATE(world_layer, return, "", "Failed to get world layer from layer stack")
        
        auto world = world_layer->get_world();
        VALIDATE(world, return, "", "Failed to get world from world layer")

        // TODO: check the world for real and 
        if (!override_current)                         // already set, dont override
            return;

        auto registry = GLT::asset::registry::get_ref();
        VALIDATE(registry, return, "", "Failed to get asset-registry")

        auto result = registry->load(world_path);
        VALIDATE(result, return, "", "Registry failed to load world [{}]", (world_path.generic_string()))

        const auto world_result = world->load_world(*result);
        VALIDATE(world_result, return, "", "Failed to load world")
    }


    void application::set_target_fps(const f32 fps) {

        const u64 time = (1 / fps) * 1000000;
        m_fps_controller.set_target_interval_duration(std::chrono::microseconds(time));
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

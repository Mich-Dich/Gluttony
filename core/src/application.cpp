
#include "util/pch.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "render/i_renderer.h"
#include "render/i_scene_renderer.h"
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

        auto* p_window = GLT::platform::window::get_ptr();
        ASSERT(p_window, "", "Failed to load window plugin")
        platform::window_attributes attributes;
        platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::load);
        p_window->create(attributes);

        auto* p_renderer = GLT::render::renderer::get_ptr();
        ASSERT(p_renderer, "", "Failed to load render plugin")
        p_renderer->create();

        auto* audio = GLT::audio::manager::get_ptr();
        ASSERT(audio, "", "Failed to load audio plugin")
        audio->create();

        // surface scene renderer: ALWAYS created, owns ImGui + presentation
        {
            GLT::render::surface_target_desc surface{
                .window = GLT::platform::window::get_ref(),
            };
            GLT::render::scene_renderer_desc desc{
                .target = surface,
                .clear_color = glm::vec4(0.09f, 0.09f, 0.09f, 1.f),
            };
            auto primary = p_renderer->create_scene_renderer(desc);
            if (primary)
                m_scene_renderers.push_back(std::move(*primary));
            else
                LOG(error, "Failed to create surface scene renderer: {}", primary.error().message)
        }

        plugin_manager::enter_phase(plugin_manager::phase::application_ready);
        
        // offscreen scene renderer: only when the editor exists
        if (GLT::plugin_manager::get_plugin_raw<plugin_manager::i_plugin>(plugin_manager::interface::editor_core) != nullptr) {

            m_scene_renderers.back()->set_active(false);    // Use surface scene renderer only for ImGui

            GLT::render::offscreen_target_desc offscreen{};
            GLT::render::scene_renderer_desc desc{
                .target = offscreen,
                .clear_color = glm::vec4(0.09f, 0.09f, 0.09f, 1.f),
            };
            auto viewport = p_renderer->create_scene_renderer(desc);
            if (viewport)
                m_scene_renderers.push_back(std::move(*viewport));
            else
                LOG(error, "Failed to create offscreen scene renderer: {}", viewport.error().message)
        }

        LOG_INIT
    }


    application::~application() {

        plugin_manager::enter_phase(plugin_manager::phase::pre_application_shutdown);

        auto* p_renderer = GLT::render::renderer::get_ptr();
        auto* p_audio = GLT::audio::manager::get_ptr();
        auto* p_window = GLT::platform::window::get_ptr();

        for (auto it = m_scene_renderers.rbegin(); it != m_scene_renderers.rend(); it++)
            it->reset();
        m_scene_renderers.clear();

        if (p_renderer)
            p_renderer->destroy();

        if (p_audio)
            p_audio->destroy();

        if (p_window) {
            auto attributes = p_window->get_window_attributes();
            platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::save);
            p_window->destroy();
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


    GLT::render::i_scene_renderer* application::primary_scene_renderer() noexcept {

        return m_scene_renderers.empty() ? nullptr : m_scene_renderers.front().get();
    }


    GLT::render::i_scene_renderer* application::get_main_world_scene_renderer() noexcept {

        for (auto& scene_renderer : m_scene_renderers)
            if (scene_renderer && scene_renderer->is_offscreen())
                return scene_renderer.get();
        return nullptr;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

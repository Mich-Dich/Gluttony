
#include "util/pch.h"
#include "application.h"

#include "event/event_bus.h"
#include "event/application_event.h"
#include "render/i_renderer.h"
#include "plugin_system/plugin_manager.h"
#include "platform/i_window.h"
#include "plugin_system/i_game_loop_plugin.h"
#include "plugin_system/i_audio_plugin.h"
#include "config/imgui_config.h"
#include "asset/i_asset_registry.h"
#include "world/i_world.h"
#include "physics/i_physics_plugin.h"
#include "game/game_layer.h"
#include "game/game_session.h"



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

        m_layer_stack.push_layer<GLT::game::game_layer>();

        auto* p_window = GLT::platform::window::get_ptr();
        ASSERT(p_window, "", "Failed to load window plugin")
        platform::window_attributes attributes;
        platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::load);
        m_main_window = p_window->create(attributes);

        auto* p_renderer = GLT::render::renderer::get_ptr();
        ASSERT(p_renderer, "", "Failed to load render plugin")
        p_renderer->create(m_main_window);

        auto* p_audio = GLT::audio::manager::get_ptr();
        ASSERT(p_audio, "", "Failed to load audio plugin")
        p_audio->create();

        {
            GLT::render::surface_target_desc surface{
                .window = m_main_window
            };
            GLT::render::scene_renderer_desc desc{
                .target = surface,
                .clear_color = glm::vec4(0.09f, 0.09f, 0.09f, 1.f),
            };
            (void)create_scene_renderer(desc);
        }

        if (auto* p_physics = GLT::physics::get_ptr()) {
            auto result = p_physics->init();
            VALIDATE(result, , "Physics backend: {}", (p_physics->backend_name()),
                "Failed to init physics plugin: error {}", (static_cast<int>(result.error())))
        }

        plugin_manager::enter_phase(plugin_manager::phase::application_ready);

        LOG_INIT
    }


    application::~application() {

        // world is owned by game_layer -> primary_session -> i_world. Dropping layer-stack releases the world (and its
        // region handles) before plugins unload
        m_layer_stack.clear();

        if (auto* physics = GLT::physics::get_ptr())
            physics->shutdown();

        plugin_manager::enter_phase(plugin_manager::phase::pre_application_shutdown);

        for (auto it = m_scene_renderers.rbegin(); it != m_scene_renderers.rend(); it++)
        it->reset();
        m_scene_renderers.clear();

        auto* p_renderer = GLT::render::renderer::get_ptr();
        if (p_renderer)
        p_renderer->destroy();

        auto* p_audio = GLT::audio::manager::get_ptr();
        if (p_audio)
        p_audio->destroy();

        auto* p_window = GLT::platform::window::get_ptr();
        if (p_window) {
            auto attributes = p_window->get_window_attributes(m_main_window);
            platform::serialize_window_attributes(m_project.project_path, attributes, serializer::option::save);
            p_window->destroy(m_main_window);
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
            [game_loop, this](const window_close_event& event) {
            
                if (event.get_window() == m_main_window)
                    game_loop->request_stop();
            }
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


    void application::set_target_fps(const f32 fps) {

        const u64 time = (1 / fps) * 1000000;
        m_fps_controller.set_target_interval_duration(std::chrono::microseconds(time));
    }


    GLT::render::i_scene_renderer* application::create_scene_renderer(const GLT::render::scene_renderer_desc& desc) noexcept {

        auto* p_renderer = GLT::render::renderer::get_ptr();
        VALIDATE(p_renderer, return {}, "", "Failed to load render plugin")
        auto scene_renderer = p_renderer->create_scene_renderer(desc);
        if (scene_renderer) {

            m_scene_renderers.push_back(std::move(*scene_renderer));
            return m_scene_renderers.back().get();
        
        } else
            LOG(error, "Failed to create offscreen scene renderer: {}", scene_renderer.error().message)

        return {};
    }


    void application::destroy_scene_renderer(GLT::render::i_scene_renderer* p_scene_renderer) noexcept {

        if (!p_scene_renderer)
            return;
        std::erase_if(m_scene_renderers, [p_scene_renderer](const auto& r) { return r.get() == p_scene_renderer; });
    }

    // ---- world access -------------------------------------------------------

    const game::game_layer* application::get_game_layer_ptr() const noexcept { return m_layer_stack.get<GLT::game::game_layer>(); }


    GLT::game::game_layer* application::get_game_layer_ptr() noexcept { return m_layer_stack.get<GLT::game::game_layer>(); }


    world::i_world* application::current_world() const noexcept {

        const auto* game_layer = get_game_layer_ptr();
        if (!game_layer)
            return nullptr;
        auto* session = game_layer->get_primary_session();
        return session ? session->get_world() : nullptr;
    }


    std::expected<void, asset::load_error> application::load_world(const std::filesystem::path& path, bool only_if_none) {

        if (only_if_none && current_world())
            return {};

        const auto* game_layer = get_game_layer_ptr();
        if (!game_layer)
            return std::unexpected(asset::load_error::no_handler);

        auto* session = game_layer->get_primary_session();
        if (!session)
            return std::unexpected(asset::load_error::not_found);

        return session->load_world_from_path(path);
    }


    std::expected<void, asset::load_error> application::save_current_world() {

        const auto* game_layer = get_game_layer_ptr();
        if (!game_layer)
            return std::unexpected(asset::load_error::no_handler);

        auto* session = game_layer->get_primary_session();
        if (!session)
            return std::unexpected(asset::load_error::not_found);

        return session->save_world();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

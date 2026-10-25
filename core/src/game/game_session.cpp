
#include "util/pch.h"
#include "game_session.h"

#include "world/i_world.h"
#include "world/i_world_plugin.h"
#include "world/i_world_scene.h"
#include "world/controller.h"
#include "render/i_scene_renderer.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::game {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    game_session::game_session(session_id id) : m_id(id) { }


    // Renderers are non-owning, deliberately left alone
    // Their owners (application / editor) destroy them first during teardown
    game_session::~game_session() { m_renderers.clear(); }

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // world -----------------------------------------------------------------------------------------------------------

    std::expected<void, asset::load_error> game_session::load_world(asset::asset_ref asset) {

        if (!asset)
            return std::unexpected(asset::load_error::not_found);

        auto* wp = world::manager::get_ptr();
        if (!wp)
            return std::unexpected(asset::load_error::no_handler);

        // NOTE: instantiate() takes the ref by value. Copying keeps our `asset` alive so we can move it into m_world_asset afterwards
        // The plugin then owns its own ref, so the world outlives our session bookkeeping if needed — but the two refs point at the same
        // cached asset
        auto result = wp->instantiate(asset);
        if (!result)
            return std::unexpected(result.error());

        m_world = std::move(*result);
        m_world_asset = std::move(asset);
        m_sim_time = 0.f;
        m_sim_delta = 0.f;
        m_has_camera = false;
        m_last_camera = {};
        m_scene_buffer.clear();
        m_light_buffer.clear();
        return {};
    }


    void game_session::unload_world() {

        m_world.reset();
        m_world_asset.reset();
        m_has_camera = false;
        m_last_camera = {};
        m_sim_time = 0.f;
        m_sim_delta = 0.f;
        m_scene_buffer.clear();
        m_light_buffer.clear();
    }


    std::expected<void, asset::load_error> game_session::save_world() {

        if (!m_world)
            return std::unexpected(asset::load_error::not_found);

        auto* wp = world::manager::get_ptr();
        if (!wp)
            return std::unexpected(asset::load_error::no_handler);

        return wp->save_world(*m_world);
    }


    std::expected<void, asset::load_error> game_session::save_world_as(const std::filesystem::path& path) {

        if (!m_world)
            return std::unexpected(asset::load_error::not_found);

        auto* wp = world::manager::get_ptr();
        if (!wp)
            return std::unexpected(asset::load_error::no_handler);

        return wp->save_world_as(*m_world, path);
    }


    // controller ------------------------------------------------------------------------------------------------------

    void game_session::set_controller(ref<world::controller> ctrl) { m_controller = std::move(ctrl); }


    // renderers -------------------------------------------------------------------------------------------------------

    void game_session::add_renderer(render::i_scene_renderer* renderer) {

        if (!renderer)
            return;
        if (std::find(m_renderers.begin(), m_renderers.end(), renderer) == m_renderers.end())
            m_renderers.push_back(renderer);
    }


    void game_session::remove_renderer(render::i_scene_renderer* renderer) { std::erase(m_renderers, renderer); }


    std::span<render::i_scene_renderer* const> game_session::renderers() const noexcept { return { m_renderers.data(), m_renderers.size() }; }


    // per-frame -------------------------------------------------------------------------------------------------------

    void game_session::update(f32 dt) {

        if (m_paused)
            return;

        if (m_controller)
            m_controller->update(dt);

        if (!m_world)
            return;

        auto* scene = m_world->as<world::i_world_scene>();

        // Sample the camera before the world tick so the streaming pass
        // (inside m_world->update) sees this frame's anchor
        if (scene) {

            world::camera_snapshot cam{};
            if (scene->get_camera_view(cam)) {
                m_last_camera = cam;
                m_has_camera = true;
                m_world->set_streaming_anchor(cam.position);
            }
        }

        m_world->update(dt);
        m_sim_time  += dt;
        m_sim_delta = dt;

        if (!scene)
            return;

        scene->gather_scene(m_scene_buffer);
        scene->gather_lights(m_light_buffer);

        for (auto* renderer : m_renderers) {
            if (!renderer)
                continue;
            renderer->submit_scene(m_scene_buffer);
            renderer->submit_lights(m_light_buffer);
            if (m_has_camera)
                renderer->set_active_camera(m_last_camera);
        }
    }

}

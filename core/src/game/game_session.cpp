#include "util/pch.h"
#include "game_session.h"

#include "world/i_world.h"
#include "world/i_world_plugin.h"
#include "world/i_world_scene.h"
#include "world/controller.h"
#include "render/i_scene_renderer.h"
#include "physics/i_physics.h"
#include "physics/i_physics_plugin.h"



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

    game_session::game_session(session_id id) : m_id(id) {

        // Physics is eager. If the plugin is loaded, every session gets its own simulation from the get-go — gameplay code
        // can call session->get_physics() unconditionally and never worry about lazy-init races. If the plugin isn't loaded,
        // the session simply runs without physics; gameplay code is expected to check get_physics() for null
        if (auto* phys = physics::get_ptr()) {

            auto result = phys->instantiate();
            if (result)
                m_physics = std::move(*result);
            else
                LOG(error, "Failed to instantiate physics for session[{}]: {}",
                    (id.index), (static_cast<int>(result.error())))
        }
    }


    game_session::~game_session() { m_renderers.clear(); }

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // world -----------------------------------------------------------------------------------------------------------

    const asset::asset_ref& game_session::world_asset() const noexcept {

        static const asset::asset_ref empty{};
        return m_world ? m_world->asset() : empty;
    }


    std::expected<void, asset::load_error> game_session::load_world(asset::asset_ref asset) {

        if (!asset)
            return std::unexpected(asset::load_error::not_found);

        auto* wp = world::manager::get_ptr();
        if (!wp)
            return std::unexpected(asset::load_error::no_handler);

        auto result = wp->instantiate(std::move(asset));
        if (!result)
            return std::unexpected(result.error());

        m_world = std::move(*result);
        m_world->set_physics(m_physics.get());
        reset_per_frame_state();
        return {};
    }


    std::expected<void, asset::load_error> game_session::load_world_from_path(const std::filesystem::path& path) {

        auto* wp = world::manager::get_ptr();
        if (!wp)
            return std::unexpected(asset::load_error::no_handler);

        auto result = wp->instantiate_from_path(path);
        if (!result)
            return std::unexpected(result.error());

        m_world = std::move(*result);
        m_world->set_physics(m_physics.get());
        reset_per_frame_state();
        return {};
    }


    void game_session::unload_world() {

        m_world.reset();                                // destroys bodies while m_physics is still live

        if (m_physics) {
            if (auto* phys = physics::get_ptr()) {
                if (auto result = phys->instantiate())
                    m_physics = std::move(*result);
                else
                    LOG(error, "Failed to rebuild physics after world unload: {}", (static_cast<int>(result.error())))
            }
        }
        reset_per_frame_state();
    }


    void game_session::reset_per_frame_state() noexcept {

        m_sim_time = 0.f;
        m_sim_delta = 0.f;
        m_scene_buffer.clear();
        m_light_buffer.clear();
    }


    std::expected<void, asset::load_error> game_session::save_world() {

        if (!m_world)
            return std::unexpected(asset::load_error::not_found);
        return m_world->save();
    }


    std::expected<void, asset::load_error> game_session::save_world_as(const std::filesystem::path& path) {

        if (!m_world)
            return std::unexpected(asset::load_error::not_found);
        return m_world->save_as(path);
    }

    // controller ------------------------------------------------------------------------------------------------------

    void game_session::set_controller(ref<world::controller> ctrl) { m_controller = std::move(ctrl); }

    // renderers -------------------------------------------------------------------------------------------------------

    void game_session::add_renderer(render::i_scene_renderer* r, world::entity_id camera) {

        if (!r)
            return;
        auto it = std::find_if(m_renderers.begin(), m_renderers.end(), [r](const renderer_slot& s){ return s.renderer == r; });
        if (it != m_renderers.end()) {
            it->camera = camera;
            return;
        }
        m_renderers.push_back({ r, camera });
    }


    void game_session::remove_renderer(render::i_scene_renderer* r) {

        std::erase_if(m_renderers, [r](const renderer_slot& s){ return s.renderer == r; });
    }


    void game_session::set_renderer_camera(render::i_scene_renderer* r, world::entity_id camera) {

        for (auto& s : m_renderers)
            if (s.renderer == r) {
                s.camera = camera;
                return;
            }
    }


    world::entity_id game_session::get_renderer_camera(render::i_scene_renderer* r) const {

        for (const auto& s : m_renderers)
            if (s.renderer == r)
                return s.camera;
        return {};
    }

    // per-frame -------------------------------------------------------------------------------------------------------

    void game_session::update(f32 dt) {

        if (m_paused)
            return;

        // Controller runs first so any input-driven intent lands in the world/ECS state before physics reads it. Physics
        // runs before world->update so the world's gather step sees this frame's post-physics transforms
        if (m_controller)
            m_controller->update(dt);

        if (m_physics)
            m_physics->step(dt);

        if (!m_world)
            return;

        auto* scene = m_world->as<world::i_world_scene>();

        // ---- streaming anchor tracks the primary camera ---------------------
        if (scene) {
            const auto primary = resolve_camera(scene, m_primary_camera);
            world::camera_snapshot cam{};
            if (scene->get_camera_view(primary, cam))                 // entity overload
                m_world->set_streaming_anchor(cam.position);
        }

        m_world->update(dt);
        m_sim_time += dt;
        m_sim_delta = dt;

        if (!scene)
            return;

        scene->gather_scene(m_scene_buffer);
        scene->gather_lights(m_light_buffer);

        for (auto& slot : m_renderers) {

            if (!slot.renderer)
                continue;

            slot.renderer->submit_scene(m_scene_buffer);
            slot.renderer->submit_lights(m_light_buffer);

            const auto camera = resolve_camera(scene, slot.camera);
            world::camera_snapshot cam{};
            if (scene->get_camera_view(camera, cam))
                slot.renderer->set_active_camera(cam);
        }
    }


    world::entity_id game_session::resolve_camera(world::i_world_scene* scene, world::entity_id override_id) const {

        if (override_id.is_valid())
            return override_id;

        if (m_primary_camera.is_valid())
            return m_primary_camera;

        return scene->get_active_camera();
    }

}

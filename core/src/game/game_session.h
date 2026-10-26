#pragma once

#include "asset/type.h"
#include "asset/asset_ref.h"
#include "asset/mesh.h"
#include "world/camera_snapshot.h"
#include "world/light_snapshot.h"
#include "plugin_system/plugin_manager.h"
#include "game/session_id.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics {
    class i_physics;
}

namespace GLT::world  {
    class i_world;
    class controller;
    class i_world_scene;
}

namespace GLT::render {
    class i_scene_renderer;
}

namespace GLT::game {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // One isolated instance of the game. Owns everything that must not bleed across instances (world, physics, controller,
    // scratch buffers, sim clock). Does NOT own renderers — those are attached by whoever created them (application / editor
    // viewport / sim-window spawner) and destroyed by the same party
    class game_session final {
    public:

        explicit game_session(session_id id);
        ~game_session();

        // Fallback camera + streaming-anchor source. If INVALID, falls back to scene->get_active_camera() at resolve time
        DEFAULT_GETTER_SETTER(world::entity_id,                 primary_camera)
        DEFAULT_GETTER_SETTER(world::entity_id,                 editor_camera)

        // identity ----------------------------------------------------------------------------------------------------
        [[nodiscard]] session_id get_id() const noexcept { return m_id; }

        // physics -----------------------------------------------------------------------------------------------------
        // Null when the physics plugin isn't loaded. Physics instances are eagerly created in the ctor and destroyed with
        // the session — their lifetime mirrors the session exactly. Eager construction means gameplay code can assume
        // physics is available the moment a session exists
        [[nodiscard]] physics::i_physics* get_physics() const noexcept { return m_physics.get(); }

        // world -------------------------------------------------------------------------------------------------------
        [[nodiscard]] world::i_world* get_world() const noexcept { return m_world.get(); }

        // Forwarded from the live world; empty when no world is loaded.
        [[nodiscard]] const asset::asset_ref& world_asset() const noexcept;

        // Load from an already-resolved world asset_ref (used by the editor when the asset is warm)
        [[nodiscard]] std::expected<void, asset::load_error> load_world(asset::asset_ref asset);

        // Load by content-dir-relative path. Resolves through the registry, then instantiates.
        [[nodiscard]] std::expected<void, asset::load_error> load_world_from_path(const std::filesystem::path& path);

        // Forwards to i_world::save / i_world::save_as. RAII teardown: dropping m_world destroys the world and releases
        // its region handles; no explicit unload is needed unless you want to keep the session.
        void unload_world();

        [[nodiscard]] std::expected<void, asset::load_error> save_world();
        [[nodiscard]] std::expected<void, asset::load_error> save_world_as(const std::filesystem::path& path);

        // controller --------------------------------------------------------------------------------------------------
        void set_controller(ref<world::controller> ctrl);
        [[nodiscard]] world::controller* get_controller() const noexcept { return m_controller.get(); }

        // renderers (non-owning; caller owns) -------------------------------------------------------------------------
        // camera defaults to INVALID -> the slot follows the session's primary camera
        void add_renderer(render::i_scene_renderer* renderer, world::entity_id camera = {});
        void remove_renderer(render::i_scene_renderer* renderer);

        // Rebinding an existing slot (safe if r isn't attached — no-op).
        void set_renderer_camera(render::i_scene_renderer* renderer, world::entity_id camera);
        [[nodiscard]] world::entity_id get_renderer_camera(render::i_scene_renderer* renderer) const;

        // sim state ---------------------------------------------------------------------------------------------------
        void set_paused(bool paused) noexcept { m_paused = paused; }
        [[nodiscard]] bool is_paused() const noexcept { return m_paused; }
        [[nodiscard]] f32 sim_time() const noexcept { return m_sim_time; }
        [[nodiscard]] f32 sim_delta() const noexcept { return m_sim_delta; }

        // per-frame ---------------------------------------------------------------------------------------------------
        void update(f32 delta_time);

    private:

        struct renderer_slot {
            GLT::render::i_scene_renderer*                      renderer{};
            GLT::world::entity_id                               camera{};      // INVALID = inherit primary
        };

        void reset_per_frame_state() noexcept;

        world::entity_id resolve_camera(GLT::world::i_world_scene* scene, world::entity_id override_id) const;


        session_id                                              m_id{};

        // IMPORTANT: declaration order. Reverse-declaration destruction order means m_world dies BEFORE m_physics. That's
        // intentional — once the world↔physics bridge lands (Phase 4), the world will own body handles and must destroy
        // them while its i_physics instance is still alive
        unique_ref<physics::i_physics>                          m_physics{};
        unique_ref<world::i_world>                              m_world{};

        ref<world::controller>                                  m_controller{};
        std::vector<renderer_slot>                              m_renderers{};
        GLT::world::entity_id                                   m_primary_camera{};
        GLT::world::entity_id                                   m_editor_camera{};
        f32                                                     m_sim_time = 0.f;
        f32                                                     m_sim_delta = 0.f;
        bool                                                    m_paused = false;
        std::vector<asset::mesh::instance>                      m_scene_buffer{};
        std::vector<world::light_snapshot>                      m_light_buffer{};
    };

}

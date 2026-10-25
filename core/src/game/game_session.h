
#pragma once

#include "asset/type.h"
#include "asset/asset_ref.h"
#include "asset/mesh.h"
#include "world/camera_snapshot.h"
#include "world/light_snapshot.h"
#include "plugin_system/plugin_manager.h"
#include "game/session_id.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world  {
    class i_world;
    class controller;
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

    // One isolated instance of the game. Owns everything that must not bleed across instances (world, controller, scratch buffers, sim clock)
    // Does NOT own renderers — those are attached by whoever created them (application / editor viewport / sim-window spawner) and destroyed
    // by the same party
    class game_session final {
    public:

        explicit game_session(session_id id);
        ~game_session();

        // ---- identity -------------------------------------------------------
        [[nodiscard]] session_id id() const noexcept { return m_id; }

        // ---- world ----------------------------------------------------------
        [[nodiscard]] world::i_world* world() const noexcept { return m_world.get(); }
        [[nodiscard]] const asset::asset_ref& world_asset() const noexcept { return m_world_asset; }

        [[nodiscard]] std::expected<void, asset::load_error> load_world(asset::asset_ref asset);
        void unload_world();

        [[nodiscard]] std::expected<void, asset::load_error> save_world();
        [[nodiscard]] std::expected<void, asset::load_error> save_world_as(const std::filesystem::path& path);

        // ---- controller -----------------------------------------------------
        void set_controller(ref<world::controller> ctrl);
        [[nodiscard]] world::controller* controller() const noexcept { return m_controller.get(); }

        // ---- renderers (non-owning; caller owns) ----------------------------
        void add_renderer(render::i_scene_renderer* r);
        void remove_renderer(render::i_scene_renderer* r);
        [[nodiscard]] std::span<render::i_scene_renderer* const> renderers() const noexcept;

        // ---- sim state ------------------------------------------------------
        void set_paused(bool paused) noexcept { m_paused = paused; }
        [[nodiscard]] bool is_paused() const noexcept { return m_paused; }
        [[nodiscard]] f32 sim_time() const noexcept { return m_sim_time; }
        [[nodiscard]] f32 sim_delta() const noexcept { return m_sim_delta; }

        // ---- per-frame (called by game_layer, main thread only) -------------
        void update(f32 delta_time);

    private:

        session_id                                              m_id{};
        unique_ref<world::i_world>                              m_world{};
        asset::asset_ref                                        m_world_asset{};
        ref<world::controller>                                  m_controller{};
        std::vector<render::i_scene_renderer*>                  m_renderers{};
        world::camera_snapshot                                  m_last_camera{};
        bool                                                    m_has_camera = false;
        f32                                                     m_sim_time = 0.f;
        f32                                                     m_sim_delta = 0.f;
        bool                                                    m_paused = false;
        // Reused every frame — sessions never allocate after warmup.
        std::vector<asset::mesh::instance>                      m_scene_buffer{};
        std::vector<world::light_snapshot>                      m_light_buffer{};

    };

}

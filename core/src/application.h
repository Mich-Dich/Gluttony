
#pragma once

#include "util/timing/interval_controller.h"
#include "config/project.h"
#include "asset/asset_ref.h"
#include "render/i_scene_renderer.h"
#include "platform/i_window.h"
#include "debug/profiler.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::platform {
    class i_window_plugin;
}

namespace GLT::render {
    class i_renderer_plugin;
}

namespace GLT::audio {
    class i_audio_plugin;
}

namespace GLT::world {
    class i_world;
}

namespace GLT::game {
    class game_layer;
}

namespace GLT {
    class window_close_event;
}

namespace GLT {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class application {
    public:

        application(const std::filesystem::path& project_path);
        ~application();

        DEFAULT_GETTER_REF(layer_stack,                                                     layer_stack)
        DEFAULT_GETTER_CC(util::interval_controller,                                        fps_controller)
        DEFAULT_GETTERS(f32,                                                                delta_time)
        DEFAULT_GETTERS(f32,                                                                total_time)
        DEFAULT_GETTERS(project,                                                            project)
        DEFAULT_GETTERS(GLT::platform::window_handle,                                       main_window)
        GETTER(std::filesystem::path,                                                       project_path,   m_project.project_path)
        DEFAULT_GETTER_REF(std::vector<GLT::unique_ref<GLT::render::i_scene_renderer>>,     scene_renderers)
        DEFAULT_GETTER_REF_CC(std::vector<GLT::unique_ref<GLT::render::i_scene_renderer>>,  scene_renderers)


        FORCE_INLINE_R static application& get() { return *s_instance; }


        void run();


        void set_target_fps(const f32 fps);


        void shutdown();

        // ---- world access ---------------------------------------------------

        // The game_layer always exists (pushed in the ctor). Never null after construction
        [[nodiscard]] const GLT::game::game_layer* get_game_layer_ptr() const noexcept;


        [[nodiscard]] GLT::game::game_layer* get_game_layer_ptr() noexcept;


        // Borrowed pointer to the primary session's world. Null until a world is loaded
        // Note: the world instance owns its own asset_ref — do NOT cache a copy
        [[nodiscard]] GLT::world::i_world* current_world() const noexcept;


        // Resolve [path] through the asset registry and instantiate it on the primary session
        // If [only_if_none] is true, skips if a world is already loaded
        [[nodiscard]] std::expected<void, GLT::asset::load_error> load_world(const std::filesystem::path& path, bool only_if_none = false);


        // Forward to primary_session()->save_world(). Returns not_found if no world is loaded
        [[nodiscard]] std::expected<void, GLT::asset::load_error> save_current_world();

        // ---- scene renderer factory -----------------------------------------

        [[nodiscard]] GLT::render::i_scene_renderer* create_scene_renderer(const GLT::render::scene_renderer_desc& desc) noexcept;


        void destroy_scene_renderer(GLT::render::i_scene_renderer* p_scene_renderer) noexcept;

    private:

        static application*                                                 s_instance;
        version                                                             m_version{};
        util::interval_controller                                           m_fps_controller{};
        layer_stack                                                         m_layer_stack{};
        u32                                                                 m_focus_fps = 60;
        u32                                                                 m_none_focus_fps = 30;
        f32                                                                 m_delta_time = 0.f;
        f32                                                                 m_total_time = 0.f;
        project                                                             m_project{};
        debug::application_stats                                            m_application_stats{};
        std::vector<GLT::unique_ref<GLT::render::i_scene_renderer>>         m_scene_renderers;
        GLT::platform::window_handle                                        m_main_window{};

    };

}

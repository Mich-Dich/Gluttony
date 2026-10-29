
#pragma once

#include <imgui.h>

#include <layer/layer.h>
#include <event/input_event.h>
#include <event/application_event.h>
#include <platform/i_window.h>

#include "window/base/base_window.h"
#include "util/event/asset_event.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {
    class i_renderer_plugin;
    class image;
    class i_scene_renderer; 
}

namespace GLT::editor {
    class content_browser_window;
}

namespace GLT::game {
    class game_session;
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class editor_layer : public GLT::layer {
    public:

        editor_layer();
        ~editor_layer();


        void on_attach() override;


        void on_detach() override;


        void update(const f32 delta_time);


        void render_imgui(const f32 delta_time);


        template<typename window_type, typename... args>
        void add_window(args&&... arguments);

    private:

        struct notification {

            std::string                                 title;
            std::string                                 description;
            GLT::logger::severity                       severity{ GLT::logger::severity::info };
            f32                                         lifetime{ 0.0f };               // seconds remaining; < 0 = persistent
            f32                                         initial_lifetime{ 0.0f };       // for progress bar
            u32                                         id{ 0 };
            bool                                        dismissed{ false };
        };


        enum class simulation_mode : u8 {

            current_viewport = 0,                       // reuse the existing world-viewport window
            new_window,                                 // 1-4 standalone windows
        };


        struct sim_window_config {

            ImVec2                                      position { 100.f, 100.f };
            ImVec2                                      size { 1280.f, 720.f };
            bool                                        maximized = false;
            bool                                        vsync = true;
        };


        // One layout = the arrangement used when there are `N` windows. Only the first `N` entries are meaningful
        // the rest stay at defaults and are ignored by the UI
        struct sim_layout {

            std::array<sim_window_config, 4>            windows{};
        };


        struct simulation_instance {

            GLT::platform::window_handle                handle{};
            GLT::render::i_scene_renderer*              scene_renderer{};   // non-owning; app owns
            GLT::game::game_session*                    session{};
            bool                                        cursor_captured = false;
        };


	    base_window* find_window_by_name(const std::string& name);


        void render_toolbar();


        // Draws the play/pause/step/stop + settings controls on the right side of the toolbar. Call this from render_toolbar()
        // while the toolbar window is still open
        void render_simulation_controls();


        void render_dockspace();


        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);


        void on_asset_open_event(const asset_open_event& event);


        void on_asset_import_request_event(const asset_import_request_event& event);


        void on_key_event(const key_event& event);


        void register_core_editors();


        void open_asset_editor(const asset_open_event& event);


        void on_save_event(const GLT::save_event& event);


        void on_window_close_event(const GLT::window_close_event& event);


        void on_notification_event(const notification_event& event);


        void add_notification(const notification_event& event);


        // Draws the bottom-right notification stack. Call at the end of render_imgui, before modals.
        void render_notifications(const f32 delta_time);


        [[nodiscard]] static f32 display_time_for(GLT::logger::severity sev);


        [[nodiscard]] static ImVec4 color_for(GLT::logger::severity sev);


        void on_asset_create_request_event(const asset_create_request_event& event);


        // Persists all 4 layouts to <project_dir>/editor_simulation.yaml.
        // Called from on_save_event (which is also triggered from ~editor_layer).
        void save_simulation_settings();


        // Reads the same file if it exists. Called from the constructor after
        // application::get().get_project_path() is valid.
        void load_simulation_settings();


        void start_simulation();


        // Creates every simulation window described by m_simulation_mode / m_sim_window_count / m_sim_layouts.
        // No-op for simulation_mode::current_viewport (the existing viewport is reused).
        void spawn_simulation_windows();


        // Destroys every handle collected in m_sim_window_handles and clears the list.
        void destroy_simulation_windows();


        editor_layer::simulation_instance* find_sim_instance(GLT::platform::window_handle handle);


        void on_window_focus_event(const GLT::window_focus_event& event);


        void capture_sim_window(simulation_instance& instance);


        void release_sim_cursor();



        GLT::unique_ref<GLT::render::image>             m_logo{};
        std::vector<GLT::unique_ref<base_window>>       m_windows{};

        bool                                            m_show_stats_window = false;
        bool                                            m_show_plugin_manager_window = false;
        bool                                            m_show_demo = false;
        bool                                            m_show_style = false;
        bool                                            m_reset_layout = true;
        ImGuiID                                         m_dockspace_id = 0;

        handle                                          m_asset_open_event_sub_handle{};
        handle                                          m_asset_import_request_event_sub_handle{};
        handle                                          m_key_sub_handle{};

        // event could be called during draw, so buffer
        std::vector<asset_open_event>                   m_asset_open_event_buffer{};
        std::vector<asset_import_request_event>         m_asset_import_request_event_buffer{};

        handle                                          m_asset_create_request_event_sub_handle{};
        std::vector<asset_create_request_event>         m_asset_create_request_event_buffer{};
        handle                                          m_save_sub_handle{};

        handle                                          m_notification_sub_handle{};
        u32                                             m_next_notification_id{ 0 };
        std::vector<notification>                       m_notifications{};
        std::vector<notification_event>                 m_notification_event_buffer{};   // buffered - event may fire mid-draw

        bool                                            m_start_sims = false;
        GLT::system_state                               m_simulation_state = GLT::system_state::idle;   // editor specific sim
        simulation_mode                                 m_simulation_mode{ simulation_mode::current_viewport };
        u32                                             m_sim_window_count{ 1 };
        std::array<sim_layout, 4>                       m_sim_layouts{}; // m_sim_layouts[N - 1] = layout for N windows. All 4 are always saved
        std::vector<simulation_instance>                m_simulation_instances{};
        handle                                          m_window_close_sub{};

    };

}

#include "editor_layer.inl"

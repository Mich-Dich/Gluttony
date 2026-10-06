
#pragma once

#include <imgui.h>

#include <layer/layer.h>
#include <event/input_event.h>
#include <event/application_event.h>

#include "window/base/base_window.h"
#include "util/event/asset_event.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {
    class i_renderer_plugin;
    class image;
}

namespace GLT::editor {
    class content_browser_window;
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


	    base_window* find_window_by_name(const std::string& name);


        void render_toolbar();


        void render_dockspace();


        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);


        void on_asset_open_event(const asset_open_event& event);


        void on_asset_import_request_event(const asset_import_request_event& event);


        void on_key_event(const key_event& event);


        void register_core_editors();


        void open_asset_editor(const asset_open_event& event);


        void on_save_event(const GLT::save_event& event);


        void on_notification_event(const notification_event& event);


        void add_notification(const notification_event& event);


        // Draws the bottom-right notification stack. Call at the end of render_imgui, before modals.
        void render_notifications(const f32 delta_time);


        [[nodiscard]] static f32 display_time_for(GLT::logger::severity sev);


        [[nodiscard]] static ImVec4 color_for(GLT::logger::severity sev);


        void on_asset_create_request_event(const asset_create_request_event& event);



        GLT::unique_ref<GLT::render::image>             m_logo{};
        std::vector<GLT::unique_ref<base_window>>       m_windows{};

        bool                                            m_show_stats_window = false;
        bool                                            m_show_demo  = false;
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
    };

}

#include "editor_layer.inl"

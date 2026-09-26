
#pragma once

#include <imgui.h>

#include <layer/layer.h>
#include <event/input_event.h>
#include <event/application_event.h>

#include "window/base_window.h"
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

        struct pending_save_request {

            save_as_request_event::request              req;
            char                                        filename[256]{};
            std::filesystem::path                       directory{};
            std::string                                 error_message;      // empty = ok
        };


	    base_window* find_window_by_name(const std::string& name);


        void render_toolbar();


        void render_dockspace();


        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);


        void on_asset_open_event(const asset_open_event& event);


        void on_asset_import_request_event(const asset_import_request_event& event);


        void on_key_event(const key_event& event);


        void on_save_event(const save_event& event);


        void on_save_as_request_event(const save_as_request_event& event);


        void register_core_editors();


        void open_asset_editor(const asset_open_event& event);


        // Reads the head of the queue and opens the modal. No-op if a request is already being shown.
        void open_next_save_request();


        // Renders the modal if one is active. Call at the end of render_imgui, after every other window - modals must layer on top.
        void render_save_as_popup();


        // Confirms or discards the current request and dequeues it. Pops the next request off the queue if one is waiting.
        void resolve_save_as_request(bool confirmed);


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

        std::vector<save_as_request_event>              m_save_as_event_buffer{};
        std::deque<pending_save_request>                m_save_as_queue{};
        bool                                            m_save_as_open{ false };

        handle                                          m_save_as_sub_handle{};
        handle                                          m_save_sub_handle{};

    };

}

#include "editor_layer.inl"

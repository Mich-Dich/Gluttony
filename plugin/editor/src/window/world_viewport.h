
#pragma once

#include <imgui.h>

#include <world/i_world_inspector.h>
#include <event/application_event.h>
#include <reflection/registry.h>

#include "window/base/base_window.h"
#include "window/content_browser.h"
#include "util/event/asset_event.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {
    class i_renderer_plugin;
    class image;
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class world_viewport_window : public base_window {
    public:

        world_viewport_window();
        ~world_viewport_window();


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        struct pending_save_request {

            GLT::save_as_request_event::request         req;
            char                                        filename[256]{};
            std::filesystem::path                       directory{};
            std::string                                 error_message;                  // empty = ok
        };

        void render_inner_dockspace();

        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);

        void render_viewport();

        // Renders the floating mode selector in the top-left of the viewport image. Returns true while its combo popup is open,
        // so the caller can suppress viewport input
        bool render_mode_selector_overlay();

        void render_details();

        void render_outliner();

        void set_cursor_captured(const bool captured);

        void render_outliner_node(GLT::world::entity_id id);

        void on_save_event(const GLT::save_event& event);

        void on_save_as_request_event(const GLT::save_as_request_event& event);

        // Reads the head of the queue and opens the modal. No-op if a request is already being shown
        void open_next_save_request();

        // Renders the modal if one is active. Call at the end of window, after every other window - modals must layer on top
        void render_save_as_popup();

        // Confirms or discards the current request and dequeues it. Pops the next request off the queue if one is waiting
        void resolve_save_as_request(bool confirmed);

        bool render_settings_overlay();

        void render_settings_popup_body();  // popup content, called by render_settings_overlay

        bool draw_reflected_setting(const GLT::reflect::member_descriptor& md, void* ptr);


        std::vector<content_browser_window>             m_content_browsers{};
        GLT::ref<GLT::render::i_renderer_plugin>        m_renderer{};
        ImVec2                                          m_viewport_size{100, 60};
        bool                                            m_reset_layout = true;
        bool                                            m_cursor_captured = false;

        GLT::ref<GLT::world::i_world_plugin>            m_world{};
        GLT::world::entity_id                           m_selected_entity{ GLT::world::INVALID_ENTITY };
        GLT::world::i_world_inspector*                  m_inspector{ nullptr };
        u64                                             m_clipboard_component{ 0 };

        handle                                          m_save_as_sub_handle{};
        handle                                          m_save_sub_handle{};
        std::vector<GLT::save_as_request_event>         m_save_as_event_buffer{};
        std::deque<pending_save_request>                m_save_as_queue{};
        bool                                            m_save_as_open{ false };

    };

}

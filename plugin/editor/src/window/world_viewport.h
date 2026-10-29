#pragma once

#include <imgui.h>
#include <ImGuizmo.h>

#include <world/i_world_inspector.h>
#include <event/application_event.h>
#include <reflection/registry.h>

#include "resource_manager/icon_manager.h"
#include "util/event/asset_event.h"
#include "window/base/base_window.h"
#include "window/content_browser.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {
    class i_renderer_plugin;
    class i_scene_renderer;
    class image;
}

namespace GLT::world {
    class i_world;
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

        // ---- rendering ----
        void render_inner_dockspace();
        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);
        void render_viewport();
        bool render_mode_selector_overlay(const ImVec2& image_min);
        void render_details();
        void render_add_component_popup(GLT::world::entity_id id, const ImVec2& btn_max);
        void render_outliner();
        void set_cursor_captured(const bool captured);

        // ---- outliner ----
        void render_outliner_row(GLT::world::entity_id id, int depth, int& row_index,
                                 const std::vector<GLT::world::entity_id>& visible_order, bool flat);
        void render_entity_tooltip(GLT::world::entity_id id);
        void render_entity_context_menu(GLT::world::entity_id id, bool visible);

        // ---- outliner helpers ----
        void handle_outliner_click(GLT::world::entity_id id, const std::vector<GLT::world::entity_id>& visible_order);
        void clear_selection();
        [[nodiscard]] bool is_selected(GLT::world::entity_id id) const;
        [[nodiscard]] GLT::world::entity_id primary_selection() const;
        void prune_dead_selection();

        void collect_visible_tree_order(GLT::world::entity_id id, std::vector<GLT::world::entity_id>& out) const;
        [[nodiscard]] bool is_tree_node_open(GLT::world::entity_id id) const;

        [[nodiscard]] std::string entity_display_name(GLT::world::entity_id id) const;
        [[nodiscard]] std::vector<icon_manager::icon_type> entity_component_icons(GLT::world::entity_id id) const;

        // ---- saving ----
        void on_save_event(const GLT::save_event& event);
        void on_save_as_request_event(const GLT::save_as_request_event& event);
        void open_next_save_request();
        void render_save_as_popup();
        void resolve_save_as_request(bool confirmed);

        // ---- render settings ----
        bool render_settings_overlay();
        void render_settings_popup_body();
        bool draw_reflected_setting(const GLT::reflect::member_descriptor& md, void* ptr);

        // ---- gizmo ----
        void render_gizmo(const ImVec2& image_min, const ImVec2& image_size);
        void render_gizmo_toolbar(const ImVec2& image_min, const ImVec2& image_size);
        static bool gizmo_toggle_button(const char* label, bool active, const ImVec2& size = ImVec2(0, 0),
                                        const bool use_icon = false,
                                        icon_manager::icon_type icon = icon_manager::icon::transfrom_rotation);

        void update_selection_debug_draw();

        // ---- data ----
        GLT::render::i_scene_renderer*                  m_scene = nullptr;
        bool                                            m_viewport_image_drawn = false;
        std::vector<content_browser_window>             m_content_browsers{};
        GLT::ref<GLT::render::i_renderer_plugin>        m_renderer{};
        ImVec2                                          m_viewport_size{100, 60};
        bool                                            m_reset_layout = true;
        bool                                            m_cursor_captured = false;

        GLT::world::i_world*                            m_world{};
        GLT::world::i_world_inspector*                  m_inspector{ nullptr };
        u64                                             m_clipboard_component{ 0 };

        // outliner state
        std::vector<GLT::world::entity_id>              m_selected_entities{};
        GLT::world::entity_id                           m_selection_anchor{ GLT::world::INVALID_ENTITY };
        char                                            m_outliner_filter[128]{};

        handle                                          m_save_as_sub_handle{};
        handle                                          m_save_sub_handle{};
        std::vector<GLT::save_as_request_event>         m_save_as_event_buffer{};
        std::deque<pending_save_request>                m_save_as_queue{};
        bool                                            m_save_as_open{ false };

        ImGuizmo::OPERATION                             m_gizmo_operation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE                                  m_gizmo_mode = ImGuizmo::WORLD;
        bool                                            m_gizmo_snap = false;
        f32                                             m_gizmo_snap_translate[3] = { 1.f, 1.f, 1.f };
        f32                                             m_gizmo_snap_rotate = 15.f;   // degrees
        f32                                             m_gizmo_snap_scale = 0.1f;

    };

}

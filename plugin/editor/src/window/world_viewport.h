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
#include "undo_system/world_change.h"



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


        // Closes in-progress edits (open gizmo drag, focused text field) before stepping through the history
        bool undo() override;
        bool redo() override;

    private:

        struct pending_save_request {

            GLT::save_as_request_event::request         req;
            char                                        filename[256]{};
            std::filesystem::path                       directory{};
            std::string                                 error_message;                  // empty = ok
        };

        // ---- rendering ----
        void render_inner_dockspace();
        void render_panel_bar();                                // slim bar above the dockspace: world name + "Panels" menu
        void render_panels_menu();
        void add_content_browser();
        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);
        void render_viewport();
        bool render_mode_selector_overlay(const ImVec2& image_min);
        void render_details();
        void render_add_component_popup(GLT::world::entity_id id, const ImVec2& btn_max);
        void render_outliner();
        void set_cursor_captured(const bool captured);

        // ---- outliner ----
        // One row, no recursion: the outliner flattens the open tree into rows and only draws the ones on screen
        void render_outliner_row(GLT::world::entity_id id, int depth, int row_index,
                                 const std::vector<GLT::world::entity_id>& visible_order, bool flat);
        void render_entity_tooltip(GLT::world::entity_id id);
        void render_entity_context_menu(GLT::world::entity_id id, bool visible);
        void render_world_row();                                // pinned "World" row: the world entity (world-scope components)
        [[nodiscard]] bool component_fits(const GLT::world::component::descriptor& d, GLT::world::entity_id id) const;

        // ---- outliner helpers ----
        void handle_outliner_click(GLT::world::entity_id id, const std::vector<GLT::world::entity_id>& visible_order);
        void clear_selection();
        [[nodiscard]] bool is_selected(GLT::world::entity_id id) const;
        [[nodiscard]] GLT::world::entity_id primary_selection() const;
        void prune_dead_selection();

        void collect_visible_tree_order(GLT::world::entity_id id, int depth, std::vector<GLT::world::entity_id>& out,
                                        std::vector<int>& depths) const;
        [[nodiscard]] bool is_tree_node_open(GLT::world::entity_id id) const;

        [[nodiscard]] std::string entity_display_name(GLT::world::entity_id id) const;
        [[nodiscard]] std::vector<icon_manager::icon_type> entity_component_icons(GLT::world::entity_id id) const;
        [[nodiscard]] static std::optional<icon_manager::icon_type> component_icon(std::string_view component_name);

        // ---- saving ----
        void on_save_event(const GLT::save_event& event);
        void on_save_as_request_event(const GLT::save_as_request_event& event);
        void open_next_save_request();
        void render_save_as_popup();
        void resolve_save_as_request(bool confirmed);

        // ---- opening worlds: drop a .glt_world from the content browser onto the viewport image ----
        void handle_viewport_world_drop();                                      // drag-drop target, call right after the image
        void request_open_world(const std::filesystem::path& path);             // checks simulation / unsaved changes first
        void render_open_world_popup();
        void open_requested_world();                                            // in update(): swaps the world between frames

        // ---- render settings ----
        bool render_settings_overlay();
        void render_settings_popup_body();
        bool draw_reflected_setting(const GLT::reflect::member_descriptor& md, void* ptr);

        // ---- gizmo ----
        void render_gizmo(const ImVec2& image_min, const ImVec2& image_size);
        void render_gizmo_toolbar(const ImVec2& image_min, const ImVec2& image_size);
        static bool gizmo_toggle_button(const char* label, bool active, const ImVec2& size = ImVec2(0, 0), const bool use_icon = false,
            icon_manager::icon_type icon = icon_manager::icon::transfrom_rotation);

        void update_selection_debug_draw();

        // ---- undo / redo ----
        void flush_edits();                                                     // commit open tracker edits
        void select_after_undo(const undo_system::world_state& state);          // undo/redo selects what it touched
        void delete_entities(std::vector<GLT::world::entity_id> ids);           // undoable, deletes whole subtrees
        void collect_subtree(GLT::world::entity_id id, std::vector<GLT::world::entity_id>& out) const;

        // ---- data ----
        GLT::render::i_scene_renderer*                  m_scene = nullptr;
        bool                                            m_viewport_image_drawn = false;
        // unique_ref: browsers subscribe to events with their [this] pointer, so they must never move in memory
        std::vector<GLT::unique_ref<content_browser_window>> m_content_browsers{};
        GLT::ref<GLT::render::i_renderer_plugin>        m_renderer{};
        ImVec2                                          m_viewport_size{100, 60};
        bool                                            m_reset_layout = true;

        // sub-panels of the inner dockspace (closable, reopened from the "Panels" menu)
        bool                                            m_show_viewport_panel = true;
        bool                                            m_show_outliner_panel = true;
        bool                                            m_show_details_panel = true;
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
        bool                                            m_render_settings_edited{ false };      // quick settings changed, not yet stored

        ImGuizmo::OPERATION                             m_gizmo_operation = ImGuizmo::TRANSLATE;
        // gizmo space + snapping live in editor_settings (persisted, also editable in the settings window)

        // undo / redo: one tracker per UI region that edits entities "freely" (see world_edit_tracker)
        undo_system::world_edit_tracker                 m_details_edit{ m_undo_stack };
        undo_system::world_edit_tracker                 m_gizmo_edit{ m_undo_stack };

        // world opening (the swap happens in update(), never in the middle of drawing panels that hold m_world)
        std::filesystem::path                           m_world_to_open{};              // confirmed, opened next update()
        std::filesystem::path                           m_world_awaiting_confirm{};     // waits for the unsaved-changes popup
        bool                                            m_open_world_popup_requested = false;

    };

}

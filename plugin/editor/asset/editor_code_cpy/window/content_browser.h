
#pragma once

#include "window/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Dockable ImGui panel that lets the user browse the project's content directory.
    //
    // Layout:
    //   [ back | fwd | up ]  breadcrumbs...    [search]  [refresh]  [splitter]
    //   ----------------------------------------------------------------
    //   |  folder tree (left)         |  grid of files/folders (right)  |
    //   ----------------------------------------------------------------
    //
    // Interactions:
    //   - Single-click selects.
    //   - Double-click on a folder enters it; on a file it is a hook for "open asset".
    //   - Files/folders can be dragged; payload type is "CONTENT_BROWSER_ITEM"
    //     and the payload is the absolute path as a null-terminated string.
    //   - Right-click on an item or on empty space for a context menu.
    class content_browser_window : public base_window {
    public:

        content_browser_window();
        ~content_browser_window();

        DEFAULT_GETTER(std::filesystem::path,       current_dir)
        DEFAULT_GETTER(std::filesystem::path,       selected_path)

        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    protected:

        // void make_window_name(const char* base_name) override;

    private:

        struct dir_entry {
            std::filesystem::path                   path;
            std::string                             name;
            std::string                             extension;      // lowercase, includes the leading dot
            bool                                    is_directory = false;
        };

        // ---- drawing helpers -----------------------------------------------
        void draw_toolbar();
        void draw_breadcrumbs();
        void draw_directory_tree();
        void draw_directory_tree_recursive(const std::filesystem::path& dir);
        void draw_file_view();
        void draw_file_item(const dir_entry& entry);
        void draw_background_context_menu();
        void draw_item_context_menu(const dir_entry& entry);
        void draw_popups();

        // ---- navigation ----------------------------------------------------
        void navigate_to(const std::filesystem::path& dir);
        void navigate_back();
        void navigate_forward();
        void navigate_up();
        void refresh_directory_entries();


        std::filesystem::path                       m_content_dir{};          // Immutable root of the content tree.
        std::filesystem::path                       m_current_dir{};          // Directory currently shown on the right.
        std::filesystem::path                       m_selected_path{};        // Item currently selected in the grid.

        std::vector<std::filesystem::path>          m_history{};
        i32                                         m_history_index = -1;

        std::vector<dir_entry>                      m_entries{};              // Cached children of m_current_dir.
        bool                                        m_entries_dirty = true;

        char                                        m_search_buffer[256]{};
        f32                                         m_left_panel_width = 260.0f;

        std::filesystem::path                       m_pending_rename_path{};
        std::filesystem::path                       m_pending_delete_path{};
        char                                        m_rename_buffer[256]{};
        bool                                        m_open_rename_popup = false;
        bool                                        m_open_delete_popup = false;

    };

}

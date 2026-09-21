
#pragma once

#include "window/base_window.h"

#include "event/event_bus.h"
#include "util/event/file_event.h"



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
    //   - Plain click selects (clears the prior multi-selection).
    //   - Ctrl + click toggles the clicked item in/out of the selection.
    //   - Shift + click extends the selection from the anchor to the clicked item.
    //   - Click on empty space clears the selection.
    //   - Double-click on a folder enters it; on a file it is a hook for "open asset".
    //   - Files/folders can be dragged; payload type is "CONTENT_BROWSER_ITEM"
    //     and the payload is the absolute path as a null-terminated string.
    //   - Right-click on an item or on empty space for a context menu.
    //
    // History is kept in a fixed-size buffer; once full, the oldest entry is
    // dropped when a new destination is pushed.
    class content_browser_window : public base_window {
    public:

        content_browser_window();
        ~content_browser_window();

        DEFAULT_MOVE_CONSTRUCTOR(content_browser_window)
        DELETE_COPY_CONSTRUCTOR(content_browser_window)

        DEFAULT_GETTER(std::filesystem::path,                       current_dir)

        // Every path currently in the selection, in grid order.
        DEFAULT_GETTER(std::vector<std::filesystem::path>,          selected_paths)


        // Primary (first) selected path, or an empty path if nothing is selected.
        // Kept for callers that expect single-selection semantics.
        std::filesystem::path selected_path() const;


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        struct dir_entry {

            std::filesystem::path                                   path;
            std::string                                             name;
            std::string                                             extension;      // lowercase, includes the leading dot
            bool                                                    is_directory = false;
        };

        static constexpr i32                                        k_history_capacity = 48;

        // ---- drawing helpers -----------------------------------------------
        void draw_toolbar();
        void draw_breadcrumbs();
        void draw_directory_tree();
        void draw_directory_tree_recursive(const std::filesystem::path& dir, const bool collapse_tree);
        void draw_file_view();
        void draw_file_item(const dir_entry& entry);
        void draw_background_context_menu();
        void draw_item_context_menu(const dir_entry& entry);
        void draw_popups();

        // ---- selection -----------------------------------------------------
        bool is_selected(const std::filesystem::path& p) const;
        void select_single(const std::filesystem::path& p);
        void toggle_selection(const std::filesystem::path& p);
        void extend_selection_to(const std::filesystem::path& p);
        void clear_selection();

        // ---- navigation ----------------------------------------------------
        void navigate_to(const std::filesystem::path& dir);
        void navigate_back();
        void navigate_forward();
        void navigate_up();
        void refresh_directory_entries();
        void history_push(const std::filesystem::path& dir);

        void import_files(const std::vector<std::filesystem::path>& paths);
        void on_file_event(const file_event& event);

        std::filesystem::path                                       m_content_dir{};    // Immutable root of the content tree.
        std::filesystem::path                                       m_current_dir{};    // Directory currently shown on the right.

        // Fixed-size history buffer. Entries [0, m_history_size) are valid;
        // m_history_index points at the "current" entry (the one m_current_dir
        // matches). When the buffer is full, pushing a new entry drops the
        // oldest by shifting.
        std::array<std::filesystem::path, k_history_capacity>       m_history{};
        i32                                                         m_history_size = 0;
        i32                                                         m_history_index = -1;

        std::vector<dir_entry>                                      m_entries{};        // Cached children of m_current_dir.
        bool                                                        m_entries_dirty = true;

        // Selection state. m_selected_paths holds every selected path;
        // m_selection_anchor is the item that Shift+click extends from.
        std::vector<std::filesystem::path>                          m_selected_paths{};
        std::filesystem::path                                       m_selection_anchor{};

        char                                                        m_search_buffer[256]{};

        std::filesystem::path                                       m_pending_rename_path{};
        std::filesystem::path                                       m_pending_delete_path{};
        char                                                        m_rename_buffer[256]{};
        bool                                                        m_open_rename_popup = false;
        bool                                                        m_open_delete_popup = false;

        handle                                                      m_file_event_sub_handle{};

    };

}

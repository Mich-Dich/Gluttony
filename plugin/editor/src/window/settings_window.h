
#pragma once

#include "window/base/base_window.h"
#include "settings/settings_registry.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Settings window: page list (grouped, searchable) on the left, the selected page on the right.
    //
    // Knows nothing about specific settings - it shows whatever pages the registry holds. With a search term it shows
    // every matching row of every page, and the sidebar shows how many rows each page contributes.
    // Changes are saved automatically as soon as the user lets go of a control.
    class settings_window : public base_window {
    public:

        settings_window(const char* title, const settings_registry& registry);
        ~settings_window() override;


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        // Shows [page_name] (and clears the search). Unknown names are ignored
        void select_page(std::string_view page_name);

    private:

        void render_sidebar();
        void render_content();
        void render_footer();

        const settings_registry&                m_registry;
        char                                    m_search[128]{};
        size_t                                  m_selected_page = 0;
        std::vector<u32>                        m_hits{};                   // rows per page that matched the search (last frame)
        bool                                    m_dirty = false;            // something changed and is not saved yet
        bool                                    m_reset_page = false;
        bool                                    m_focus_search = true;
        bool                                    m_scroll_to_top = false;
    };

}


#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // One page of a settings window ("Appearance", "Camera", ...).
    //
    // [draw] only draws sections and rows (UI::settings_begin_section / UI::settings_toggle / ...). The settings window
    // draws the page header, handles search and "reset page", and calls [save] automatically after a change.
    struct settings_page {

        std::string                             group;          // sidebar group, e.g. "Interface". Groups appear in registration order
        std::string                             name;           // page title + sidebar entry
        std::string                             description;    // one line under the title
        std::function<bool()>                   draw;           // returns true if any value changed this frame
        std::function<void()>                   save;           // persists the page's values (optional)
    };


    // The set of pages a settings window shows. The editor has one for editor settings; engine settings can get their
    // own registry and reuse the same window.
    class settings_registry {
    public:

        void add(settings_page page)                                    { m_pages.push_back(std::move(page)); }

        [[nodiscard]] std::span<const settings_page> pages() const      { return m_pages; }

        // Calls every save callback (they write small files, a few per change is fine)
        void save_all() const {

            for (const auto& page : m_pages)
                if (page.save)
                    page.save();
        }

    private:

        std::vector<settings_page>              m_pages{};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

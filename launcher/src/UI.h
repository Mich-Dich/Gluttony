
#pragma once

#include "util/image.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::launcher {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Owns everything the launcher renders. app.cpp is a hub; ui.cpp is the presentation layer
    class UI {
    public:

        enum class page {
            home = 0,
            projects,
            settings,
            about
        };

        // Apply the launcher theme and preload the branding images. Must be called once the GL/ImGui context is current
        void init(const std::filesystem::path& asset_dir);

        // Draws the entire launcher frame. Pass app's running flag so UI can request shutdown
        void draw(bool& running);

    private:

        void draw_top_bar(bool& running);
        void draw_side_bar();
        void draw_content();

        void draw_home_page();
        void draw_projects_page();
        void draw_settings_page();
        void draw_about_page();

        page                                    m_page = page::home;
        image                                   m_logo;
        image                                   m_hero;
        image                                   m_about_art;

    };

}

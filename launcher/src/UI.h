#pragma once

#include "util/image.h"
#include "util/project_data.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT {
    struct project;
}

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

        UI();

        ~UI();

        enum class page {

            home = 0,
            projects,
            project_details,
            settings,
            about
        };

        void init(const std::filesystem::path& asset_dir);

        void draw(bool& running);

    private:

        void draw_top_bar(bool& running, const f32 height, const f32 weight);
        void draw_side_bar();
        void draw_content();

        void draw_home_page();
        void draw_projects_page();
        void draw_project_details_page();
        void draw_settings_page();
        void draw_about_page();

        void render_project_grid();
        void draw_project_card(const project& p, f32 width);

        void load_project_details(const std::filesystem::path& project_dir);

        page                                                            m_page = page::home;
        image                                                           m_logo;
        image                                                           m_hero;
        image                                                           m_about_art;
        std::vector<GLT::launcher::project>                             m_projects;
        std::string                                                     m_selected_project;
        std::unordered_map<std::string, std::unique_ptr<image>>         m_project_images;
        std::unique_ptr<GLT::project>                                   m_details_project;
        bool                                                            m_details_loaded = false;
    };

}

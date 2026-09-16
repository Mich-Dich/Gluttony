
#pragma once

#include <imgui.h>

#include "window/base_window.h"
#include "window/content_browser.h"



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

        void render_inner_dockspace();
        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);
        void render_viewport();
        void render_details();
        void render_tools();

        std::vector<content_browser_window>             m_content_browsers{};
        GLT::ref<GLT::render::i_renderer_plugin>        m_renderer{};
        ImVec2                                          m_viewport_size{100, 60};
        bool                                            m_reset_layout = true;

    };

}

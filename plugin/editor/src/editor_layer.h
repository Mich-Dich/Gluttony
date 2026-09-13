
#pragma once

#include <imgui.h>

#include <layer/layer.h>



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

    class editor_layer : public GLT::layer {
    public:

        editor_layer();
        ~editor_layer();


        void update(const f32 delta_time);


        void render_imgui(const f32 delta_time);

    private:

        void render_toolbar();
        void render_dockspace();

        void render_viewport();
        void render_content_browser();
        void render_details();
        void render_tools();

        void build_default_layout(ImGuiID dockspace_id, const ImVec2& size);

        GLT::ref<GLT::render::i_renderer_plugin>        m_renderer{};
        ImVec2                                          m_content_size{100, 60};
        GLT::unique_ref<GLT::render::image>             m_logo{};

        bool                                            m_show_demo  = false;
        bool                                            m_show_style = false;

        // Set to true in the ctor or from the View > Reset Layout menu item.
        bool                                            m_reset_layout = true;
    };

}

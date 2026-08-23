
#pragma once

#include <imgui.h>

#include <layer/layer.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {
    class i_renderer_plugin;
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

        GLT::ref<GLT::render::i_renderer_plugin>        m_renderer{};
        ImVec2                                          m_content_size{100, 60};

    };

}

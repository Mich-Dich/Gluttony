
#include <util/pch.h>

#include <imgui.h>

#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_renderer_plugin.h>

#include "editor_layer.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    editor_layer::editor_layer() 
        : layer("editor_layer") {

        m_renderer = GLT::plugin_manager::get_plugin_ref<GLT::render::i_renderer_plugin>(
            GLT::plugin_manager::interface::renderer);
    }


    editor_layer::~editor_layer() {

        m_renderer.reset();
    }

    // CLASS PUBLIC ====================================================================================================
    
    void editor_layer::update(const f32 /*delta_time*/) {

        m_renderer->set_render_size({m_content_size.x, m_content_size.y});
    }


    void editor_layer::render_imgui(const f32 /*delta_time*/) {

        static bool show_demo = false;  // Disabled by default
        if (show_demo)
            ImGui::ShowDemoWindow(&show_demo);

        // ============ MAIN MENU BAR ============
        if (ImGui::BeginMainMenuBar()) {

            if (ImGui::BeginMenu("File")) {

                if (ImGui::MenuItem("New")) { /* TODO */ }
                if (ImGui::MenuItem("Open", "Ctrl+O")) { /* TODO */ }
                if (ImGui::MenuItem("Save", "Ctrl+S")) { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) { /* TODO */ }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Demo Window", nullptr, &show_demo);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        if (ImGui::Begin("Viewport", nullptr)) {
            
            ImTextureID tex_id = m_renderer->get_rendered_image();
            m_content_size = ImGui::GetContentRegionAvail();        // update the window size
            ImGui::Image(tex_id, m_content_size);
        }
        ImGui::End();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

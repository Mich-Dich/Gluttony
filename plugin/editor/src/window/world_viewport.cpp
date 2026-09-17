
#include "util/pch.h"
#include "world_viewport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_renderer_plugin.h>
#include <render/image.h>

#include "util/ui/pannel_collection.h"
#include "resource_manager/icon_manager.h"
#include "window/content_browser.h"



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

    world_viewport_window::world_viewport_window() {
        
        make_window_name("World Viewport");
        m_renderer = GLT::plugin_manager::get_plugin_ref<GLT::render::i_renderer_plugin>(GLT::plugin_manager::interface::renderer);
        m_content_browsers.push_back(GLT::editor::content_browser_window());       // create first content browser
    }


    world_viewport_window::~world_viewport_window() {

        m_content_browsers.clear();
        m_renderer.reset();
    }

    // CLASS PUBLIC ====================================================================================================

    void world_viewport_window::window(const f32 delta_time) {

        if (!m_show_window)
            return;

        apply_pending_dock();

        // A regular, dockable window. Its body hosts a nested dockspace for
        // the Viewport / Details / Tools sub-panels. The outer editor_layer
        // docks this window by its full ImGui ID (m_window_id).
        ImGui::SetNextWindowSizeConstraints(ImVec2(600, 400), ImVec2(std::numeric_limits<f32>::max(), std::numeric_limits<f32>::max()));
        ImGui::Begin(m_window_id.c_str(), &m_show_window);
        {
            render_inner_dockspace();
        }
        ImGui::End();

        // Sub-panels are top-level windows. DockBuilder assigns them to the
        // inner dockspace's nodes the first time the inner layout is built.
        render_viewport();
        render_details();
        render_tools();

        for (auto& browsers : m_content_browsers)
            if (!browsers.should_close())
                browsers.window(delta_time);
    }


    void world_viewport_window::update(const f32 /*delta_time*/) {

        m_renderer->set_render_size({m_viewport_size.x, m_viewport_size.y});
    }


    bool world_viewport_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void world_viewport_window::render_inner_dockspace() {

        ImGuiID inner_id = ImGui::GetID("##world_viewport_dockspace");
        const ImVec2  inner_size = ImGui::GetContentRegionAvail();

        const bool no_layout_yet = (ImGui::DockBuilderGetNode(inner_id) == nullptr);
        if (m_reset_layout || no_layout_yet) {

            m_reset_layout = false;
            build_default_layout(inner_id, inner_size);
        }

        ImGui::DockSpace(inner_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }


    void world_viewport_window::build_default_layout(ImGuiID dockspace_id, const ImVec2& size) {

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, size);

        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);
        ImGuiID dock_right_b = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.50f, nullptr, &dock_right);
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, nullptr, &dock_main);

        // Names must match exactly what the sub-windows pass to ImGui::Begin().
        ImGui::DockBuilderDockWindow("Viewport", dock_main);
        ImGui::DockBuilderDockWindow("Details",  dock_right);
        ImGui::DockBuilderDockWindow("Tools",    dock_right_b);

        if (m_content_browsers.size() > 0)
            ImGui::DockBuilderDockWindow(m_content_browsers[0].get_window_id().c_str(), dock_bottom);

        ImGui::DockBuilderFinish(dockspace_id);
    }


    void world_viewport_window::render_viewport() {

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ImageRounding, 0.0f);
        if (ImGui::Begin("Viewport")) {

            m_viewport_size = ImGui::GetContentRegionAvail();
            ImGui::Image(m_renderer->get_rendered_image(), m_viewport_size);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }


    void world_viewport_window::render_details() {

        if (ImGui::Begin("Details")) {

        }
        ImGui::End();
    }


    void world_viewport_window::render_tools() {

        if (ImGui::Begin("Tools")) {

        }
        ImGui::End();
    }

}

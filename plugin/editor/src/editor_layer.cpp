
#include <util/pch.h>
#include "editor_layer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <application.h>
#include <event/event_bus.h>
#include <event/application_event.h>
#include <config/imgui_config.h>
#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_renderer_plugin.h>
#include <render/image.h>

#include "resource_manager/icon_manager.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr const char*                   MAIN_MENU_BAR_POPUP = "MAIN_MENU_BAR_POPUP";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // Draws a single top-level menu entry ("File", "Edit", ...) that looks
    // like plain text but highlights on hover / while its popup is open.
    template <typename F>
    void toolbar_menu(const char* label, F&& popup_content);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template <typename F>
    void toolbar_menu(const char* label, const char* popup_label, F&& popup_content) {

        constexpr f32 pad_x    = 12.0f;
        constexpr f32 pad_y    =  6.0f;
        constexpr f32 buffer   = 10.0f;   // <-- hover-leave tolerance in pixels

        const ImVec2 text_size = ImGui::CalcTextSize(label);
        const ImVec2 item_size = ImVec2(text_size.x + pad_x * 2.0f, text_size.y + pad_y * 2.0f);

        ImGui::PushID(label);
        ImGui::InvisibleButton("##hit", item_size);

        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked();
        const ImVec2 rmin = ImGui::GetItemRectMin();
        const ImVec2 rmax = ImGui::GetItemRectMax();

        if (clicked) {
            ImGui::SetNextWindowPos(ImVec2(rmin.x, rmax.y + 2.0f));
            ImGui::OpenPopup(popup_label);
        }

        const bool open = ImGui::IsPopupOpen(popup_label);

        // highlight ---------------------------------------------------------------------------------------------------
        if (hovered || open) {
            const ImU32 bg = ImGui::GetColorU32(open ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered);
            ImGui::GetWindowDrawList()->AddRectFilled(rmin, rmax, bg, 4.0f);
        }

        // label -------------------------------------------------------------------------------------------------------
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(rmin.x + pad_x, rmin.y + pad_y),
            ImGui::GetColorU32(ImGuiCol_Text),
            label);

        // dropdown ----------------------------------------------------------------------------------------------------
        if (ImGui::BeginPopup(popup_label)) {

            ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
            popup_content();
            ImGui::PopItemFlag();

            // auto-close on hover-leave (with buffer) -----------------------------------------------------------------
            const ImVec2 p_min = ImGui::GetWindowPos();
            const ImVec2 p_max = ImVec2(p_min.x + ImGui::GetWindowSize().x, p_min.y + ImGui::GetWindowSize().y);
            const ImVec2 mp = ImGui::GetMousePos();
            const bool in_button =
                mp.x >= rmin.x - buffer && mp.x <= rmax.x + buffer &&
                mp.y >= rmin.y - buffer && mp.y <= rmax.y + buffer;
            const bool in_popup =
                mp.x >= p_min.x - buffer && mp.x <= p_max.x + buffer &&
                mp.y >= p_min.y - buffer && mp.y <= p_max.y + buffer;
            if (!in_button && !in_popup)
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    editor_layer::editor_layer() 
        : layer("editor_layer") {

        m_renderer = GLT::plugin_manager::get_plugin_ref<GLT::render::i_renderer_plugin>(GLT::plugin_manager::interface::renderer);
        m_logo = GLT::create_unique_ref<GLT::render::image>(std::filesystem::path(
            GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "image/logo.png"));
    }


    editor_layer::~editor_layer() {

        m_logo.reset();
        m_renderer.reset();
    }

    // CLASS PUBLIC ====================================================================================================
    
    void editor_layer::update(const f32 /*delta_time*/) {

        m_renderer->set_render_size({m_content_size.x, m_content_size.y});
    }


    void editor_layer::render_imgui(const f32 /*delta_time*/) {

        render_toolbar();
        render_dockspace();

        render_viewport();
        render_content_browser();
        render_details();
        render_tools();

        if (m_show_demo)  ImGui::ShowDemoWindow(&m_show_demo);
        if (m_show_style) ImGui::ShowStyleEditor();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void editor_layer::render_toolbar() {

        constexpr f32 logo_side  = 50.0f;
        constexpr f32 win_pad    =  4.0f;
        constexpr f32 menu_pad_y =  6.0f;
        constexpr f32 toolbar_height = logo_side + win_pad * 2.0f;

        const ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, toolbar_height));
        ImGui::SetNextWindowViewport(vp->ID);

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoDocking;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(win_pad, win_pad));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        if (ImGui::Begin("##editor_toolbar", nullptr, flags)) {

            ImGui::Image(m_logo->get_descriptor_set(), ImVec2(logo_side, logo_side), ImVec2(0, 0), ImVec2(1, 1), 
                GLT::imgui_config::get_main_color_ref(), ImVec4(0, 0, 0, 0));

            ImGui::SameLine();
            const f32 line_h    = ImGui::GetTextLineHeight();
            const f32 menu_h    = line_h + menu_pad_y * 2.0f;

            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("File", MAIN_MENU_BAR_POPUP, []{
                if (ImGui::MenuItem("New",     "Ctrl + N"))             { /* TODO */ }
                if (ImGui::MenuItem("Open Project  ", "Ctrl + O"))      { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Save",    "Ctrl + S"))             { /* TODO */ }
                if (ImGui::MenuItem("Save As", "Ctrl + Shift + S"))     { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt + F4"))                { GLT::event_bus::post(GLT::window_close_event()); }
            });

            ImGui::SameLine();
            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("Edit", MAIN_MENU_BAR_POPUP, []{
                if (ImGui::MenuItem("Undo", "Ctrl + Z"))                { /* TODO */ }
                if (ImGui::MenuItem("Redo", "Ctrl + Y"))                { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Preferences"))                     { /* TODO */ }
            });

            ImGui::SameLine();
            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("View", MAIN_MENU_BAR_POPUP, [this]{
                if (ImGui::MenuItem("Reset Layout"))                    { m_reset_layout = true; }
                ImGui::Separator();

                if (ImGui::MenuItem("Viewport"))                        { /* TODO */ }
                if (ImGui::MenuItem("Logo"))                            { /* TODO */ }

                #if defined(DEBUG)
                    ImGui::SeparatorText("Debug");
                    ImGui::MenuItem("Show Demo", "", &m_show_demo);
                    ImGui::MenuItem("Show Style", "", &m_show_style);
                #endif
            });

            ImGui::SameLine();
            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("Help", MAIN_MENU_BAR_POPUP, []{
                if (ImGui::MenuItem("About"))                           { /* TODO */ }
            });
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
    }


    void editor_layer::render_dockspace() {

        // Same math as render_toolbar() — keep these in sync if you tweak the toolbar.
        constexpr f32 logo_side      = 50.0f;
        constexpr f32 win_pad        =  4.0f;
        constexpr f32 toolbar_height = logo_side + win_pad * 2.0f;

        const ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + toolbar_height));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - toolbar_height));
        ImGui::SetNextWindowViewport(vp->ID);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("##editor_dockspace", nullptr, flags);
        ImGui::PopStyleVar(3);

        const ImGuiID dockspace_id = ImGui::GetID("editor_dockspace");
        const ImVec2  dockspace_size(vp->WorkSize.x, vp->WorkSize.y - toolbar_height);

        // Build the layout on the very first frame, after a manual reset,
        // or if the docking data is missing from the .ini (e.g. deleted file).
        const bool no_layout_yet = (ImGui::DockBuilderGetNode(dockspace_id) == nullptr);
        if (m_reset_layout || no_layout_yet) {
            m_reset_layout = false;
            build_default_layout(dockspace_id, dockspace_size);
        }

        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End();
    }


    void editor_layer::build_default_layout(ImGuiID dockspace_id, const ImVec2& size) {

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, size);

        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);
        ImGuiID dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, nullptr, &dock_main);
        ImGuiID dock_right_b = ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Down, 0.50f, nullptr, &dock_right);

        ImGui::DockBuilderDockWindow("Viewport",        dock_main);
        ImGui::DockBuilderDockWindow("Content Browser", dock_bottom);
        ImGui::DockBuilderDockWindow("Details",         dock_right);
        ImGui::DockBuilderDockWindow("Tools",           dock_right_b);

        ImGui::DockBuilderFinish(dockspace_id);
    }


    void editor_layer::render_viewport() {

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin("Viewport")) {
            m_content_size = ImGui::GetContentRegionAvail();
            ImGui::Image(m_renderer->get_rendered_image(), m_content_size);
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }


    void editor_layer::render_content_browser() {

        if (ImGui::Begin("Content Browser")) {

            const auto folder_data = icon_manager::get(icon_manager::icon::folder);
            ImGui::Image(folder_data.tex_ref, folder_data.image_size, folder_data.uv0, folder_data.uv1);
        }
        ImGui::End();
    }


    void editor_layer::render_details() {

        if (ImGui::Begin("Details"))
            ImGui::Text("Details");
        ImGui::End();
    }


    void editor_layer::render_tools() {

        if (ImGui::Begin("Tools"))
            ImGui::Text("Tools");
        ImGui::End();
    }

}

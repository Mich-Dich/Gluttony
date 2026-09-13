
#include <util/pch.h>

#include <imgui.h>

#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_renderer_plugin.h>

#include "editor_layer.h"



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

            popup_content();

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
        m_logo = GLT::create_unique_ref<GLT::render::image>(std::filesystem::path(GLT::util::get_executable_path() / "assets/image/logo_small.jpeg"));
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

        render_toolbar();                   // draw the custom toolbar first, so it sits under nothing else
    
        if (m_show_demo)    { ImGui::ShowDemoWindow(); }
        if (m_show_style)   { ImGui::ShowStyleEditor(); }

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Viewport", nullptr)) {

            m_content_size = ImGui::GetContentRegionAvail();        // update the window size
            ImGui::Image(m_renderer->get_rendered_image(), m_content_size);
        }
        ImGui::End();
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
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));
        if (ImGui::Begin("##editor_toolbar", nullptr, flags)) {

            ImGui::Image(m_logo->get_descriptor_set(), ImVec2(logo_side, logo_side));

            ImGui::SameLine();
            const f32 line_h    = ImGui::GetTextLineHeight();
            const f32 menu_h    = line_h + menu_pad_y * 2.0f;

            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("File", MAIN_MENU_BAR_POPUP, []{
                if (ImGui::MenuItem("New",     "Ctrl+N"))           { /* TODO */ }
                if (ImGui::MenuItem("Open...", "Ctrl+O"))           { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Save",    "Ctrl+S"))           { /* TODO */ }
                if (ImGui::MenuItem("Save As", "Ctrl+Shift+S"))     { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit"))                        { /* TODO */ }
            });

            ImGui::SameLine();
            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("Edit", MAIN_MENU_BAR_POPUP, []{
                if (ImGui::MenuItem("Undo", "Ctrl+Z"))              { /* TODO */ }
                if (ImGui::MenuItem("Redo", "Ctrl+Y"))              { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Preferences"))                 { /* TODO */ }
            });

            ImGui::SameLine();
            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("View", MAIN_MENU_BAR_POPUP, []{
                if (ImGui::MenuItem("Viewport"))                    { /* TODO */ }
                if (ImGui::MenuItem("Logo"))                        { /* TODO */ }
            });

            ImGui::SameLine();
            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("Help", MAIN_MENU_BAR_POPUP, []{
                if (ImGui::MenuItem("About"))                       { /* TODO */ }
            });
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
    }

}

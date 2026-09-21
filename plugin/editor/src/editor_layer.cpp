
#include <util/pch.h>
#include "editor_layer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <application.h>
#include <event/event_bus.h>
#include <event/application_event.h>
#include <config/imgui_config.h>
#include <render/image.h>

#include "window/content_browser.h"
#include "window/world_viewport.h"
#include "window/image_viewer.h"
#include "window/log_viewer.h"
#include "window/stats.h"
#include "window/audio_viewer.h"
#include "window/plugin_wizard.h"
#include "window/asset_import.h"
#include "util/asset_editor_registry.h"
#include "util/file_watcher.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr const char*                   MAIN_MENU_BAR_POPUP = "MAIN_MENU_BAR_POPUP";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // Draws a single top-level menu entry ("File", "Edit", ...) that looks like plain text but highlights on hover / while its popup is open.
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

        m_logo = GLT::create_unique_ref<GLT::render::image>(GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "image/logo.png");
        add_window<world_viewport_window>();

        register_core_editors();
        file_watcher::init();
        file_watcher::watch(PROJECT_CONTENT_DIR, true);
        m_asset_open_event_sub_handle = GLT::event_bus::subscribe<asset_open_event>(std::bind_front(&editor_layer::on_asset_open_event, this));
        m_asset_import_request_event_sub_handle = GLT::event_bus::subscribe<asset_import_request_event>(std::bind_front(&editor_layer::on_asset_import_request_event, this));
    }


    editor_layer::~editor_layer() {

        GLT::event_bus::unsubscribe(m_asset_open_event_sub_handle);
        file_watcher::unwatch_all();
        file_watcher::shutdown();
        m_windows.clear();
        m_logo.reset();
    }

    // CLASS PUBLIC ====================================================================================================Fugaxe
    
    void editor_layer::update(const f32 delta_time) {

		for (const auto& editor_window : m_windows)
			editor_window->update(delta_time);

		// First pass to mark items for removal
		auto it = std::remove_if(m_windows.begin(), m_windows.end(),
			[](const unique_ref<base_window>& editor_window) {
				return editor_window->should_close();
			});

		// Erase the removed items
		m_windows.erase(it, m_windows.end());

        for (auto& event : m_asset_open_event_buffer)
            open_asset_editor(event);

        for (auto& event : m_asset_import_request_event_buffer)
            add_window<asset_import_window>(event.get_sources(), event.get_target_dir());
            
        m_asset_open_event_buffer.clear();
        m_asset_import_request_event_buffer.clear();
    }


    void editor_layer::render_imgui(const f32 delta_time) {

        render_toolbar();
        render_dockspace();

        for (auto& window : m_windows)
            window->window(delta_time);

        if (m_show_demo)            ImGui::ShowDemoWindow(&m_show_demo);
        if (m_show_style)           ImGui::ShowStyleEditor();
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    base_window* editor_layer::find_window_by_name(const std::string& name) {

        for (auto& window : m_windows) {

            VALIDATE(window, continue, "", "Null pointer detected in [m_windows]")

            if (window->should_close())                 // Ignore windows the user has closed - they're about to be pruned
                continue;

            if (window->get_window_title() == name)
                return window.get();
        }

        return nullptr;
    }


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
            toolbar_menu("Window", MAIN_MENU_BAR_POPUP, [this]{

                if (ImGui::MenuItem("New Log View"))
                    add_window<log_viewer_window>();

                if (ImGui::MenuItem("New Plugin Wizard"))
                    add_window<plugin_wizard_window>();

                if (ImGui::MenuItem("Debug Statistics", "", m_show_stats_window)) {

                    if (auto* window = find_window_by_name("Debug Statistics"))
                        window->close_window();
                    else
                        add_window<stats_window>();
                }
            });

            ImGui::SameLine();
            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("View", MAIN_MENU_BAR_POPUP, [this]{
                
                ImGui::SeparatorText("Layout");
                if (ImGui::MenuItem("Reset Layout"))
                    m_reset_layout = true;
                                
                ImGui::SeparatorText("Main color");
                static ImVec4 backup_color;
                static bool saved_palette_init = true;
                static ImVec4 saved_palette[35] = {};

                ImGui::Text("change main-color");
                if (saved_palette_init) {
                    for (size_t n = 0; n < ARRAY_SIZE(saved_palette); n++) {

                        ImGui::ColorConvertHSVtoRGB((n / 34.f), .8f, .8f, saved_palette[n].x, saved_palette[n].y, saved_palette[n].z);
                        saved_palette[n].w = 1.0f; // Alpha
                    }
                    saved_palette_init = false;
                }

                if (ImGui::ColorPicker4("##picker", (float*)&GLT::imgui_config::get_main_color_ref(), ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview))
                    GLT::imgui_config::update_ui_colors(GLT::imgui_config::get_main_color_ref());

                ImGui::SameLine();
                ImGui::BeginGroup();
                {
                    ImGui::BeginGroup();
                    {
                        ImGui::Text("Current");
                        ImGui::ColorButton("##current", GLT::imgui_config::get_main_color_ref(), ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(60, 40));
                    }
                    ImGui::EndGroup();
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    {
                        ImGui::Text("Previous");
                        if (ImGui::ColorButton("##previous", backup_color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(60, 40)))
                            GLT::imgui_config::update_ui_colors(backup_color);
                    }
                    ImGui::EndGroup();

                    ImGui::Separator();
                    ImGui::Text("Palette");
                    for (size_t n = 0; n < ARRAY_SIZE(saved_palette); n++) {
                        ImGui::PushID(n);
                        if ((n % 5) != 0)
                            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.y);

                        ImGuiColorEditFlags palette_button_flags = ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip;
                        if (ImGui::ColorButton("##palette", saved_palette[n], palette_button_flags, ImVec2(21, 21)))
                            GLT::imgui_config::update_ui_colors(ImVec4(saved_palette[n].x, saved_palette[n].y, saved_palette[n].z, GLT::imgui_config::get_main_color_ref().w));

                        // Allow user to drop colors into each palette entry. Note that ColorButton() is already a
                        // drag source by default, unless specifying the ImGuiColorEditFlags_NoDragDrop flag.
                        if (ImGui::BeginDragDropTarget()) {

                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_3F))
                                memcpy((float*)&saved_palette[n], payload->Data, sizeof(float) * 3);

                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IMGUI_PAYLOAD_TYPE_COLOR_4F))
                                memcpy((float*)&saved_palette[n], payload->Data, sizeof(float) * 4);

                            ImGui::EndDragDropTarget();
                        }

                        ImGui::PopID();
                    }
                }
                ImGui::EndGroup();

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

        // Same math as render_toolbar() - keep these in sync if you tweak the toolbar.
        constexpr f32 logo_side      = 50.0f;
        constexpr f32 win_pad        =  4.0f;
        constexpr f32 toolbar_height = logo_side + win_pad * 2.0f;

        const ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + toolbar_height));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - toolbar_height));
        ImGui::SetNextWindowViewport(vp->ID);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoNavFocus
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoSavedSettings;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
        ImGui::Begin("##editor_dockspace", nullptr, flags);
        ImGui::PopStyleVar(3);

        m_dockspace_id = ImGui::GetID("editor_dockspace");
        const ImVec2  dockspace_size(vp->WorkSize.x, vp->WorkSize.y - toolbar_height);

        // Build the layout on the very first frame, after a manual reset,
        // or if the docking data is missing from the .ini (e.g. deleted file).
        if (m_reset_layout || (ImGui::DockBuilderGetNode(m_dockspace_id) == nullptr)) {

            m_reset_layout = false;
            build_default_layout(m_dockspace_id, dockspace_size);

            for (auto& w : m_windows)
                w->dock_to(m_dockspace_id);
        }

        ImGui::DockSpace(m_dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        ImGui::End();
    }


    void editor_layer::build_default_layout(ImGuiID dockspace_id, const ImVec2& size) {

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, size);
        ImGui::DockBuilderFinish(dockspace_id);
    }


    void editor_layer::on_asset_open_event(const asset_open_event& event) {

        VALIDATE(!event.get_path().empty(), return, "", "Can't open a asset editor for an empty path");
        m_asset_open_event_buffer.push_back(event);
    }


    void editor_layer::on_asset_import_request_event(const asset_import_request_event& event) {

        VALIDATE(!event.get_sources().empty(), return, "", "No assets to import provided");
        m_asset_import_request_event_buffer.push_back(event);
    }


    void editor_layer::register_core_editors() {

        #define ADD_ASSET_EDITOR(type, window)                                                      \
            GLT::editor::asset_editor_registry::register_editor(type,                               \
                [](const std::filesystem::path& p) -> GLT::unique_ref<base_window> {                \
                    return GLT::create_unique_ref<window>(p);                                       \
                });

        ADD_ASSET_EDITOR(GLT::asset::core_types::texture2D,     image_viewer_window)
        ADD_ASSET_EDITOR(GLT::asset::core_types::texture3D,     image_viewer_window)
        ADD_ASSET_EDITOR(GLT::asset::core_types::cube_map,      image_viewer_window)
        ADD_ASSET_EDITOR(GLT::asset::core_types::audio,         audio_viewer_window)

        #undef ADD_ASSET_EDITOR
    }


    void editor_layer::open_asset_editor(const asset_open_event& event) {

        const GLT::asset::type type = event.get_asset_type();
        const auto& path = event.get_path();
        VALIDATE(!path.empty(), return, "", "asset_open_event with empty path (type {})", type.value);

        auto window = GLT::editor::asset_editor_registry::create(type, path);
        VALIDATE(window, return, "", "No editor registered for asset type [{}] (path: {})", type.value, path.generic_string());

        // Dock new windows into the main dockspace
        if (m_dockspace_id != 0)
            window->dock_to(m_dockspace_id);

        m_windows.push_back(std::move(window));
    }

}

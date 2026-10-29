
#include <util/pch.h>
#include "editor_layer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <application.h>
#include <event/event_bus.h>
#include <event/application_event.h>
#include <asset/i_asset_registry.h>
#include <config/config.h>
#include <config/imgui_config.h>
#include <render/image.h>
#include <render/i_renderer.h>
#include <game/game_layer.h>
#include <game/game_session.h>
#include <util/ui/asset_picker.h>
#include <util/ui/pannel_collection.h>
#include <util/io/serializer_yaml.h>

#include "window/base/asset_editor.h"
#include "window/content_browser.h"
#include "window/world_viewport.h"
#include "window/texture_viewer.h"
#include "window/log_viewer.h"
#include "window/stats.h"
#include "window/audio_viewer.h"
#include "window/plugin_wizard.h"
#include "window/asset_import.h"
#include "window/asset_create.h"
#include "window/material_viewer.h"
#include "window/mesh_editor.h"
#include "window/world_topdown_editor.h"
#include "window/plugin_manager.h"
#include "util/asset_editor_registry.h"
#include "util/file_watcher.h"
#include "input/editor_controller.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    constexpr const char*                   MAIN_MENU_BAR_POPUP = "MAIN_MENU_BAR_POPUP";
        
    constexpr size_t                        MAX_NOTIFICATIONS = 6;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // Draws a single top-level menu entry ("File", "Edit", ...) that looks like plain text but highlights on hover / while its popup is open
    template <typename F>
    void toolbar_menu(const char* label, F&& popup_content);

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    template <typename F>
    void toolbar_menu(const char* label, const char* popup_label, F&& popup_content) {

        constexpr f32 pad_x = 12.0f;
        constexpr f32 pad_y =  6.0f;
        constexpr f32 buffer = 10.0f;   // <-- hover-leave tolerance in pixels

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

    editor_layer::editor_layer() : layer("editor_layer") {

        auto& scene_renderers = GLT::application::get().get_scene_renderers_ref();
        scene_renderers.front()->set_active(false);

        m_logo = GLT::create_unique_ref<GLT::render::image>(GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "image/logo.png");
        add_window<world_viewport_window>();

        register_core_editors();
        file_watcher::init();
        file_watcher::watch(PROJECT_CONTENT_DIR, true);
        GLT::UI::set_asset_root(PROJECT_CONTENT_DIR);
        m_asset_open_event_sub_handle = GLT::event_bus::subscribe<asset_open_event>(std::bind_front(&editor_layer::on_asset_open_event, this));
        m_asset_import_request_event_sub_handle = GLT::event_bus::subscribe<asset_import_request_event>(std::bind_front(&editor_layer::on_asset_import_request_event, this));
        m_key_sub_handle = GLT::event_bus::subscribe<key_event>(std::bind_front(&editor_layer::on_key_event, this));
        m_notification_sub_handle = GLT::event_bus::subscribe<notification_event>(std::bind_front(&editor_layer::on_notification_event, this));
        m_asset_create_request_event_sub_handle = GLT::event_bus::subscribe<asset_create_request_event>(std::bind_front(&editor_layer::on_asset_create_request_event, this));
        m_save_sub_handle = GLT::event_bus::subscribe<GLT::save_event>(std::bind_front(&editor_layer::on_save_event, this));
        m_window_close_sub = GLT::event_bus::subscribe<GLT::window_close_event>(std::bind_front(&editor_layer::on_window_close_event, this));

        if (auto* game_layer = GLT::application::get().get_game_layer_ptr()) {
            if (auto* session = game_layer->get_primary_session()) {

                // primary session is editor's working world, physics must not step on it
                session->remove_physics();
                session->set_controller(GLT::create_ref<GLT::editor::input::editor_controller>(session));
            }
        }

        load_simulation_settings();

        // load the editor world
        const auto& editor_world_path = GLT::application::get().get_project().editor_start_world;
        if (!editor_world_path.empty()) {

            auto registry = GLT::asset::registry::get_ref();
            VALIDATE(registry, return, "", "Failed to get asset-registry")

            const auto result = registry->load(editor_world_path);                                      // registry CAN find the editor world
            if (result)
                (void)GLT::application::get().load_world(editor_world_path, true);                            // load project world if none loaded
            else
                GLT::application::get().get_project().editor_start_world = std::filesystem::path{};     // reset if asset was not found
        }
    }


    editor_layer::~editor_layer() {

        GLT::event_bus::unsubscribe(m_window_close_sub);
        GLT::event_bus::unsubscribe(m_save_sub_handle);
        GLT::event_bus::unsubscribe(m_asset_create_request_event_sub_handle);
        GLT::event_bus::unsubscribe(m_notification_sub_handle);
        GLT::event_bus::unsubscribe(m_key_sub_handle);
        GLT::event_bus::unsubscribe(m_asset_import_request_event_sub_handle);
        GLT::event_bus::unsubscribe(m_asset_open_event_sub_handle);
        file_watcher::unwatch_all();
        file_watcher::shutdown();
        m_windows.clear();
        m_logo.reset();

        auto& scene_renderers = GLT::application::get().get_scene_renderers_ref();
        scene_renderers.front()->set_active(true);
    }

    // CLASS PUBLIC ====================================================================================================

    void editor_layer::on_attach() { }


    void editor_layer::on_detach() { GLT::event_bus::post(GLT::save_event()); }


    void editor_layer::update(const f32 delta_time) {
        
        for (const auto& editor_window : m_windows)
            editor_window->update(delta_time);

        auto it = std::remove_if(m_windows.begin(), m_windows.end(), [](const unique_ref<base_window>& w) { return w->should_close(); });
        m_windows.erase(it, m_windows.end());

        // The asset-opened buffers
        for (auto& event : m_asset_open_event_buffer)
            open_asset_editor(event);

        for (auto& event : m_asset_import_request_event_buffer)
            add_window<asset_import_window>(event.get_sources(), event.get_target_dir());

        for (auto& event : m_notification_event_buffer)
            add_notification(event);

        for (auto& event : m_asset_create_request_event_buffer)
            add_window<asset_create_window>(event.get_target_dir());

        m_asset_create_request_event_buffer.clear();
        m_notification_event_buffer.clear();
        m_asset_open_event_buffer.clear();
        m_asset_import_request_event_buffer.clear();
    }


    void editor_layer::render_imgui(const f32 delta_time) {

        ImGuizmo::BeginFrame();
        // ImGui::ShowIDStackToolWindow();

        if (m_start_sims) {
            start_simulation();
            m_start_sims = false;
        }

        render_toolbar();
        render_dockspace();

        for (auto& window : m_windows)
            window->window(delta_time);

        if (m_show_demo)           
            ImGui::ShowDemoWindow(&m_show_demo);

        if (m_show_style)          
            ImGui::ShowStyleEditor();
    
        render_notifications(delta_time);
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

        constexpr f32 logo_side = 50.0f;
        constexpr f32 win_pad =  4.0f;
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
            const f32 line_h = ImGui::GetTextLineHeight();
            const f32 menu_h = line_h + menu_pad_y * 2.0f;

            ImGui::SetCursorPosY(win_pad + (logo_side - menu_h) * 0.5f);
            toolbar_menu("File", MAIN_MENU_BAR_POPUP, []{
                if (ImGui::MenuItem("New",     "Ctrl + N"))             { /* TODO */ }
                if (ImGui::MenuItem("Open Project  ", "Ctrl + O"))      { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Save",    "Ctrl + S"))             { /* TODO */ }
                if (ImGui::MenuItem("Save As", "Ctrl + Shift + S"))     { /* TODO */ }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt + F4")) {
                    GLT::event_bus::post(GLT::window_close_event(GLT::application::get().get_main_window()));
                }
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

                if (ImGui::MenuItem("Plugin Manager", "", &m_show_plugin_manager_window)) {

                    if (auto* window = find_window_by_name("Plugin Manager"))
                        window->close_window();
                    else
                        add_window<plugin_manager_window>();
                }

                if (ImGui::MenuItem("Debug Statistics", "", &m_show_stats_window)) {

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
                        // drag source by default, unless specifying the ImGuiColorEditFlags_NoDragDrop flag
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

            render_simulation_controls();
        }
        ImGui::End();

        ImGui::PopStyleVar(2);
    }


    void editor_layer::render_simulation_controls() {

        using namespace GLT::editor::icon_manager;

        constexpr f32 WIN_PAD = 4.f;        // matches render_toolbar()
        constexpr f32 LOGO_SIDE = 50.f;
        constexpr f32 BTN_SIZE = 24.f;      // click footprint
        constexpr f32 ICON_SIZE = 20.f;     // rendered icon (centred in the footprint)
        constexpr f32 BTN_SPACING = 4.f;

        const bool idle = (m_simulation_state == GLT::system_state::idle);
        const bool running = (m_simulation_state == GLT::system_state::running);
        const bool paused = (m_simulation_state == GLT::system_state::paused);

        u32 visible = 0;
        if (idle)
            visible = 2;                    // Play, Settings

        else if (running)
            visible = 2;                    // Pause, Stop

        else if (paused)
            visible = 3;                    // Resume, Step, Stop

        const f32 total_w = visible * BTN_SIZE + (visible - 1) * BTN_SPACING;

        // Centre in the toolbar window - leaves the right side free for the project name that will land there later
        const f32 x_pos = (ImGui::GetWindowWidth() - total_w) * 0.5f;
        const f32 y_pos = WIN_PAD + (LOGO_SIDE - BTN_SIZE) * 0.5f;

        ImGui::SetCursorPos(ImVec2(x_pos, y_pos));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));

        bool any_drawn = false;
        ImVec2 settings_btn_min{};
        ImVec2 settings_btn_max{};
        bool settings_btn_drawn = false;

        auto tinted_icon_button = [&](icon_manager::icon_type icon_id, const char* tooltip) -> bool {

            if (any_drawn)
                ImGui::SameLine(0, BTN_SPACING);

            any_drawn = true;
            const ImVec2 btn_pos = ImGui::GetCursorScreenPos();

            ImGui::InvisibleButton(tooltip, ImVec2(BTN_SIZE, BTN_SIZE));
            const bool clicked = ImGui::IsItemClicked();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            const auto data = icon_manager::get(icon_id);
            if (data.image_size.x > 0.f && data.image_size.y > 0.f) {

                const ImVec4& tint = active  ? GLT::imgui_config::get_action_color00_active_ref()
                    : hovered ? GLT::imgui_config::get_action_color00_hover_ref()
                    : GLT::imgui_config::get_main_color_ref();

                const ImVec2 img_min = ImVec2(btn_pos.x + (BTN_SIZE - ICON_SIZE) * 0.5f, btn_pos.y + (BTN_SIZE - ICON_SIZE) * 0.5f);
                const ImVec2 img_max = ImVec2(img_min.x + ICON_SIZE, img_min.y + ICON_SIZE);

                ImGui::GetWindowDrawList()->AddImage(data.tex_ref, img_min, img_max, data.uv0, data.uv1, ImGui::GetColorU32(tint));
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltip);

            return clicked;
        };

        // Gray-backed image button - the settings gear keeps its visible frame so it reads as a distinct affordance from the transport row
        auto gray_icon_button = [&](icon_manager::icon_type icon_id, const char* tooltip) -> bool {

            if (any_drawn)
                ImGui::SameLine(0, BTN_SPACING);
                
            any_drawn = true;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.f, 2.f));
            ImGui::PushStyleColor(ImGuiCol_Button, GLT::imgui_config::get_default_gray_ref());
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, GLT::imgui_config::get_action_color_gray_hover_ref());
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, GLT::imgui_config::get_action_color_gray_active_ref());

            const auto data = icon_manager::get(icon_id);
            bool clicked = false;
            if (data.image_size.x > 0.f && data.image_size.y > 0.f)
                clicked = ImGui::ImageButton(tooltip, data.tex_ref, ImVec2(ICON_SIZE, ICON_SIZE), data.uv0, data.uv1);
            else
                clicked = ImGui::Button(tooltip, ImVec2(ICON_SIZE, ICON_SIZE));

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltip);

            return clicked;
        };

        // Play / Resume
        if (idle || paused)
            if (tinted_icon_button(icon::play, idle ? "Play" : "Resume")) {
                if (idle)
                    start_simulation();
                m_simulation_state = GLT::system_state::running;
            }

        // Settings (idle only)
        if (idle) {
            if (gray_icon_button(icon::settings, "Simulation Settings"))
                ImGui::OpenPopup("##sim_settings_popup");

            settings_btn_min = ImGui::GetItemRectMin();
            settings_btn_max = ImGui::GetItemRectMax();
            settings_btn_drawn = true;
        }

        // Pause
        if (running)
            if (tinted_icon_button(icon::pause, "Pause"))
                m_simulation_state = GLT::system_state::paused;

        if (paused)
            if (tinted_icon_button(icon::step, "Step")) {
                // GLT::event_bus::post(GLT::simulation_step_event());
            }

        if (running || paused)
            if (tinted_icon_button(icon::stop, "Stop")) {
                m_simulation_state = GLT::system_state::idle;
                destroy_simulation_windows();       // tearing the sim down also tears the windows down
            }

        ImGui::PopStyleVar();   // outer FramePadding

        // Settings popup
        if (idle && settings_btn_drawn) {
            ImGui::SetNextWindowPos(ImVec2(settings_btn_min.x, settings_btn_max.y + 4.f), ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(440.f, 180.f), ImVec2(FLT_MAX, FLT_MAX));
        }

        if (ImGui::BeginPopup("##sim_settings_popup", ImGuiWindowFlags_AlwaysAutoResize)) {

            if (!idle)
                ImGui::CloseCurrentPopup();

            else {

                // Fixed table width flips begin_table() into its fixed/stretch column mode - without this, ImGui's default SizingFixedFit
                // collapses both columns to content width and the combo never gets room to grow
                constexpr f32 TABLE_W = 420.f;
                if (UI::begin_table("##sim_settings_tbl", false, ImVec2(TABLE_W, 0.f))) {

                    const std::vector<std::string> mode_options = {
                        "In current viewport",
                        "In a new window",
                    };
                    int mode_idx = static_cast<int>(m_simulation_mode);
                    if (UI::table_row("Mode", mode_idx, mode_options))
                        m_simulation_mode = static_cast<simulation_mode>(mode_idx);

                    // window count
                    if (m_simulation_mode != simulation_mode::current_viewport) {
                        int count = static_cast<int>(m_sim_window_count);
                        if (UI::table_row_slider_int("Window count", count, 1, 4))
                            m_sim_window_count = static_cast<u32>(count);
                    }

                    // ---- per-window config ----
                    if (m_simulation_mode != simulation_mode::current_viewport) {

                        auto& layout = m_sim_layouts[m_sim_window_count - 1];
                        for (u32 index = 0; index < m_sim_window_count; index++) {

                            auto& cfg = layout.windows[index];
                            ImGui::PushID(static_cast<int>(index));

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextDisabled("Window %u", index + 1);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Separator();

                            UI::table_row("Position", cfg.position, 1.f, 0.f, 8000.f);
                            UI::table_row("Size", cfg.size, 1.f, 100.f, 8000.f);
                            UI::table_row("Maximized", cfg.maximized);
                            UI::table_row("VSync", cfg.vsync);

                            ImGui::PopID();
                        }
                    }

                    UI::end_table();
                }
            }

            ImGui::EndPopup();
        }
    }


    void editor_layer::render_dockspace() {

        // Same math as render_toolbar() - keep these in sync if you tweak the toolbar
        constexpr f32 logo_side = 50.0f;
        constexpr f32 win_pad =  4.0f;
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
        // or if the docking data is missing from the .ini (e.g. deleted file)
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

        VALIDATE(!event.get_path().empty(), return, "", "Can't open a asset editor for an empty path")
        m_asset_open_event_buffer.push_back(event);
    }


    void editor_layer::on_asset_import_request_event(const asset_import_request_event& event) {

        VALIDATE(!event.get_sources().empty(), return, "", "No assets to import provided")
        m_asset_import_request_event_buffer.push_back(event);
    }


    void editor_layer::on_key_event(const GLT::key_event& event) {

        static bool control_pressed = false;
        static bool alt_pressed = false;
        static bool shift_pressed = false;
        
        if (event.is_key_code(GLT::key_code::key_left_control) || event.is_key_code(GLT::key_code::key_right_control))
            control_pressed = !event.is_key_state(GLT::key_state::release);             // For hold and pressed

        if (event.is_key_code(GLT::key_code::key_left_alt) || event.is_key_code(GLT::key_code::key_right_alt))
            alt_pressed = !event.is_key_state(GLT::key_state::release);                 // For hold and pressed

        if (event.is_key_code(GLT::key_code::key_left_shift) || event.is_key_code(GLT::key_code::key_right_shift))
            shift_pressed = !event.is_key_state(GLT::key_state::release);               // For hold and pressed

        if (control_pressed && event.is(GLT::key_code::key_S, GLT::key_state::press))
            GLT::event_bus::post(GLT::save_event(shift_pressed));

        if (alt_pressed && event.is(GLT::key_code::key_F4, GLT::key_state::press))
            GLT::event_bus::post(GLT::window_close_event(GLT::application::get().get_main_window()));

        if (event.is(GLT::key_code::key_F8, GLT::key_state::press))
            release_sim_cursor();

        // Clicking a sim window that isn't captured captures it. This is the "recapture after F8" path, focus doesn't change there,
        // so the focus event won't fire
        if (event.is(GLT::key_code::mouse_bu_left, GLT::key_state::press)) {
            auto* sim = find_sim_instance(event.get_window());
            if (sim && !sim->cursor_captured)
                capture_sim_window(*sim);
        }

        if (event.is(GLT::key_code::key_F5, GLT::key_state::press))
            m_start_sims = true;
    }


    void editor_layer::register_core_editors() {

        #define ADD_ASSET_EDITOR(type, window)                                                      \
            GLT::editor::asset_editor_registry::register_editor(type,                               \
                [](const std::filesystem::path& p) -> GLT::unique_ref<base_window> {                \
                    return GLT::create_unique_ref<window>(p);                                       \
                });

        ADD_ASSET_EDITOR(GLT::asset::core_types::material,      material_viewer_window)
        ADD_ASSET_EDITOR(GLT::asset::core_types::static_mesh,   mesh_editor_window)
        ADD_ASSET_EDITOR(GLT::asset::core_types::audio,         audio_viewer_window)
        ADD_ASSET_EDITOR(GLT::asset::core_types::world,         world_topdown_editor)

        #undef ADD_ASSET_EDITOR
    }


    void editor_layer::open_asset_editor(const asset_open_event& event) {

        const GLT::asset::type type = event.get_asset_type();
        const auto& path = event.get_path();
        VALIDATE(!path.empty(), return, "", "asset_open_event with empty path (type {})", (type.value))

        // Already open? Bring it forward
        const auto wanted = path.lexically_normal();

        for (auto& w : m_windows) {

            if (w->should_close())
                continue;                                              // about to be pruned; treat as gone

            if (auto* asset_editor = dynamic_cast<GLT::editor::asset_editor*>(w.get())) {

                if (asset_editor->get_asset_path().lexically_normal() == wanted) {
                    asset_editor->focus_window();
                    return;
                }
            }
        }

        // Not open yet: create
        auto window = GLT::editor::asset_editor_registry::create(type, path);
        VALIDATE(window, return, "", "No editor registered for asset type [{}] (path: {})", (type.value, path.generic_string()))

        if (m_dockspace_id != 0)
            window->dock_to(m_dockspace_id);

        m_windows.push_back(std::move(window));
    }


    void editor_layer::on_save_event(const GLT::save_event& /*event*/) {

        GLT::application::get().get_project_ref().serialize_projects_data(GLT::serializer::option::save);
        save_simulation_settings();
    }


    void editor_layer::on_window_close_event(const GLT::window_close_event& event) {

        const auto event_handle = event.get_window();

        auto it = std::ranges::find_if(m_simulation_instances,
            [event_handle](const simulation_instance& w) { return w.handle == event_handle; });
        if (it == m_simulation_instances.end())
            return;

        auto* game_layer = GLT::application::get().get_game_layer_ptr();

        if (it->session && it->scene_renderer)
            it->session->remove_renderer(it->scene_renderer);

        if (it->scene_renderer) {
            GLT::application::get().destroy_scene_renderer(it->scene_renderer);
            it->scene_renderer = nullptr;
        }

        if (auto* p_window = GLT::platform::window::get_ptr())
            p_window->destroy(it->handle);

        if (it->session && game_layer)
            game_layer->destroy_session(it->session->get_id());
        it->session = nullptr;

        m_simulation_instances.erase(it);

        if (m_simulation_instances.empty())
            m_simulation_state = GLT::system_state::idle;
    }

    // notification ----------------------------------------------------------------------------------------------------

    // Buffered: the event can fire from anywhere in the frame, we only mutate the stack during our update pass
    void editor_layer::on_notification_event(const notification_event& event) { m_notification_event_buffer.push_back(event); }


    void editor_layer::add_notification(const notification_event& event) {

        notification notif{
            .title = event.get_title(),
            .description = event.get_description(),
            .severity = event.get_severity(),
            .lifetime = display_time_for(notif.severity),      // < 0 => persistent
            .initial_lifetime = notif.lifetime,
            .id = m_next_notification_id++,
            .dismissed = false,
        };
        m_notifications.push_back(std::move(notif));

        // Hard cap the visible stack - drop the oldest first (front of the vector)
        if (m_notifications.size() > MAX_NOTIFICATIONS)
            m_notifications.erase(m_notifications.begin(), m_notifications.begin() + (m_notifications.size() - MAX_NOTIFICATIONS));
    }


    void editor_layer::render_notifications(const f32 delta_time) {

        // tick lifetimes
        for (auto& notif : m_notifications)
            if (notif.lifetime >= 0.0f)
                notif.lifetime -= (delta_time / 1000.f);

        std::erase_if(m_notifications, [](const notification& n) { return n.dismissed || n.lifetime <= 0.0f; });

        if (m_notifications.empty())
            return;

        // ---- anchor to bottom-right of the main viewport -------------------------------
        constexpr f32 margin = 12.0f;
        constexpr f32 notif_width = 340.0f;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 anchor(vp->WorkPos.x + vp->WorkSize.x - margin, vp->WorkPos.y + vp->WorkSize.y - margin);

        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(notif_width, 0.0f), ImVec2(notif_width, FLT_MAX));

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoNav
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

        if (ImGui::Begin("##notifications_stack", nullptr, flags)) {

            // Render oldest -> newest so that the newest sits at the bottom (window grows upward)
            for (auto& notif : m_notifications) {

                ImGui::PushID(static_cast<int>(notif.id));

                const ImVec4 sev_col = color_for(notif.severity);
                ImVec4 bg_col = sev_col;  bg_col.w *= 0.22f;
                ImVec4 border_col = sev_col; border_col.w = 0.65f;

                const f32 alpha = (notif.lifetime >= 0.0f) ? 1.0f : GLT::math::clamp(1.0f + notif.lifetime / 0.25f, 0.0f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, bg_col);
                ImGui::PushStyleColor(ImGuiCol_Border,  border_col);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,   2.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,   ImVec2(10.0f, 8.0f));

                if (ImGui::BeginChild("##notif", ImVec2(0, 0), 
                    ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar)) {

                    // ---- header: title (colored) + right-aligned close button ----
                    ImGui::PushStyleColor(ImGuiCol_Text, sev_col);
                    ImGui::TextUnformatted(notif.title.c_str());
                    ImGui::PopStyleColor();

                    const f32 btn_size = ImGui::GetFrameHeight();
                    const f32 target_x = ImGui::GetWindowContentRegionMax().x - btn_size;
                    ImGui::SameLine();
                    if (ImGui::GetCursorPosX() < target_x)
                        ImGui::SetCursorPosX(target_x);

                    if (ImGui::SmallButton("x"))
                        notif.dismissed = true;

                    // ---- body ----
                    if (!notif.description.empty()) {
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextUnformatted(notif.description.c_str());
                        ImGui::PopTextWrapPos();
                    }

                    // ---- auto-dismiss progress ----
                    if (notif.lifetime >= 0.0f && notif.initial_lifetime > 0.0f) {
                        const f32 t = notif.lifetime / notif.initial_lifetime;
                        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, sev_col);
                        ImGui::ProgressBar(t, ImVec2(-FLT_MIN, 1.0f), "");
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::EndChild();

                ImGui::PopStyleVar(4);
                ImGui::PopStyleColor(2);

                ImGui::PopID();
            }
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);

        // Dock spaces and docked windows keep their z-order across frames, so the notification stack can end up behind them as
        // soon as the user clicks any editor window. Force it to the top of the display list every frame
        if (ImGuiWindow* window = ImGui::FindWindowByName("##notifications_stack"))
            ImGui::BringWindowToDisplayFront(window);
    }


    f32 editor_layer::display_time_for(GLT::logger::severity sev) {

        using sev_t = GLT::logger::severity;
        switch (sev) {
            case sev_t::trace:      return  2.0f;
            case sev_t::debug:      return  3.0f;
            case sev_t::info:       return  4.5f;
            case sev_t::warn:       return  6.5f;
            case sev_t::error:      return  9.0f;
            case sev_t::fatal:      return -1.0f;   // never auto-dismiss
            default:                return  4.5f;
        }
    }


    ImVec4 editor_layer::color_for(GLT::logger::severity sev) {

        using sev_t = GLT::logger::severity;
        switch (sev) {
            case sev_t::trace:      return ImVec4(.65f, .65f, .65f, 1.f);
            case sev_t::debug:      return ImVec4(.55f, .75f, .95f, 1.f);
            case sev_t::info:       return ImVec4(.40f, .85f, .50f, 1.f);
            case sev_t::warn:       return ImVec4(.95f, .80f, .30f, 1.f);
            case sev_t::error:      return ImVec4(.95f, .40f, .35f, 1.f);
            case sev_t::fatal:      return ImVec4(.85f, .20f, .60f, 1.f);
            default:                return ImVec4( 1.f,  1.f,  1.f, 1.f);
        }
    }


    void editor_layer::on_asset_create_request_event(const asset_create_request_event& event) { 

        m_asset_create_request_event_buffer.push_back(event);
    }


    void editor_layer::save_simulation_settings() {

        const auto path = GLT::application::get().get_project_path() / GLT::config::config_type_to_filepath(GLT::config::type::app_settings);

        bool success = false;
        GLT::serializer::yaml cfg(path, "simulation_settings", GLT::serializer::option::save, &success);
        VALIDATE(success, return, "", "simulation_settings: could not open [{}] for writing", (path.generic_string()))

        i32 mode_int = static_cast<i32>(m_simulation_mode);
        cfg.entry("mode", mode_int);

        i32 count_int = static_cast<i32>(m_sim_window_count);
        cfg.entry("window_count", count_int);

        // Save every layout — index 0 = 1 window, index 3 = 4 windows
        // Only the meaningful entries are written; the file stays compact
        for (u32 layout_idx = 0; layout_idx < 4; ++layout_idx) {

            const u32 layout_count = layout_idx + 1;
            const auto& layout = m_sim_layouts[layout_idx];

            cfg.sub_section("layout_" + std::to_string(layout_count), [&](GLT::serializer::yaml& y) {

                for (u32 win_idx = 0; win_idx < layout_count; ++win_idx) {

                    const auto& w = layout.windows[win_idx];

                    // Copy to locals so entry() gets non-const refs and we
                    // don't touch the live state on the load path
                    f32  px = w.position.x;
                    f32  py = w.position.y;
                    f32  sx = w.size.x;
                    f32  sy = w.size.y;
                    bool m = w.maximized;
                    bool v = w.vsync;

                    y.sub_section("window_" + std::to_string(win_idx),
                        [&](GLT::serializer::yaml& y2) {
                            y2.entry("pos_x", px);
                            y2.entry("pos_y", py);
                            y2.entry("size_x", sx);
                            y2.entry("size_y", sy);
                            y2.entry("maximized", m);
                            y2.entry("vsync", v);
                        });
                }
            });
        }
    }


    void editor_layer::load_simulation_settings() {

        const auto path = GLT::application::get().get_project_path() / GLT::config::config_type_to_filepath(GLT::config::type::app_settings);
        std::error_code ec{};
        if (!GLT::vfs::exists(path, ec) || ec)
            return;                                     // first run — defaults are fine

        bool success = false;
        GLT::serializer::yaml cfg(path, "simulation_settings", GLT::serializer::option::load, &success);
        VALIDATE(success, return, "", "simulation_settings: could not parse [{}]", (path.generic_string()))

        // Global state. Clamp on load so a hand-edited file can't put us into
        // an out-of-range enum value
        i32 mode_int = static_cast<i32>(m_simulation_mode);
        cfg.entry("mode", mode_int);
        m_simulation_mode = static_cast<simulation_mode>(std::clamp<i32>(mode_int, 0, 2));

        i32 count_int = static_cast<i32>(m_sim_window_count);
        cfg.entry("window_count", count_int);
        m_sim_window_count = static_cast<u32>(std::clamp<i32>(count_int, 1, 4));

        // ---- per-count layouts ----
        for (u32 layout_idx = 0; layout_idx < 4; ++layout_idx) {

            const u32 layout_count = layout_idx + 1;
            auto& layout = m_sim_layouts[layout_idx];

            cfg.sub_section("layout_" + std::to_string(layout_count), [&](GLT::serializer::yaml& y) {

                for (u32 win_idx = 0; win_idx < layout_count; ++win_idx) {

                    auto& w = layout.windows[win_idx];

                    // Seed with current values so a partial file leaves
                    // missing fields at their defaults rather than 0
                    f32  px = w.position.x;
                    f32  py = w.position.y;
                    f32  sx = w.size.x;
                    f32  sy = w.size.y;
                    bool m = w.maximized;
                    bool v = w.vsync;

                    y.sub_section("window_" + std::to_string(win_idx),
                        [&](GLT::serializer::yaml& y2) {
                            y2.entry("pos_x", px);
                            y2.entry("pos_y", py);
                            y2.entry("size_x", sx);
                            y2.entry("size_y", sy);
                            y2.entry("maximized", m);
                            y2.entry("vsync", v);
                        });

                    w.position = { px, py };
                    w.size = { sx, sy };
                    w.maximized = m;
                    w.vsync = v;
                }
            });
        }
    }


    void editor_layer::start_simulation() {
        
        GLT::event_bus::post(GLT::save_event());
        spawn_simulation_windows(); 
        m_simulation_state = GLT::system_state::running;
    }


    void editor_layer::spawn_simulation_windows() {

        if (m_simulation_mode == simulation_mode::current_viewport)
            return;

        auto* p_window = GLT::platform::window::get_ptr();
        VALIDATE(p_window, return, "", "Failed to get window plugin")

        auto* game_layer = GLT::application::get().get_game_layer_ptr();
        VALIDATE(game_layer, return, "", "Failed to get game layer")

        auto* primary = game_layer->get_primary_session();
        VALIDATE(primary, return, "", "No primary session")

        // The asset the primary session is running. Empty if nothing is loaded
        const auto world_asset = primary->world_asset();

        const u32 count = (m_simulation_mode == simulation_mode::new_window) ? 1u : m_sim_window_count;
        const auto& layout = m_sim_layouts[count - 1];
        m_simulation_instances.reserve(count);

        for (u32 index = 0; index < count; index++) {

            const auto& cfg = layout.windows[index];

            GLT::platform::window_attributes attrs{};
            attrs.title = "GLT-game-" + std::to_string(index + 1);
            attrs.width = static_cast<u32>(cfg.size.x);
            attrs.height = static_cast<u32>(cfg.size.y);
            attrs.pos_x = static_cast<u32>(cfg.position.x);
            attrs.pos_y = static_cast<u32>(cfg.position.y);
            attrs.vsync = cfg.vsync;
            attrs.size_state = cfg.maximized
                ? GLT::platform::window_size_state::fullscreen_windowed
                : GLT::platform::window_size_state::windowed;

            const auto handle = p_window->create(attrs);
            VALIDATE(handle.is_valid(), continue, "",
                "Failed to create simulation window {} of {}", (index + 1, count))

            GLT::render::surface_target_desc surface{
                .window = handle
            };
            GLT::render::scene_renderer_desc desc{
                .target = surface,
                .clear_color = glm::vec4(0.09f, 0.09f, 0.09f, 1.f),
                .enable_imgui = false,
            };
            auto* scene_renderer = GLT::application::get().create_scene_renderer(desc);
            if (!scene_renderer) {
                LOG(error, "Failed to create scene renderer for simulation window {}", index + 1)
                p_window->destroy(handle);
                continue;
            }

            auto* session = game_layer->create_session();
            if (!session) {
                LOG(error, "Failed to create simulation session")
                GLT::application::get().destroy_scene_renderer(scene_renderer);
                p_window->destroy(handle);
                continue;
            }

            // give the session a world
            if (world_asset) {
                if (auto r = session->load_world(world_asset); !r) {
                    LOG(error, "Sim session {} failed to load world: error {}", index + 1, static_cast<int>(r.error()))
                    // don't bail, the window will just be empty, which is more debuggable than a silently destroyed window
                }
            } else
                LOG(warn, "no world loaded in primary session, sim window {} will be empty", index + 1)

            auto ctrl = GLT::create_ref<GLT::editor::input::editor_controller>(session);    // TODO: see if the game has a controller set
            ctrl->set_input_enabled(false);                 // starts disabled
            session->set_controller(ctrl);
            session->add_renderer(scene_renderer);
            m_simulation_instances.push_back(simulation_instance{
                .handle = handle,
                .scene_renderer = scene_renderer,
                .session = session,
                .cursor_captured = false,
            });
        }
    }


    void editor_layer::destroy_simulation_windows() {

        if (m_simulation_instances.empty())
            return;

        auto* p_game_layer = GLT::application::get().get_game_layer_ptr();
        auto* p_window = GLT::platform::window::get_ptr();

        for (auto& w : m_simulation_instances) {

            if (w.session && w.scene_renderer)
                w.session->remove_renderer(w.scene_renderer);

            if (w.scene_renderer) {
                GLT::application::get().destroy_scene_renderer(w.scene_renderer);
                w.scene_renderer = nullptr;
            }

            if (p_window)
                p_window->destroy(w.handle);

            if (w.session && p_game_layer)
                p_game_layer->destroy_session(w.session->get_id());
            w.session = nullptr;
        }

        m_simulation_instances.clear();
    }


    editor_layer::simulation_instance* editor_layer::find_sim_instance(GLT::platform::window_handle handle) {

        for (auto& w : m_simulation_instances)
            if (w.handle == handle)
                return &w;
        return nullptr;
    }


    void editor_layer::on_window_focus_event(const GLT::window_focus_event& event) {

        auto* sim = find_sim_instance(event.get_window());
        if (!sim)
            return;

        if (event.get_focused())
            capture_sim_window(*sim);
        else if (sim->cursor_captured)
            release_sim_cursor();
    }


    void editor_layer::capture_sim_window(simulation_instance& instance) {

        auto* window = GLT::platform::window::get_ptr();
        if (!window)
            return;

        // Only one window can own the cursor. Release whatever else might be captured first
        release_sim_cursor();

        // The primary editor camera goes quiet while a sim runs — otherwise a stale
        // right-button state in the editor viewport could drive both cameras at once
        if (auto* game_layer = GLT::application::get().get_game_layer_ptr()) {
            if (auto* primary = game_layer->get_primary_session()) {
                if (auto* ctrl = primary->get_controller())
                    ctrl->set_input_enabled(false);
            }
        }

        window->set_cursor_mode(instance.handle, GLT::platform::cursor_mode::cursor_disabled);

        if (instance.session) {
            if (auto* ctrl = instance.session->get_controller())
                ctrl->set_input_enabled(true);
        }

        instance.cursor_captured = true;
    }


    void editor_layer::release_sim_cursor() {

        auto* window = GLT::platform::window::get_ptr();

        for (auto& w : m_simulation_instances) {

            if (!w.cursor_captured)
                continue;

            if (window)
                window->set_cursor_mode(w.handle, GLT::platform::cursor_mode::cursor_normal);

            if (w.session) {
                if (auto* ctrl = w.session->get_controller())
                    ctrl->set_input_enabled(false);
            }

            w.cursor_captured = false;
        }
    }

}

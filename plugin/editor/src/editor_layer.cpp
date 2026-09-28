
#include <util/pch.h>
#include "editor_layer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <application.h>
#include <event/event_bus.h>
#include <event/application_event.h>
#include <asset/i_asset_registry.h>
#include <config/imgui_config.h>
#include <render/image.h>
#include <render/i_renderer.h>
#include <world/world_layer.h>
#include <util/ui/asset_picker.h>
#include <util/ui/pannel_collection.h>

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
        GLT::UI::set_asset_root(PROJECT_CONTENT_DIR);
        m_asset_open_event_sub_handle = GLT::event_bus::subscribe<asset_open_event>(std::bind_front(&editor_layer::on_asset_open_event, this));
        m_asset_import_request_event_sub_handle = GLT::event_bus::subscribe<asset_import_request_event>(std::bind_front(&editor_layer::on_asset_import_request_event, this));
        m_key_sub_handle = GLT::event_bus::subscribe<key_event>(std::bind_front(&editor_layer::on_key_event, this));
        m_save_sub_handle = GLT::event_bus::subscribe<GLT::save_event>(std::bind_front(&editor_layer::on_save_event, this));
        m_save_as_sub_handle = GLT::event_bus::subscribe<save_as_request_event>(std::bind_front(&editor_layer::on_save_as_request_event, this));
        m_notification_sub_handle = GLT::event_bus::subscribe<notification_event>(std::bind_front(&editor_layer::on_notification_event, this));
        m_asset_create_request_event_sub_handle = GLT::event_bus::subscribe<asset_create_request_event>(std::bind_front(&editor_layer::on_asset_create_request_event, this));

        if (auto* world_layer = GLT::application::get().get_layer_stack_ref().get<GLT::world::world_layer>())
            world_layer->set_controller<GLT::editor::input::editor_controller>();                       // create controller

        // load the editor world
        const auto& editor_world_path = GLT::application::get().get_project().editor_start_world;
        if (!editor_world_path.empty()) {

            auto registry = GLT::asset::registry::get_ref();
            VALIDATE(registry, return, "", "Failed to get asset-registry");

            const auto result = registry->load(editor_world_path);                                      // registry CAN find the editor world
            if (result) {
                GLT::application::get().load_world(editor_world_path, true);                            // load project world if none loaded
            } else
                GLT::application::get().get_project().editor_start_world = std::filesystem::path{};     // reset if asset was not found
        }
    }


    editor_layer::~editor_layer() {

        GLT::event_bus::post(GLT::save_event());                // save before closing

        GLT::event_bus::unsubscribe(m_asset_create_request_event_sub_handle);
        GLT::event_bus::unsubscribe(m_notification_sub_handle);
        GLT::event_bus::unsubscribe(m_save_as_sub_handle);
        GLT::event_bus::unsubscribe(m_save_sub_handle);
        GLT::event_bus::unsubscribe(m_key_sub_handle);
        GLT::event_bus::unsubscribe(m_asset_import_request_event_sub_handle);
        GLT::event_bus::unsubscribe(m_asset_open_event_sub_handle);
        file_watcher::unwatch_all();
        file_watcher::shutdown();
        m_windows.clear();
        m_logo.reset();
    }

    // CLASS PUBLIC ====================================================================================================

    void editor_layer::update(const f32 delta_time) {

        for (const auto& editor_window : m_windows)
            editor_window->update(delta_time);

        auto it = std::remove_if(m_windows.begin(), m_windows.end(), [](const unique_ref<base_window>& w) { return w->should_close(); });
        m_windows.erase(it, m_windows.end());

        // Move any events that arrived during the previous frame into the queue.
        for (auto& ev : m_save_as_event_buffer) {

            pending_save_request pending{};
            pending.req = ev.get();

            std::snprintf(pending.filename, sizeof(pending.filename), "%s", pending.req.default_name.c_str());

            // Picker and registry both speak project-relative - convert here so the modal opens with a path the picker can highlight in its tree
            pending.directory = PROJECT_CONTENT_DIR;
            if (pending.directory.empty())
                pending.directory = pending.req.default_dir;        // fallback: picker still accepts absolute

            m_save_as_queue.push_back(std::move(pending));
        }
        m_save_as_event_buffer.clear();

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

        render_toolbar();
        render_dockspace();

        for (auto& window : m_windows)
            window->window(delta_time);

        if (m_show_demo)           
            ImGui::ShowDemoWindow(&m_show_demo);

        if (m_show_style)          
            ImGui::ShowStyleEditor();
    
        render_notifications(delta_time);
        render_save_as_popup();                 // after every other window
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
            GLT::event_bus::post(GLT::window_close_event());

        if (event.is(GLT::key_code::key_F5, GLT::key_state::press)) {

            // GLT::event_bus::post(GLT::application_refresh_event());
            // GLT::event_bus::post(GLT::notification_event("Refreshed", GLT::Logger::severity::info));
        }
    }


    void editor_layer::on_save_event(const GLT::save_event& event) {

        // save world --------------------------------------------------------------------------------------------------
        auto* world_layer = GLT::application::get().get_layer_stack_ref().get<GLT::world::world_layer>();
        VALIDATE(world_layer, return, "", "Failed to get world layer");

        auto world = world_layer->get_world();
        VALIDATE(world, return, "", "Failed to get world");

        const bool has_handle = world->world_handle() != GLT::asset::handle{};
        const bool force_as = event.is_forced_save_as();
        if (has_handle && !force_as) {

            auto result = world->save_world();
            if (result)
                GLT::event_bus::post(GLT::notification_event("Saved", "World saved successfully", GLT::logger::severity::info));
            else {

                const auto description = std::format("Failed to save World [{}]", GLT::util::enum_to_string(result.error()));
                GLT::event_bus::post(GLT::notification_event("Saved", description, GLT::logger::severity::warn));
            }
            return;
        }

        // need a location from the user
        GLT::save_as_request_event::request req{
            .title = "Save World As",
            .default_name = has_handle
                ? std::string(GLT::asset::registry::get_ref()->info(world->world_handle()).name)
                : std::string("untitled_world"),
            .default_dir = PROJECT_CONTENT_DIR / "world",
            .extension = std::string(GLT::asset::extension_for_type(GLT::asset::core_types::world)),
            .on_resolved = [world](const std::filesystem::path& chosen) {
    
                if (chosen.empty())
                    return;                                     // cancelled
    
                if (auto result = world->save_world_as(chosen); !result)
                    LOG(error, "save_world_as failed: error {}", static_cast<int>(result.error()));
            },
        };

        GLT::event_bus::post(GLT::save_as_request_event(std::move(req)));
    }


    // Buffered so we don't mutate the queue while someone else is iterating - update() drains this into m_save_as_queue
    void editor_layer::on_save_as_request_event(const save_as_request_event& event) { m_save_as_event_buffer.push_back(event); }


    void editor_layer::register_core_editors() {

        #define ADD_ASSET_EDITOR(type, window)                                                      \
            GLT::editor::asset_editor_registry::register_editor(type,                               \
                [](const std::filesystem::path& p) -> GLT::unique_ref<base_window> {                \
                    return GLT::create_unique_ref<window>(p);                                       \
                });

        // ADD_ASSET_EDITOR(GLT::asset::core_types::texture2D,     texture_viewer_window)       // TODO: fix image viewer implementation
        // ADD_ASSET_EDITOR(GLT::asset::core_types::texture3D,     texture_viewer_window)
        // ADD_ASSET_EDITOR(GLT::asset::core_types::cube_map,      texture_viewer_window)
        ADD_ASSET_EDITOR(GLT::asset::core_types::material,      material_viewer_window)
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


    void editor_layer::open_next_save_request() {

        if (m_save_as_open)
            return;                         // one at a time

        if (m_save_as_queue.empty())
            return;

        m_save_as_open = true;
        ImGui::OpenPopup("##save_as_modal");
    }


    void editor_layer::render_save_as_popup() {

        open_next_save_request();

        if (!m_save_as_open)
            return;

        auto& pending = m_save_as_queue.front();

        // Always centered in the main viewport - no ImGuiCond_Appearing.
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(560, 0), ImVec2(FLT_MAX, FLT_MAX));

        bool keep_open = true;
        if (ImGui::BeginPopupModal("##save_as_modal", &keep_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {

            ImGui::TextUnformatted(pending.req.title.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            // ---- directory ----
            ImGui::TextUnformatted("Directory");
            ImGui::SameLine(100.f);
            GLT::UI::draw_directory_picker("##save_as_dir_picker", pending.directory, PROJECT_CONTENT_DIR);

            // ---- filename ----
            ImGui::TextUnformatted("Filename");
            ImGui::SameLine(100.f);
            const std::string ext_hint = "." + pending.req.extension;
            const f32 ext_w = ImGui::CalcTextSize(ext_hint.c_str()).x + 8.f;
            ImGui::SetNextItemWidth(-(ext_w + ImGui::GetStyle().ItemSpacing.x));
            ImGui::InputText("##name", pending.filename, sizeof(pending.filename));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ext_hint.c_str());

            ImGui::Spacing();

            // ---- preview + validation ----
            std::filesystem::path full = pending.directory / pending.filename;
            if (full.extension() != std::filesystem::path(ext_hint))
                full += ext_hint;

            ImGui::TextDisabled("Will write to:");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", full.generic_string().c_str());

            pending.error_message.clear();
            if (pending.directory.empty())
                pending.error_message = "Directory is required.";
            else if (pending.filename[0] == '\0')
                pending.error_message = "Filename is required.";

            if (!pending.error_message.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.f, 0.5f, 0.4f, 1.f), "%s", pending.error_message.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ---- buttons ----
            const bool can_confirm = pending.error_message.empty();

            ImGui::BeginDisabled(!can_confirm);
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                resolve_save_as_request(true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                resolve_save_as_request(false);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                resolve_save_as_request(false);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (!keep_open && m_save_as_open)
            resolve_save_as_request(false);
    }


    void editor_layer::resolve_save_as_request(bool confirmed) {

        if (m_save_as_queue.empty()) {
            m_save_as_open = false;
            return;
        }

        pending_save_request pending = std::move(m_save_as_queue.front());
        m_save_as_queue.pop_front();
        m_save_as_open = false;

        std::filesystem::path resolved{};

        if (confirmed) {

            resolved = pending.directory / pending.filename;
            const std::string ext_hint = "." + pending.req.extension;
            if (resolved.extension() != ext_hint)
                resolved += ext_hint;
        }

        auto callback = std::move(pending.req.on_resolved);
        if (callback)
            callback(resolved);
    }

    // notification ----------------------------------------------------------------------------------------------------

    // Buffered: the event can fire from anywhere in the frame, we only mutate the stack during our update pass.
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

        // Hard cap the visible stack - drop the oldest first (front of the vector).
        if (m_notifications.size() > MAX_NOTIFICATIONS)
            m_notifications.erase(m_notifications.begin(), m_notifications.begin() + (m_notifications.size() - MAX_NOTIFICATIONS));
    }


    void editor_layer::render_notifications(const f32 delta_time) {

        // ---- tick lifetimes ------------------------------------------------------------
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

            // Render oldest -> newest so that the newest sits at the bottom (window grows upward).
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
        // soon as the user clicks any editor window. Force it to the top of the display list every frame.
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

}

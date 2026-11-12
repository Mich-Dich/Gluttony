
#include "util/pch.h"
#include "editor_settings.h"

#include <imgui.h>

#include <config/config.h>
#include <config/imgui_config.h>
#include <util/system.h>
#include <util/io/serializer_yaml.h>
#include <util/io/vfs.h>
#include <util/ui/panel_collection.h>


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

    namespace {

        void save_editor_settings()                     { editor_settings::get().serialize(GLT::serializer::option::save); }


        void save_appearance()                          { GLT::imgui_config::serialize(GLT::serializer::option::save); }

        // ---- Interface ------------------------------------------------------------------------------------------------

        bool draw_appearance_page() {

            using namespace GLT::imgui_config;
            bool changed = false;

            UI::settings_begin_section("Colors");
            {
                static constexpr const char* THEMES[] = { "Dark", "Light" };
                theme_selection theme = get_ui_theme_selection();
                if (UI::settings_segmented("Theme", "Base brightness of panels and windows", theme, theme_selection::dark, THEMES)) {
                    set_ui_theme_selection(theme);
                    update_ui_theme();
                    changed = true;
                }

                ImVec4 accent = get_main_color_ref();
                if (UI::settings_color("Accent color", "Used for buttons, tabs, sliders and highlights", accent, get_default_main_color())) {
                    update_ui_colors(accent, false);            // the window saves once the user lets go
                    changed = true;
                }
            }
            UI::settings_end_section();

            UI::settings_begin_section("Size");
            {
                i32 percent = static_cast<i32>(std::round(get_font_scale() * 100.f));
                if (UI::settings_slider("Interface scale", "Size of all text in the editor", percent, 100, 75, 150, "%d %%")) {
                    set_font_scale(static_cast<f32>(percent) / 100.f);
                    changed = true;
                }
            }
            UI::settings_end_section();

            return changed;
        }


        bool draw_notifications_page() {

            auto& cfg = editor_settings::get().notifications;
            const auto& def = editor_settings::defaults().notifications;
            bool changed = false;

            UI::settings_begin_section("Pop-ups");
            changed |= UI::settings_toggle("Show notifications", "Short messages such as \"World saved\" or import errors",
                cfg.enabled, def.enabled);
            changed |= UI::settings_slider("Maximum on screen", "Older ones disappear when more arrive", cfg.max_visible, def.max_visible, 1, 12);
            UI::settings_end_section();

            UI::settings_begin_section("Display time", "How long a notification stays before it fades out");
            changed |= UI::settings_slider("Info", nullptr, cfg.info_seconds, def.info_seconds, 1.f, 30.f, "%.1f s");
            changed |= UI::settings_slider("Warning", nullptr, cfg.warning_seconds, def.warning_seconds, 1.f, 30.f, "%.1f s");
            changed |= UI::settings_slider("Error", nullptr, cfg.error_seconds, def.error_seconds, 1.f, 60.f, "%.1f s");
            changed |= UI::settings_toggle("Keep errors until dismissed", "Errors don't fade out, click them away", cfg.keep_errors, def.keep_errors);
            UI::settings_end_section();

            return changed;
        }

        // ---- World editor -----------------------------------------------------------------------------------------

        bool draw_viewport_page() {

            auto& cfg = editor_settings::get();
            const auto& def = editor_settings::defaults();
            bool changed = false;

            UI::settings_begin_section("Camera movement", "Hold the right mouse button in the viewport to fly: W A S D, Space up, Shift down");
            changed |= UI::settings_slider("Fly speed", "Distance per frame. Also changes with the mouse wheel while flying",
                cfg.camera.fly_speed, def.camera.fly_speed, 0.01f, 5.f, "%.2f", ImGuiSliderFlags_Logarithmic);
            changed |= UI::settings_toggle("Mouse wheel changes speed", nullptr, cfg.camera.scroll_changes_speed, def.camera.scroll_changes_speed);
            UI::settings_end_section();

            UI::settings_begin_section("Mouse look");
            changed |= UI::settings_slider("Look sensitivity", nullptr, cfg.camera.look_sensitivity, def.camera.look_sensitivity,
                0.002f, 0.05f, "%.3f", ImGuiSliderFlags_Logarithmic);
            changed |= UI::settings_toggle("Invert vertical look", "Moving the mouse up looks down", cfg.camera.invert_y, def.camera.invert_y);
            UI::settings_end_section();

            UI::settings_begin_section("Selection highlight", "Outline color of selected entities in the viewport");
            changed |= UI::settings_color("Selected entity", "The one shown in the Details panel", cfg.selection.primary_color,
                def.selection.primary_color, true);
            changed |= UI::settings_color("Other selected entities", nullptr, cfg.selection.secondary_color, def.selection.secondary_color, true);
            UI::settings_end_section();

            return changed;
        }


        bool draw_gizmo_page() {

            auto& cfg = editor_settings::get().gizmo;
            const auto& def = editor_settings::defaults().gizmo;
            bool changed = false;

            UI::settings_begin_section("Behavior", "Both can also be switched on the viewport toolbar");
            static constexpr const char* SPACES[] = { "World", "Local" };
            changed |= UI::settings_segmented("Coordinate space", "Move along the world axes or the entity's own axes", cfg.space, def.space, SPACES);
            changed |= UI::settings_toggle("Snap", "Move, rotate and scale in fixed steps", cfg.snap, def.snap);
            UI::settings_end_section();

            UI::settings_begin_section("Snap steps");
            changed |= UI::settings_drag("Move", "Per axis (X, Y, Z)", cfg.snap_translate, def.snap_translate, 0.1f, 0.f, 100.f);
            changed |= UI::settings_drag("Rotate", nullptr, cfg.snap_rotate, def.snap_rotate, 1.f, 0.f, 180.f, "%.1f deg");
            changed |= UI::settings_drag("Scale", nullptr, cfg.snap_scale, def.snap_scale, 0.01f, 0.f, 10.f);
            UI::settings_end_section();

            return changed;
        }


        bool draw_history_page() {

            auto& cfg = editor_settings::get().history;
            const auto& def = editor_settings::defaults().history;
            bool changed = false;

            UI::settings_begin_section("Undo / Redo", "Ctrl+Z undoes, Ctrl+Y or Ctrl+Shift+Z redoes");
            changed |= UI::settings_slider("Steps to remember", "Each window keeps its own history, older steps are dropped",
                cfg.max_steps, def.max_steps, 16, 1024);
            changed |= UI::settings_toggle("Select what changed", "Undo and redo select the entities they changed",
                cfg.select_after_undo, def.select_after_undo);
            UI::settings_end_section();

            return changed;
        }

    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_editor_settings_pages(settings_registry& registry) {

        registry.add({ "Interface", "Appearance", "Theme, accent color and size of the editor interface", &draw_appearance_page, &save_appearance });
        registry.add({ "Interface", "Notifications", "Messages that pop up in the bottom right corner", &draw_notifications_page, &save_editor_settings });

        registry.add({ "World Editor", "Viewport", "Editor camera and selection highlight", &draw_viewport_page, &save_editor_settings });
        registry.add({ "World Editor", "Transform Gizmo", "Moving, rotating and scaling entities in the viewport", &draw_gizmo_page, &save_editor_settings });
        registry.add({ "World Editor", "Undo History", "How undo and redo behave", &draw_history_page, &save_editor_settings });
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    editor_settings& editor_settings::get() {

        static editor_settings s_settings{};
        return s_settings;
    }


    const editor_settings& editor_settings::defaults() {

        static const editor_settings s_defaults{};
        return s_defaults;
    }


    void editor_settings::serialize(const GLT::serializer::option option) {

        const auto path = GLT::util::get_executable_path() / GLT::config::config_type_to_filepath(GLT::config::type::editor);

        std::error_code error{};
        if (option == GLT::serializer::option::load && (!GLT::vfs::exists(path, error) || error))
            return;                                             // first start: defaults

        bool success = false;
        GLT::serializer::yaml file(path, "editor_settings", option, &success);
        VALIDATE(success, return, "", "editor settings: could not open [{}]", (path.generic_string()))

        file.sub_section("camera", [this](GLT::serializer::yaml& section) {
            section.entry("fly_speed", camera.fly_speed)
                .entry("look_sensitivity", camera.look_sensitivity)
                .entry("scroll_changes_speed", camera.scroll_changes_speed)
                .entry("invert_y", camera.invert_y);
        });

        file.sub_section("gizmo", [this](GLT::serializer::yaml& section) {
            section.entry("space", gizmo.space)
                .entry("snap", gizmo.snap)
                .entry("snap_translate", gizmo.snap_translate)
                .entry("snap_rotate", gizmo.snap_rotate)
                .entry("snap_scale", gizmo.snap_scale);
        });

        file.sub_section("selection", [this](GLT::serializer::yaml& section) {
            section.entry("primary_color", selection.primary_color)
                .entry("secondary_color", selection.secondary_color);
        });

        file.sub_section("history", [this](GLT::serializer::yaml& section) {
            section.entry("max_steps", history.max_steps)
                .entry("select_after_undo", history.select_after_undo);
        });

        file.sub_section("notifications", [this](GLT::serializer::yaml& section) {
            section.entry("enabled", notifications.enabled)
                .entry("max_visible", notifications.max_visible)
                .entry("info_seconds", notifications.info_seconds)
                .entry("warning_seconds", notifications.warning_seconds)
                .entry("error_seconds", notifications.error_seconds)
                .entry("keep_errors", notifications.keep_errors);
        });

        // Values from a hand-edited file could be anything
        history.max_steps = std::clamp(history.max_steps, 16, 1024);
        notifications.max_visible = std::clamp(notifications.max_visible, 1, 12);
        camera.fly_speed = std::clamp(camera.fly_speed, 0.01f, 5.f);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

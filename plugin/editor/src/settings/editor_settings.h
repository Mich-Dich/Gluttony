
#pragma once

#include "settings/settings_registry.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class gizmo_space : u8 {
        world = 0,
        local,
    };


    // Every editor preference in one place. Per user (stored next to the executable in config/editor.config), not per
    // project. Appearance (theme, accent color, UI scale) lives in imgui_config / ui.config because the launcher uses it too.
    //
    // Default values are the member initializers - editor_settings::defaults() is just a default constructed instance,
    // that's what "reset to default" uses.
    struct editor_settings {

        struct camera_settings {

            f32                                 fly_speed = 0.15f;                      // units per frame
            f32                                 look_sensitivity = 0.01f;               // degrees per mouse unit
            bool                                scroll_changes_speed = true;
            bool                                invert_y = false;
        };

        struct gizmo_settings {

            gizmo_space                         space = gizmo_space::world;
            bool                                snap = false;
            glm::vec3                           snap_translate{ 1.f, 1.f, 1.f };
            f32                                 snap_rotate = 15.f;                     // degrees
            f32                                 snap_scale = 0.1f;
        };

        struct selection_settings {

            glm::vec4                           primary_color{ 1.f, 0.5f, 0.1f, 1.f };
            glm::vec4                           secondary_color{ 1.f, 0.7f, 0.3f, 0.55f };
        };

        struct history_settings {

            i32                                 max_steps = 256;
            bool                                select_after_undo = true;
        };

        struct notification_settings {

            bool                                enabled = true;
            i32                                 max_visible = 6;
            f32                                 info_seconds = 4.5f;
            f32                                 warning_seconds = 6.5f;
            f32                                 error_seconds = 9.f;
            bool                                keep_errors = false;                    // errors stay until clicked away
        };

        camera_settings                         camera{};
        gizmo_settings                          gizmo{};
        selection_settings                      selection{};
        history_settings                        history{};
        notification_settings                   notifications{};


        // The live settings
        [[nodiscard]] static editor_settings& get();

        // Factory defaults
        [[nodiscard]] static const editor_settings& defaults();

        void serialize(const GLT::serializer::option option);
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Adds the built-in editor pages (Appearance, Notifications, Viewport, Transform Gizmo, Undo History)
    void register_editor_settings_pages(settings_registry& registry);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

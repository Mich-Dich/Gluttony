#pragma once

#include <window/base/base_window.h>
#include <asset/type.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::material {
    class material_asset;
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Modal-ish "create new asset" wizard. Non-dockable, appears centered in the main viewport on first show
    // Owns the target type, directory, name, auto-open flag, and per-type settings. On Create it registers an in-memory asset
    // with the registry and saves it to disk - the same register_runtime + save flow the content browser used to run inline
    //
    // Layout is split into three vertical zones: a padded content area (fields + preview), a separator, and a
    // footer pinned to the bottom edge that carries the status hint and the action buttons
    class asset_create_window : public base_window {
    public:

        explicit asset_create_window(std::filesystem::path target_dir);

        ~asset_create_window() override;


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        // Create windows are deliberately non-dockable - they are modal-ish and always centered
        void dock_to(ImGuiID dock_id) override;

    private:

        // A type the user can create from scratch. Types that only make sense to import (meshes, images) are not listed here
        struct creatable_type {

            GLT::asset::type                                            type;
            const char*                                                 label;
            const char*                                                 tooltip;
        };

        // ---- static data ---------------------------------------------------------------------------------------------

        [[nodiscard]] static std::span<const creatable_type>            creatable_types();

        // ---- drawing helpers ----------------------------------------------------------------------------------------
        void                                                            draw_asset_section();

        void                                                            draw_settings_section();

        // Footer bar pinned to the bottom of the window. `footer_height` is the reserved height so the content child can subtract it from its own
        void                                                            draw_footer(f32 footer_height);

        // Attempts to build + register + save the asset. On success, closes the window (and optionally fires asset_open_event)
        // On failure, populates m_operation_error and returns false
        bool                                                            try_create();


        // ---- state --------------------------------------------------------------------------------------------------

        std::filesystem::path                                           m_target_dir{};
        GLT::asset::type                                                m_target_type{ GLT::asset::core_types::material };
        char                                                            m_name_buffer[256]{};
        bool                                                            m_auto_open = true;
        bool                                                            m_center_next_frame = false;

        // Recomputed every frame from the current field values. Drives the enabled state of the Create button and the footer hint
        std::string                                                     m_validation_error{};

        // Set only by try_create() when an operation fails (disk write, missing registry, ...). Cleared when the user edits a field,
        // so it survives the per-frame validation pass instead of being wiped
        std::string                                                     m_operation_error{};
    };

}

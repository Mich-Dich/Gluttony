
#pragma once

#include <window/base_window.h>
#include <asset/type.h>
#include <plugin_system/i_asset_factory_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Modal-ish import wizard. Non-dockable, appears centered the first time it
    // is opened. Owns per-file state: target name, target type, engine toggles,
    // factory-provided options, per-row status, and the shared target directory.
    class asset_import_window : public base_window {
    public:

        asset_import_window(std::vector<std::filesystem::path> sources, std::filesystem::path target_dir);

        ~asset_import_window() override;


        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        void dock_to(ImGuiID dock_id) override;

    private:

        // A single editable option, backed by one option_descriptor from a
        // factory's option_schema(). Only the field matching desc.type is
        // meaningful; the others are inert.
        struct option_field {

            GLT::asset::factory::i_asset_factory_plugin::option_descriptor  desc{};
            bool                                                            bool_value = false;
            i32                                                             int_value = 0;
            f32                                                             float_value = 0.0f;
            std::string                                                     string_value{};     // enumeration / path
        };


        struct import_item {

            std::filesystem::path                                           source;
            std::string                                                     target_name;        // editable, no extension
            GLT::asset::type                                                target_type{};
            std::vector<GLT::asset::factory::binding>                       candidates;
            std::string                                                     status;

            // Engine-level toggles (mirrored into import_options on submit).
            bool                                                            editor_preview_only = false;
            bool                                                            strip_editor_data = false;

            // Factory-provided options. Rebuilt whenever target_type changes.
            std::vector<option_field>                                       options{};

            // Set on successful import. Drives the green header tint; cleared whenever name or target type is edited.
            bool                                                            imported = false;
        };


        void rebuild_candidates();

        void rebuild_options_for(import_item& item);

        void draw_item_section(import_item& item, int index);

        void draw_option_field(option_field& field, int index);

        void draw_footer();

        // only_index < 0  -> import every eligible item ("Import All").
        // only_index >= 0 -> import just that item (per-item button).
        void start_imports(int only_index = -1);

        [[nodiscard]] std::filesystem::path resolved_target_path(const import_item& item) const;

        // True if a file already sits at resolved_target_path(item).
        [[nodiscard]] bool target_exists(const import_item& item) const;

        // Serializes the item's option_fields into the type_specific blob that
        // import_options hands to the factory. Layout (little-endian):
        //   boolean      : u8
        //   integer      : i32
        //   real         : f32
        //   enumeration  : u32 length + UTF-8 bytes
        //   path         : u32 length + UTF-8 bytes
        [[nodiscard]] std::vector<std::byte> pack_type_specific(const import_item& item) const;


        std::vector<import_item>                                            m_items{};
        std::filesystem::path                                               m_target_dir{};
        bool                                                                m_center_next_frame = false;
        bool                                                                m_importing = false;
        bool                                                                m_override_existing = false;
        bool                                                                m_close_when_all_done = false;
        u32                                                                 m_imports_pending = 0;

    };

}


#pragma once

#include <TextEditor.h>

#include "window/base_window.h"
#include "plugin_system/i_plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Interactive wizard for scaffolding a new plugin from inside the editor.
    //
    // Layout (left / right split, resizable):
    //
    //   +-- settings (450 px) -------------+-- preview ------------------------+
    //   |  Identity                       |  src/entry_point.cpp              |
    //   |  Lifecycle                      |  CMakeLists.txt                   |
    //   |  Dependencies                   |                                   |
    //   |  Build config                   |                                   |
    //   |  Vendor repositories            |                                   |
    //   |  [ status ] [Reset] [Create]    |                                   |
    //   +---------------------------------+-----------------------------------+
    //
    // Produces this layout under the chosen base directory:
    //   <base_dir>/<name>/
    //     CMakeLists.txt
    //     src/entry_point.cpp
    //     [asset/]
    //
    // The wizard writes directly to disk — nothing is buffered in the project
    // file. Generated files are intentionally close to the existing plugin
    // templates so they can be hand-edited afterwards.
    class plugin_wizard_window : public base_window {
    public:

        plugin_wizard_window();
        ~plugin_wizard_window();

        void window(f32 delta_time) override;

        void update(f32 delta_time) override;

        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

        // ---- enums ---------------------------------------------------------

        enum class target_root : u8 {

            project = 0,
            engine,
        };


        enum class dependency_mode : u8 {

            by_name = 0,
            by_interface,
        };


        enum class vendor_ref_mode : u8 {

            none = 0,
            branch,
            tag,
        };


        // ---- data ----------------------------------------------------------

        // A third-party repository pulled in by the plugin's CMakeLists.txt
        // via git_clone_or_update().
        struct vendor_entry {

            std::string                                 url{};
            std::string                                 alias{};                    // falls back to url_stem(url)
            vendor_ref_mode                             ref_mode = vendor_ref_mode::none;
            std::string                                 ref_value{};                // branch or tag name
            bool                                        shallow = true;
        };

        // ---- panels (left / right of the split frame) ----------------------
        void draw_settings_panel();

        void draw_preview_panel();

        // ---- settings sections ---------------------------------------------
        void draw_identity_section();

        void draw_lifecycle_section();

        void draw_dependencies_section();

        void draw_build_section();

        void draw_vendor_section();

        void draw_action_bar();

        // ---- popups --------------------------------------------------------
        void draw_add_dependency_popup();

        void draw_add_vendor_popup();

        // ---- actions -------------------------------------------------------
        void on_create_clicked();

        void reset_form();

        void refresh_plugin_path();

        // ---- helpers -------------------------------------------------------
        bool validate_form(std::string& out_error) const;

        std::string namespace_name() const;

        void write_files(const std::filesystem::path& plugin_root);

        // ---- generation ----------------------------------------------------
        std::string build_entry_point_cpp() const;

        std::string build_cmake_lists() const;


        std::string                                     m_plugin_name{"my_new_plugin"};
        std::filesystem::path                           m_plugin_path{};
        target_root                                     m_target_root = target_root::project;

        GLT::plugin_manager::phase                      m_load_phase    = GLT::plugin_manager::phase::application_ready;
        GLT::plugin_manager::phase                      m_unload_phase  = GLT::plugin_manager::phase::pre_application_shutdown;
        GLT::plugin_manager::interface                  m_target_interface = GLT::plugin_manager::interface::custom;

        std::vector<std::string>                        m_plugin_dependencies_name{};
        std::vector<GLT::plugin_manager::interface>     m_plugin_dependencies_interface{};
        std::vector<vendor_entry>                       m_vendors{};

        std::string                                     m_compile_defs{};
        bool                                            m_use_glm = true;
        bool                                            m_use_imgui = true;
        bool                                            m_create_asset_dir = false;

        // Toggle flags consumed by UI::table_row(label, string&, bool&).
        bool                                            m_editing_plugin_name = true;
        bool                                            m_editing_link_libs = true;
        bool                                            m_editing_compile_defs = true;

        std::string                                     m_status_message{};
        bool                                            m_status_is_error = false;

        // add-dependency popup scratch
        bool                                            m_open_add_dependency = false;
        dependency_mode                                 m_new_dep_mode = dependency_mode::by_name;
        std::string                                     m_new_dep_name{};
        GLT::plugin_manager::interface                  m_new_dep_iface = GLT::plugin_manager::interface::none;
        bool                                            m_editing_new_dep_name = true;

        // add-vendor popup scratch
        bool                                            m_open_add_vendor = false;
        std::string                                     m_new_vendor_url{};
        std::string                                     m_new_vendor_alias{};
        vendor_ref_mode                                 m_new_vendor_ref_mode = vendor_ref_mode::none;
        std::string                                     m_new_vendor_ref_value{};
        bool                                            m_new_vendor_shallow = true;
        bool                                            m_editing_new_vendor_url = true;
        bool                                            m_editing_new_vendor_alias = true;
        bool                                            m_editing_new_vendor_ref_value = true;

        TextEditor                                      m_entry_point_editor{};
        TextEditor                                      m_cmake_editor{};

    };

}

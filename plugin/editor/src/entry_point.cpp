
#include "util/pch.h"
#include <plugin_system/i_plugin.h>
#include <editor/i_editor.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {
    class editor_layer;
}

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };
    
    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {
        
        GLT::plugin_manager::interface::renderer,
        GLT::plugin_manager::interface::input_system,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .display_name                                           = "Gluttony Editor - ImGui",
        .description                                            = "The main Gluttony editor, handles most task needed for game creation",
        .category                                               = "Core|UI|Editor",
        .author                                                 = "Mich-Dich",
        .plugin_version                                         = {1, 0, 0},
        .api_version                                            = {1, 0, 0},
        .load_phase                                             = GLT::plugin_manager::phase::application_ready,
        .unload_phase                                           = GLT::plugin_manager::phase::pre_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::editor_core,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // The editor plugin also is the editor extension point for other plugins (GLT::editor::get(), core/src/editor/i_editor.h)
    class plugin : public GLT::plugin_manager::i_plugin, public GLT::editor::i_editor {
    public:

        plugin();
        ~plugin();


        void on_load() override;


        void on_unload() override;

        // i_editor ----------------------------------------------------------------------------------------------------

        void register_asset_editor(std::string_view owner, GLT::asset::type type, GLT::editor::window_factory factory) override;
        void register_asset_creator(std::string_view owner, GLT::editor::asset_creator creator) override;
        void register_menu_item(std::string_view owner, std::string_view menu_path, std::function<void()> action) override;
        void open_window(std::string_view owner, GLT::unique_ref<GLT::editor::i_editor_window> window) override;
        void open_asset(GLT::asset::type type, const std::filesystem::path& asset_path) override;
        [[nodiscard]] GLT::world::i_world* edited_world() override;
        void unregister_all(std::string_view owner) override;

    private:

        weak_ref<editor::editor_layer>          mp_editor_layer{};
    };

}

#include "plugin.inl"

EXPORT_PLUGIN_CLASS(GLT::editor::plugin, GLT::editor::descriptor)

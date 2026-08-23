
#include <plugin_system/i_plugin.h>



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
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
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

    class plugin : public GLT::plugin_manager::i_plugin {
    public:

        plugin();
        ~plugin();


        void on_load() override;


        void on_unload() override;


        void update(const GLT::update_event&) override;

    private:

        weak_ref<editor::editor_layer>          mp_editor_layer{};
    };

}

#include "editor.inl"

EXPORT_PLUGIN_CLASS(GLT::editor::plugin, GLT::editor::descriptor)

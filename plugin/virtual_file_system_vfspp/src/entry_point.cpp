
#include <plugin_system/i_plugin.h>

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::vfs_plugin {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                needed_plugins_names[] = { 
        
        nullptr
    };

    static constexpr plugin_manager::interface                  needed_plugins_interfaces[] = {

        plugin_manager::interface::none
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .load_phase                                             = GLT::plugin_manager::phase::earliest_possible,
        .unload_phase                                           = GLT::plugin_manager::phase::final_cleanup,
        .target                                                 = plugin_manager::interface::virtual_file_system,
        .dependency_names_count                                 = ARRAY_SIZE(needed_plugins_names),
        .dependency_names                                       = needed_plugins_names,
        .dependency_interface_count                             = ARRAY_SIZE(needed_plugins_interfaces),
        .dependency_interfaces                                  = needed_plugins_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class plugin : public GLT::plugin_manager::i_plugin {
    public:

        void on_load() override {

            // TODO:
            //  - read config, should it use [native] [memory] or [zip]

            // CAUTION! - currently only native is supported and for that we use just the engine defaults
            
            LOG_LOADED
        }
        
        void on_unload() override {

            // CAUTION! - currently only native is supported and for that we use just the engine defaults
            LOG_UNLOADED
        }

    };

}

EXPORT_PLUGIN_CLASS(GLT::vfs_plugin::plugin, GLT::vfs_plugin::descriptor)

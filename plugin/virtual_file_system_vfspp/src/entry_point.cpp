
#include <plugin_system/plugin_interface.h>

// #include <util/util.h>

#include "native.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::vfs_plugin {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static const char*                              needed_plugins_names[] = { 
        
        nullptr
    };

    static plugin_manager::targeted_interface       needed_plugins_interfaces[] = {

        plugin_manager::targeted_interface::none
    };

    static plugin_manager::plugin_descriptor        descriptor = {
        .name                                       = GLT_MODULE_NAME,
        .phase                                      = plugin_manager::load_phase::earliest_possible,
        .target                                     = plugin_manager::targeted_interface::virtual_file_system,
        .dependency_names_count                     = ARRAY_SIZE(needed_plugins_names),
        .dependency_names                           = needed_plugins_names,
        .dependency_interface_count                 = ARRAY_SIZE(needed_plugins_interfaces),
        .dependency_interfaces                      = needed_plugins_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class plugin : public GLT::plugin_manager::i_plugin {
    public:

        void on_load() override {

            // Initialize the plugin's VFS backend (vfspp)
            VALIDATE(GLT::vfs_plugin::native::init(), return, "", "Failed to initialize VFS plugin")

            // Build the function table that the core expects
            static const GLT::vfs::vfs_functions plugin_functions = {
                .exists                 = &GLT::vfs_plugin::native::exists,
                .create_file            = &GLT::vfs_plugin::native::create_file,
                .is_directory           = &GLT::vfs_plugin::native::is_directory,
                .is_regular_file        = &GLT::vfs_plugin::native::is_regular_file,
                .create_directory       = &GLT::vfs_plugin::native::create_directory,
                .create_directories     = &GLT::vfs_plugin::native::create_directories,
                .remove                 = &GLT::vfs_plugin::native::remove,
                .rename                 = &GLT::vfs_plugin::native::rename,
                .copy_file              = &GLT::vfs_plugin::native::copy_file,
                .file_size              = &GLT::vfs_plugin::native::file_size,
                .read_text_file         = &GLT::vfs_plugin::native::read_text_file,
                .write_text_file        = &GLT::vfs_plugin::native::write_text_file,
                .open_file              = &GLT::vfs_plugin::native::open_file,
                .read_file              = &GLT::vfs_plugin::native::read_file,
                .write_file             = &GLT::vfs_plugin::native::write_file,
                .seek_file              = &GLT::vfs_plugin::native::seek_file,
                .tell_file              = &GLT::vfs_plugin::native::tell_file,
                .close_file             = &GLT::vfs_plugin::native::close_file,
            };

            // Install the functions into the core
            GLT::vfs::install_vfs_functions(plugin_functions);
            LOG_LOADED
        }
        
        void on_unload() override {

            GLT::vfs_plugin::native::shutdown();
            LOG_UNLOADED
        }

    };

}

EXPORT_PLUGIN_CLASS(GLT::vfs_plugin::plugin, GLT::vfs_plugin::descriptor)

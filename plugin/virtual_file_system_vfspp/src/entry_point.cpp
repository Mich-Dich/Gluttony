
#include <plugin_system/plugin_interface.h>
#include <vfspp/VFS.h>

#include <util/util.h>

#include "vfs.h"

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

    static plugin_manager::plugin_descriptor descriptor = {
        .name                                   = GLT_MODULE_NAME,
        .phase                                  = plugin_manager::load_phase::earliest_possible,
        .target                                 = plugin_manager::targeted_interface::virtual_file_system,
        .dependency_names_count                 = ARRAY_SIZE(needed_plugins_names),
        .dependency_names                       = needed_plugins_names,
        .dependency_interface_count             = ARRAY_SIZE(needed_plugins_interfaces),
        .dependency_interfaces                  = needed_plugins_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class plugin : public GLT::plugin_manager::i_plugin {
    public:

        void on_load() override {

            // 1. Initialize the plugin's VFS backend (vfspp)
            if (!init()) {
                // Log error and maybe prevent engine from starting
                LOG(error, "Failed to initialize VFS plugin");
                return;
            }

            // 2. Build the function table that the core expects
            static const GLT::vfs::vfs_functions plugin_functions = {
                .exists             = &exists_impl,
                .is_directory       = &is_directory_impl,
                .is_regular_file    = &is_regular_file_impl,
                .create_directory   = &create_directory_impl,
                .create_directories = &create_directories_impl,
                .remove             = &remove_impl,
                .rename             = &rename_impl,
                .copy_file          = &copy_file_impl,
                .file_size          = &file_size_impl,
                .list_directory     = &list_directory_impl,
                .read_text_file     = &read_text_file_impl,
                .write_text_file    = &write_text_file_impl,
                .open_file          = &open_file_impl,
                .read_file          = &read_file_impl,
                .write_file         = &write_file_impl,
                .seek_file          = &seek_file_impl,
                .tell_file          = &tell_file_impl,
                .close_file         = &close_file_impl,
            };

            // 3. Install the functions into the core
            GLT::vfs::install_vfs_functions(plugin_functions);

            LOG_LOADED
        }
        
        void on_unload() override {

            GLT::vfs_plugin::shutdown();
            LOG_UNLOADED
        }

    };

}

EXPORT_PLUGIN_CLASS(GLT::vfs_plugin::plugin, GLT::vfs_plugin::descriptor)

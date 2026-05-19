
#include <iostream>
#include <vector>
#include <plugin_system/plugin_interface.h>

#include "logger.h"                 // plugin logger implementation
#include "util/util.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::logger_plugin {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static const char*                              dependencies_names[] = {
        
        nullptr
    };

    static plugin_manager::targeted_interface       dependencies_interfaces[] = {

        plugin_manager::targeted_interface::virtual_file_system 
    };

    static plugin_manager::plugin_descriptor        descriptor = {
        .name                           = GLT_MODULE_NAME,
        .phase                          = plugin_manager::load_phase::earliest_possible,
        .target                         = plugin_manager::targeted_interface::logger,
        .dependency_names_count         = ARRAY_SIZE(dependencies_names),
        .dependency_names               = dependencies_names,
        .dependency_interface_count     = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces          = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class plugin : public GLT::plugin_manager::i_plugin {
    public:

        void on_load() override {

            // Bind plugins function to core
            static const GLT::logger::logger_functions plugin_functions = {
                .init                           = &GLT::logger_plugin::init,
                .shutdown                       = &GLT::logger_plugin::shutdown,
                .log_msg_internal               = &GLT::logger_plugin::log_msg_internal,
                .get_log_file_location          = &GLT::logger_plugin::get_log_file_location,
                .set_format                     = &GLT::logger_plugin::set_format,
                .use_previous_format            = &GLT::logger_plugin::use_previous_format,
                .get_format                     = &GLT::logger_plugin::get_format,
                .set_buffer_threshold           = &GLT::logger_plugin::set_buffer_threshold,
                .set_buffer_size                = &GLT::logger_plugin::set_buffer_size,
                .register_label_for_thread      = &GLT::logger_plugin::register_label_for_thread,
                .unregister_label_for_thread    = &GLT::logger_plugin::unregister_label_for_thread,
            };
            GLT::logger::install_logger_functions(plugin_functions);

            // take over messages from before logger attachment
            GLT::logger::register_label_for_thread("main");             // assume label will remain the same
            std::vector<GLT::logger::message_data> previous_messages = GLT::logger::drain_log_buffer(true);
            for (const auto& msg : previous_messages)
                GLT::logger_plugin::log_msg_internal(msg.msg_sev, msg.file_name, msg.function_name, msg.line,
                    msg.module_name, msg.thread_id, msg.message);       // Only called here directly because I know what im doing

            LOG_LOADED
        }
        
        void on_unload() override {

            LOG_UNLOADED
        }

    };

}

EXPORT_PLUGIN_CLASS(GLT::logger_plugin::plugin, GLT::logger_plugin::descriptor)

#include "util/pch.h"
#include "logger.h"
#include "util/system.h"
#include <iomanip>

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::logger {

    // CONSTANTS =======================================================================================================
            
    constexpr const char* sev_names[] = 
    {
        "TRACE",
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR",
        "FATAL"
    };

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static bool                                 s_use_buffer = true;

    static std::vector<message_data>            s_log_buffer;

    static std::mutex                           s_buffer_mutex;

    // ---------- default (fallback) implementations --------------------------------
    static bool default_init(const std::string& /*format*/, bool /*log_to_console*/, const std::filesystem::path& /*log_dir*/,
        const std::string& /*main_log_file_name*/, bool /*use_append_mode*/) {

        s_use_buffer = false;

        std::vector<GLT::logger::message_data> previous_messages = drain_log_buffer(true);
        for (const auto& msg : previous_messages)
            fprintf(stdout, "[%s] %s\n", sev_names[static_cast<int>(msg.msg_sev)], msg.message.c_str());

        s_log_buffer.clear();
        return true;        // minimal: do nothing, just return success
    }


    static void default_shutdown() {
        
        s_use_buffer = true;
    }


    static void default_log_msg_internal(severity msg_sev, const std::source_location location, const char* module_name, 
        std::thread::id thread_id, std::string message) {
        
        if (message.empty())
            return;

        if (s_use_buffer) {

            std::lock_guard lock(s_buffer_mutex);
            s_log_buffer.emplace_back(msg_sev, location, module_name, thread_id, message);

        } else {
            
            static const char* sev_names[] = {"TRACE","DEBUG","INFO","WARN","ERROR","FATAL"};
            fprintf(stdout, "[%s] %s\n", sev_names[static_cast<int>(msg_sev)], message.c_str());
        }
    }


    static std::filesystem::path default_get_log_file_location() { return {}; }
    
    
    static void default_set_format(const std::string&) { }
    
    
    static void default_use_previous_format() { }
    
    
    static const std::string default_get_format() { return {}; }
    
    
    static void default_set_buffer_threshold(severity) { }
    
    
    static void default_set_buffer_size(size_t) { }
    
    
    static void default_flush_buffer() { }


    static void default_register_label(const std::string&, std::thread::id) { }
    
    
    static void default_unregister_label(std::thread::id) { }

    // ---------- global pointer table ----------------------------------------------
    constexpr const logger_functions default_logger_func = {
        default_init,
        default_shutdown,
        default_log_msg_internal,
        default_get_log_file_location,
        default_set_format,
        default_use_previous_format,
        default_get_format,
        default_set_buffer_threshold,
        default_set_buffer_size,
        default_flush_buffer,
        default_register_label,
        default_unregister_label
    };

    
    static logger_functions g_logger = default_logger_func;
    
    // INTERNAL FUNCTION ===============================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void use_default_logger_functions() {

        install_logger_functions(default_logger_func);
    }

    void install_logger_functions(const logger_functions& funcs) {
        g_logger = funcs;   // safe as long as called before any logging threads run
    }


    bool init(const std::string& format, bool log_to_console, const std::filesystem::path& log_dir, const std::string& main_log_file_name, bool use_append_mode) {
        return g_logger.init(format, log_to_console, log_dir, main_log_file_name, use_append_mode);
    }

    
    void shutdown() {
        g_logger.shutdown();
    }

    
    std::filesystem::path get_log_file_location() {
        return g_logger.get_log_file_location();
    }

    
    void set_format(const std::string& new_format) {
        g_logger.set_format(new_format);
    }

    
    void use_previous_format() {
        g_logger.use_previous_format();
    }

    
    const std::string get_format() {
        return g_logger.get_format();
    }

    
    void set_buffer_threshold(severity new_threshold) {
        g_logger.set_buffer_threshold(new_threshold);
    }

    
    void set_buffer_size(size_t new_size) {
        g_logger.set_buffer_size(new_size);
    }


    void flush_buffer() {
        g_logger.flush_buffer();
    }

    
    void register_label_for_thread(const std::string& thread_label, std::thread::id thread_id) {
        g_logger.register_label_for_thread(thread_label, thread_id);
    }


    void unregister_label_for_thread(std::thread::id thread_id) {
        g_logger.unregister_label_for_thread(thread_id);
    }


    void log_msg_internal(severity msg_sev, const std::source_location location, const char* module_name,
        std::thread::id thread_id, std::string message) {

        g_logger.log_msg_internal(msg_sev, location, module_name, thread_id, std::move(message));
    }


    std::vector<message_data> drain_log_buffer(const bool disable_buffer) {

        s_use_buffer = !disable_buffer;

        std::lock_guard lock(s_buffer_mutex);
        std::vector<message_data> drained;
        drained.swap(s_log_buffer);
        return drained;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

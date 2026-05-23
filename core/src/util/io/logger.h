#pragma once

// #include "util/pch.h"
#include <filesystem>
#include <thread>
#include <vector>

#include "util/macros.h"
#include "util/data_structures/data_types.h"
#include "util/core_config.h"


// FORWARD DECLARATIONS ================================================================================================


namespace GLT::logger {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================
    
    #ifndef LOG_LEVEL_ENABLED

        #if defined(DEBUG)
            #define LOG_LEVEL_ENABLED_DEFAULT_VALUE             6
        #else
            #define LOG_LEVEL_ENABLED_DEFAULT_VALUE             2
        #endif

        // This enables the different log levels
        //  0 =             disabled
        //  1 = FATAL
        //  2 = FATAL + ERROR
        //  3 = FATAL + ERROR + WARN
        //  4 = FATAL + ERROR + WARN + INFO
        //  5 = FATAL + ERROR + WARN + INFO + DEBUG
        //  6 = FATAL + ERROR + WARN + INFO + DEBUG + TRACE
        #define LOG_LEVEL_ENABLED  LOG_LEVEL_ENABLED_DEFAULT_VALUE

    #endif

    #undef ERROR

    #ifndef GLT_MODULE_NAME
        #define GLT_MODULE_NAME "Unknown"
    #endif

    // Compile-time validation: LOG_LEVEL_ENABLED must be in [0,6]
    #if (LOG_LEVEL_ENABLED < 0) || (LOG_LEVEL_ENABLED > 6)
        #error "LOG_LEVEL_ENABLED must be a value between 0 and 6 (inclusive)."
    #endif

    // TYPES ===========================================================================================================

    // Define the severity levels for logging
    // @note severity Enum representing the levels of logging severity
    // @note Trace The lowest level, used for tracing program execution
    // @note Debug Used for detailed debug information
    // @note Info Informational messages that highlight progress
    // @note Warn Messages for potentially harmful situations
    // @note Error Error events that might still allow the application to continue
    // @note Fatal Severe error events that lead to application shutdown
    enum class severity : u8 {
        trace = 0,
        debug,
        info,
        warn,
        error,
        fatal
    };

    
    struct message_data {
        const GLT::logger::severity                             msg_sev;
        const char*                                             file_name;
        const char*                                             function_name;
        const int                                               line;
        const char*                                             module_name;
        const std::thread::id                                   thread_id;
        const std::string                                       message;
        
        message_data(const GLT::logger::severity msg_sev, const char* file_name, const char* function_name, const int line, 
            const char* module_name, std::thread::id thread_id, std::string message)
        : msg_sev(msg_sev), file_name(file_name), function_name(function_name), line(line), module_name(module_name), 
            thread_id(thread_id), message(std::move(message)) {};
    };


    using init_func = bool (*)(const std::string& format, bool log_to_console, const std::filesystem::path& log_dir, 
        const std::string& main_log_file_name, bool use_append_mode);

    using shutdown_func = void (*)();

    using log_msg_internal_func = void (*)(severity msg_sev, const char* file_name, const char* function_name, int line, const char* module_name,
        std::thread::id thread_id, std::string message);
    
    using get_log_file_location_func = std::filesystem::path (*)();

    using set_format_func = void (*)(const std::string& new_format);

    using use_previous_format_func = void (*)();

    using get_format_func = const std::string (*)();

    using set_buffer_threshold_func = void (*)(severity new_threshold);

    using set_buffer_size_func = void (*)(size_t new_size);

    using flush_buffer_func = void (*)();

    using register_label_func = void (*)(const std::string& thread_label, std::thread::id thread_id);

    using unregister_label_func = void (*)(std::thread::id thread_id);
    
    struct logger_functions {
        init_func                       init;
        shutdown_func                   shutdown;
        log_msg_internal_func           log_msg_internal;
        get_log_file_location_func      get_log_file_location;
        set_format_func                 set_format;
        use_previous_format_func        use_previous_format;
        get_format_func                 get_format;
        set_buffer_threshold_func       set_buffer_threshold;
        set_buffer_size_func            set_buffer_size;
        flush_buffer_func               flush_buffer;
        register_label_func             register_label_for_thread;
        unregister_label_func           unregister_label_for_thread;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    void use_default_logger_functions();


    // --- installation function ------------------------------------------------
    // Call once from the plugin to install the full logger backend.
    // Must be called before any logging calls (typically in on_load() of the logger plugin).
    void install_logger_functions(const logger_functions& funcs);

    std::vector<GLT::logger::message_data> drain_log_buffer(const bool disable_buffer = true);

    // Existing function declarations (remain unchanged)
    bool init(const std::string& format, bool log_to_console = false, const std::filesystem::path& log_dir = "./logs", 
        const std::string& main_log_file_name = "general.log", bool use_append_mode = false);
    
    
    void shutdown();
    
    
    std::filesystem::path get_log_file_location();
    
    
    void set_format(const std::string& new_format);
    
    
    void use_previous_format();
    
    
    const std::string get_format();
    
    
    void set_buffer_threshold(severity new_threshold);
    
    
    void set_buffer_size(size_t new_size);
    

    void flush_buffer();

    
    void register_label_for_thread(const std::string& thread_label, std::thread::id thread_id = std::this_thread::get_id());
    
    
    void unregister_label_for_thread(std::thread::id thread_id = std::this_thread::get_id());
    
    
    void log_msg_internal(severity msg_sev, const char* file_name, const char* function_name, int line, const char* module_name,
        std::thread::id thread_id, std::string message);

    // TEMPLATE DECLARATION ============================================================================================

    // if (msg_sev < g_min_severity.load(std::memory_order_relaxed))
    //     return;   // avoid formatting entirely

    // Template version that uses std::format for format strings with arguments
    template<typename... Args>
    FORCE_INLINE void log_msg(const severity msg_sev, const char* file_name, const char* function_name, const int line, const char* module_name,
        std::thread::id thread_id, std::format_string<Args...> fmt, Args&&... args) {

        // // Early exit for empty format string with no arguments (common case)
        // if constexpr (sizeof...(Args) == 0)
        //     return;

        // if (fmt.get()[0] == '\0')
        //     return;   // empty message, skip formatting entirely

        std::string message = std::format(fmt, std::forward<Args>(args)...);
        if (message.empty())             // still check for other sources of emptiness
            return;

        log_msg_internal(msg_sev, file_name, function_name, line, module_name, thread_id, std::move(message));
    }


    // Overload for plain strings
    FORCE_INLINE void log_msg(const severity msg_sev, const char* file_name, const char* function_name, const int line, const char* module_name,
        std::thread::id thread_id, std::string_view message) {
            
        if (message.empty())
            return;

        log_msg_internal(msg_sev, file_name, function_name, line, module_name, thread_id, std::string(message));
    }

    // CLASS DECLARATION ===============================================================================================

    // An exception type that logs the error message immediately when constructed.
    // The exception stores the provided message and also forwards it to the logger
    // with context (file, function, line, thread).
    // @note This class inherits from std::exception so it can be thrown/caught like a standard exception.
    class logged_exception : public std::exception {
		public:

            // Constructs a logged_exception from source location, thread id and a string message.
            template<typename... Args>
			explicit logged_exception(const char* file, const char* function, const int line, const char* module_name,
                std::thread::id thread_id, std::format_string<Args...> fmt, Args&&... args)
            : m_msg(std::format(fmt, std::forward<Args>(args)...)) {
                logger::log_msg_internal(logger::severity::error, file, function, line, module_name, thread_id, m_msg);
            }

            // Overload for plain string
            explicit logged_exception(const char* file, const char* function, const int line, const char* module_name,
                std::thread::id thread_id, const std::string& message)
            : m_msg(message) {
                logger::log_msg_internal(logger::severity::error, file, function, line, module_name, thread_id, m_msg);
            }

            // Overload for C-string
            explicit logged_exception(const char* file, const char* function, const int line, const char* module_name,
                std::thread::id thread_id, const char* message)
            : m_msg(message) {
                logger::log_msg_internal(logger::severity::error, file, function, line, module_name, thread_id, m_msg);
            }

            // Returns a C-string describing the exception. Marked noexcept to match std::exception::what().
            // @return A pointer to a null-terminated C-string containing the stored error message.
            virtual const char* what() const noexcept override { return m_msg.c_str(); }

		private:

            // The stored error message for this exception instance.
            // @note This string is the source for the pointer returned by what().
			std::string m_msg;
	};

}

// MACROS ==============================================================================================================

// Logger support macros -----------------------------------------------------------------------------------------------
//      split macros into severity specific macros that use all the same master to enable severity level specific logging,
//      this will remove unused logs at compile time depending on the severity

#define LOG_Master(severity_level, fmt, ...)                                                                            \
    {                                                                                                                   \
        GLT::logger::log_msg(GLT::logger::severity::severity_level, __FILE__, __FUNCTION__, __LINE__,                   \
            GLT_MODULE_NAME, std::this_thread::get_id(), fmt __VA_OPT__(,) __VA_ARGS__);                                \
    }

#if LOG_LEVEL_ENABLED > 0
    #define LOG_fatal(fmt, ...)      LOG_Master(fatal, fmt __VA_OPT__(,) __VA_ARGS__)
#else
    #define LOG_fatal(fmt, ...)      { }
#endif

#if LOG_LEVEL_ENABLED > 1
    #define LOG_error(fmt, ...)      LOG_Master(error, fmt __VA_OPT__(,) __VA_ARGS__)
#else
    #define LOG_error(fmt, ...)      { }
#endif


#if LOG_LEVEL_ENABLED > 2
    #define LOG_warn(fmt, ...)       LOG_Master(warn, fmt __VA_OPT__(,) __VA_ARGS__)
#else
    #define LOG_warn(fmt, ...)       { }
#endif

#if LOG_LEVEL_ENABLED > 3
    #define LOG_info(fmt, ...)       LOG_Master(info, fmt __VA_OPT__(,) __VA_ARGS__)
#else
    #define LOG_info(fmt, ...)       { }
#endif

#if LOG_LEVEL_ENABLED > 4
    #define LOG_debug(fmt, ...)      LOG_Master(debug, fmt __VA_OPT__(,) __VA_ARGS__)
#else
    #define LOG_debug(fmt, ...)      { }
#endif

#if LOG_LEVEL_ENABLED > 5
    #define LOG_trace(fmt, ...)      LOG_Master(trace, fmt __VA_OPT__(,) __VA_ARGS__)
#else
    #define LOG_trace(fmt, ...)      { }
#endif


// Logger main macro ---------------------------------------------------------------------------------------------------

#define LOG(severity, fmt, ...)      LOG_##severity(fmt __VA_OPT__(,) __VA_ARGS__)


// util ----------------------------------------------------------------------------------------------------------------

#define LOGGED_EXCEPTION(fmt, ...)                                                                                      \
    {                                                                                                                   \
        throw GLT::logger::logged_exception(__FILE__, __FUNCTION__, __LINE__, GLT_MODULE_NAME,                          \
            std::this_thread::get_id(), "LOGGER EXCEPTION: " fmt __VA_OPT__(,) __VA_ARGS__);                            \
    }

#define LOG_INIT                                                            LOG(trace, "init");
#define LOG_SHUTDOWN                                                        LOG(trace, "shutdown");

#define LOG_LOADED                                                          LOG(trace, "loaded");
#define LOG_UNLOADED                                                        LOG(trace, "unloaded");

// Assertion & Validation ----------------------------------------------------------------------------------------------
// In logger.h

// For ASSERT macro
#if defined (PLATFORM_WINDOWS)
    #if ENABLE_LOGGING_FOR_ASSERTS
        #define ASSERT(expr, message_success, message_failure, ...)                                                     \
            if (expr)                                                                                                   \
                LOG(trace, message_success __VA_OPT__(,) __VA_ARGS__)                                                   \
            else {                                                                                                      \
                LOG(fatal, message_failure __VA_OPT__(,) __VA_ARGS__)                                                   \
                DEBUG_BREAK();                                                                                          \
            }

        #define ASSERT_S(expr)                                                                                          \
            if (!(expr)) {                                                                                              \
                LOG(fatal, "Assertion failed: {}", #expr)                                                               \
                DEBUG_BREAK();                                                                                          \
            }
    #else
        #define ASSERT(expr, message_success, message_failure, ...)         if (!(expr)) { DEBUG_BREAK(); }
        #define ASSERT_S(expr)                                              if (!(expr)) { DEBUG_BREAK(); }
    #endif

#elif defined (PLATFORM_LINUX)

    #if ENABLE_LOGGING_FOR_ASSERTS
        #define ASSERT(expr, message_success, message_failure, ...)                                                     \
            if (expr)                                                                                                   \
                LOG(trace, message_success __VA_OPT__(,) __VA_ARGS__)                                                   \
            else {                                                                                                      \
                LOG(fatal, message_failure __VA_OPT__(,) __VA_ARGS__)                                                   \
                LOGGED_EXCEPTION(message_failure __VA_OPT__(,) __VA_ARGS__);                                            \
            }

        #define ASSERT_S(expr)                                                                                          \
            if (!(expr)) {                                                                                              \
                LOG(fatal, "Assertion failed: {}", #expr)                                                               \
                LOGGED_EXCEPTION("Assertion failed: {}", #expr);                                                        \
            }
    #else
        #define ASSERT(expr, message_success, message_failure, ...)         if (!(expr)) { LOGGED_EXCEPTION("Assertion failed: {}", #expr); }
        #define ASSERT_S(expr)                                              if (!(expr)) { LOGGED_EXCEPTION("Assertion failed: {}", #expr); }
    #endif

#endif

// For VALIDATE macro
#if ENABLE_LOGGING_FOR_VALIDATION
    #define VALIDATE(expr, command, message_success, message_failure, ...)                                              \
        if (expr) {                                                                                                     \
            LOG(trace, message_success __VA_OPT__(,) __VA_ARGS__)                                                       \
        } else {                                                                                                        \
            LOG(error, message_failure __VA_OPT__(,) __VA_ARGS__)                                                       \
            command;                                                                                                    \
        }

    #define VALIDATE_S(expr, command)                                                                                   \
        if (!(expr)) {                                                                                                  \
            LOG(error, "Validation failed: {}", #expr)                                                                  \
            command;                                                                                                    \
        }
#else
    #define VALIDATE(expr, command, message_success, message_failure, ...)  if (!(expr)) { command; }
    #define VALIDATE_S(expr, command)                                       if (!(expr)) { command; }
#endif

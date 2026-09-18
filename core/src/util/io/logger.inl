#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::logger {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    template<typename... Args>
    FORCE_INLINE void log_msg(const severity msg_sev, const std::source_location location, const char* module_name,
        std::thread::id thread_id, std::format_string<Args...> fmt, Args&&... args) {

        std::string message = std::format(fmt, std::forward<Args>(args)...);
        if (message.empty())             // still check for other sources of emptiness
            return;

        log_msg_internal(msg_sev, location, module_name, thread_id, std::move(message));
    }


    FORCE_INLINE void log_msg(const severity msg_sev, const std::source_location location, const char* module_name,
        std::thread::id thread_id, std::string_view message) {
            
        if (message.empty())
            return;

        log_msg_internal(msg_sev, location, module_name, thread_id, std::string(message));
    }

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}

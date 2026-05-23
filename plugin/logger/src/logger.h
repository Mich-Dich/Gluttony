#pragma once

// #include "util/pch.h"
#include "util/core_config.h"
#include <util/io/logger.h>


// FORWARD DECLARATIONS ================================================================================================


namespace GLT::logger_plugin {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Initialize the logging system
    // @param format The initial log message format
    // @param log_to_console should the log message be written to std::cout?
    // @param log_dir the directory that will contain all log files
    // @ main_log_file_name name of the central log_file (the thread that runs logger::init())
    // @param use_append_mode Should the system write over the existing log file or append to it
    bool init(const std::string& format, const bool log_to_console = false, const std::filesystem::path& log_dir = "./logs",
        const std::string& main_log_file_name = "general.log", const bool use_append_mode = false);


    // Shuts down the logging subsystem: stops the worker thread, drains and processes
    // any remaining queued log messages, flushes buffered messages to the main log file,
    // and marks the logger as uninitialized.
    // If the logger was not initialized, an error is printed and the program exits immediately.
    // @return None.
    void shutdown();

    // Returns the filesystem path to the main log file used by the logger.
    // @return A std::filesystem::path pointing to the current main log file.
    std::filesystem::path get_log_file_location();


    // The format of log-messages can be customized with the following tags
    // @note to format all following log-messages use: set_format()
    // @note e.g. set_format("$B[$T] $L [$F] $C$E")
    //
    // @param $T time                    hh:mm:ss
    // @param $H hour                    hh
    // @param $M minute                  mm
    // @param $S secund                  ss
    // @param $J milliseconds            jjj
    //
    // @param $N data                    yyyy:mm:dd
    // @param $Y data year               yyyy
    // @param $O data month              mm
    // @param $D data day                dd
    //
    // @param $Q thread                  Thread_id: 137575225550656 or a label if provided
    // @param $F function name           application::main, math::foo
    // @param $R module name             core, logger_plugin
    // @param $P only function name      main, foo
    // @param $A file name               /home/workspace/test_cpp/src/main.cpp  /home/workspace/test_cpp/src/project.cpp
    // @param $I only file name          main.cpp
    // @param $G line                    1, 42
    //
    // @param $L log-level               add used log severity: [TRACE], [DEBUG] ... [FATAL]
    // @param $X alignment               adds space for "INFO" & "WARN"
    // @param $B color begin             from here the color begins
    // @param $E color end               from here the color will be reset
    // @param $C text                    the message the user wants to print
    // @param $Z new line                add a new line in the message format
    void set_format(const std::string& new_format);


    // Restore the previous log-message format
    // @note This function swaps the current log-message format with the previously stored backup.
    // It's useful for reverting to the previous format after temporary changes
    void use_previous_format();


    // Returns the current log output format string.
    // @return A copy of the format string used for log messages.
    const std::string get_format();


    // all messages with a lower severity than the provided argument will be buffered
    // Trace => buffer[]
    // Debug => buffer[Trace]
    // Info  => buffer[Trace + Debug]
    // Warn  => buffer[Trace + Debug + Info]
    // Error => buffer[Trace + Debug + Info + Warn]     (Error and Fatal will never be buffered)
    // Fatal => buffer[Trace + Debug + Info + Warn]     (Error and Fatal will never be buffered)
    void set_buffer_threshold(const GLT::logger::severity new_threshold);


    // set the size of the buffer.
    // @note for messages that are not directly logged
    void set_buffer_size(const size_t new_size);


    void flush_buffer();
    

    // Registers a label for a specific thread, allowing for easier identification in logs.
    // If a label is already registered for the given thread ID, it will be overridden with the new label.
    // @param thread_label The label to be associated with the thread.
    // @param thread_id The ID of the thread for which the label is being registered.
    //                  Defaults to the ID of the calling thread if not provided.
    void register_label_for_thread(const std::string& thread_label, std::thread::id thread_id = std::this_thread::get_id());


    // Unregisters the label for a specific thread, removing its association from the logger.
    // If no label is registered for the given thread ID, a message will be logged indicating that the operation was ignored.
    // @param thread_id The ID of the thread for which the label is being unregistered.
    //                  Defaults to the ID of the calling thread if not provided.
    void unregister_label_for_thread(std::thread::id thread_id = std::this_thread::get_id());


    // THIS SHOULD NEVER BE DIRECTLY CALLED
    // @note empty log messages will be ignored
    void log_msg_internal(const GLT::logger::severity msg_sev, const char* file_name, const char* function_name, const int line, 
        const char* module_name, std::thread::id thread_id, std::string message);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

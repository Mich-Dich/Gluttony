
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <iomanip>

#include <util/system.h>
#include <util/io/vfs.h>

#include "logger.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::logger_plugin {

    // CONSTANTS =======================================================================================================

    const std::string                                           severity_names[] = {
        "TRACE",
        "DEBUG",
        "INFO",
        "WARN",
        "ERROR",
        "FATAL"
    };
    const std::string                                           console_color_table[] = {
        "\x1b[38;5;246m",                                           // trace: Gray
        "\x1b[94m",                                                 // debug: Blue
        "\x1b[92m",                                                 // info: Green
        "\x1b[33m",                                                 // warn: Yellow
        "\x1b[31m",                                                 // error: Red
        "\x1b[41m\x1b[30m",                                         // fatal: Red Background
    };
    const std::string                                           console_rest = "\x1b[0m";

    // MACROS ==========================================================================================================

    #define SETW(width)                                         std::setw(width) << std::setfill('0')

    #define CMD_UPDATE_FORMAT                                   1       // "LOGGER update format"
    #define CMD_REVERSE_FORMAT                                  2       // "LOGGER reverse format"
    #define CMD_CHANGE_THRESHOLD                                3       // "LOGGER change threshold"
    #define CMD_CHANGE_BUFFER_SIZE                              4       // "LOGGER change buffer size"
    #define CMD_REGISTER_THREAD_LABEL                           5       // "LOGGER register thread label"
    #define CMD_UNREGISTER_THREAD_LABEL                         6       // "LOGGER unregister thread label"

    #if defined(DEBUG)
        #define QUEUE_MAX_SIZE                                  0       // TODO: flush messages directly in debug (set to 0)
    #else
        #define QUEUE_MAX_SIZE                                  16000
    #endif

    #define OPEN_FILE                                                                                                   \
        s_main_file = std::ofstream(s_main_log_file_path, std::ios::app);                                               \
        if (!s_main_file.is_open()) {                                                                                   \
            std::cerr << "Failed to open main log file path: ["                                                         \
                << s_main_log_file_path.string() << "]" << std::endl;                                                   \
            std::quick_exit(1);                                                                                         \
        }

    #define CLOSE_FILE                                          if (s_main_file.is_open()) { s_main_file.close(); }
    #define WRITE_TO_FILE(message)                              { OPEN_FILE s_main_file << message; CLOSE_FILE }

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static bool                                                 s_is_init = false;
    
    static bool                                                 s_write_log_to_console = false;
    
    static std::string                                          s_format_current = "";
    
    static std::string                                          s_format_prev = "";
    
    static GLT::logger::severity                                s_severity_level_buffering_threshold = GLT::logger::severity::trace;
    
    static size_t                                               s_buffer_size = 1024;
    
    static std::string                                          s_buffered_messages{};
    
    static std::filesystem::path                                s_main_log_dir = "";
    
    static std::filesystem::path                                s_main_log_file_path = "";
    
    static std::ofstream                                        s_main_file{};
    
    static std::queue<GLT::logger::message_data>                s_log_queue{};
    
    static std::unordered_map<std::thread::id, std::string>     s_thread_labels{};
    
    static std::mutex                                           s_queue_mutex{};
    
    static std::mutex                                           s_general_mutex{};
    
    static std::condition_variable                              s_cv{};
    
    static std::atomic<bool>                                    s_stop = false;
    
    static std::thread                                          s_worker_thread{};

    // INTERNAL FUNCTION ===============================================================================================

    inline const char* get_filename(const char* filepath) {

        const char* filename = std::strrchr(filepath, '\\');
        if (filename == nullptr)
            filename = std::strrchr(filepath, '/');

        if (filename == nullptr)
            return filepath;  // No path separator found, return the whole string

        return filename + 1;  // Skip the path separator
    }

    // Helper function to convert thread::id to string
    inline std::string thread_id_to_string(std::thread::id id) {
        std::ostringstream oss;
        oss << id;
        return oss.str();
    }

    // Helper function to format tm structure
    inline std::string format_time(const std::tm& tm, const char* format) {
        std::ostringstream oss;
        oss << std::put_time(&tm, format);
        return oss.str();
    }

    // FUNCTION DECLARATION ============================================================================================

    void process_log_message(const GLT::logger::message_data&& message);

    void process_queue();

    // FUNCTION IMPLEMENTATION =========================================================================================

    bool init(const std::string& format, const bool log_to_console, const std::filesystem::path& log_dir, 
        const std::string& main_log_file_name, const bool use_append_mode) {

        if (s_is_init) {
            std::cerr << "Tried to init logger system multiple times" << std::endl;
            return false;
        }

        s_format_current = format;
        s_format_prev = format;
        s_write_log_to_console = log_to_console;
        s_main_log_dir = log_dir;
        s_main_log_file_path = s_main_log_dir / main_log_file_name;

        std::error_code error{};
        GLT::vfs::create_directories(s_main_log_file_path.parent_path(), error);
        if (error) {
            std::cerr << "Failed to create the directory for log files" << std::endl;
            return false;
        }

        s_main_file = std::ofstream(s_main_log_file_path, (use_append_mode) ? std::ios::app : std::ios::out);
        if (!s_main_file.is_open()) {
            std::cerr << "Failed to open main log file path: [" << s_main_log_file_path.string() << "]" << std::endl;
            return false;
        }
        s_main_file << "\n================================================================================================\n";
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        s_main_file << "Log initialized at [" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "]\n";
        s_main_file << "------------------------------------------------------------------------------------------------\n";
        CLOSE_FILE

        s_buffered_messages.reserve(s_buffer_size);
        s_is_init = true;
        s_worker_thread = std::thread(&process_queue);                                                        // start after inital write to avoid using mutex

        // take over messages from before logger attachment
        std::vector<GLT::logger::message_data> previous_messages = GLT::logger::drain_log_buffer(true);
        for (const auto& msg : previous_messages)
            GLT::logger_plugin::log_msg_internal(msg.msg_sev, msg.location, msg.module_name, msg.thread_id, msg.message);

        return true;
    }


    void shutdown() {

        if (!s_is_init) {
            std::cerr << "Tried to shutdown logger before initialization" << std::endl;
            std::quick_exit(1);
        }

        s_stop = true;
        s_cv.notify_all();
        if (s_worker_thread.joinable())
            s_worker_thread.join();

        // Process any remaining messages in the queue after worker thread has stopped
        std::queue<GLT::logger::message_data> remaining_messages;
        {
            std::lock_guard<std::mutex> lock(s_queue_mutex);
            remaining_messages = std::move(s_log_queue); // Take all remaining messages
        }

        while (!remaining_messages.empty()) {
            GLT::logger::message_data msg = std::move(remaining_messages.front());
            remaining_messages.pop();
            process_log_message(std::move(msg)); // Process each message
        }

        OPEN_FILE
        s_main_file << s_buffered_messages;
        auto now = std::time(nullptr);
        auto tm = *std::localtime(&now);
        s_main_file << "------------------------------------------------------------------------------------------------\n";
        s_main_file << "Log shutdown at [" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "]\n";
        s_main_file << "================================================================================================\n";
        CLOSE_FILE

        s_is_init = false;
    }

    // settings --------------------------------------------------------------------------------------------------------

    std::filesystem::path get_log_file_location() { 
    
        if (!s_is_init) return {};
        return s_main_log_file_path; 
    }


    void set_format(const std::string& new_format) {        // needed to insert this into the queue to preserve the order

        if (!s_is_init) {
            std::cerr << "Tried to set logger format befor logger was initialized" << std::endl;
            return;
        }

        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_log_queue.emplace(GLT::logger::severity::trace, std::source_location::current(), GLT_MODULE_NAME, std::thread::id(), new_format, CMD_UPDATE_FORMAT);
        s_cv.notify_all();
    }


    void use_previous_format() {

        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_log_queue.emplace(GLT::logger::severity::trace, std::source_location::current(), GLT_MODULE_NAME, std::thread::id(), "", CMD_REVERSE_FORMAT);
        s_cv.notify_all();
    }


    const std::string get_format() { return s_format_current; }


    void register_label_for_thread(const std::string& thread_label, std::thread::id thread_id) {

        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_log_queue.emplace(GLT::logger::severity::trace, std::source_location::current(), GLT_MODULE_NAME, thread_id, thread_label, CMD_REGISTER_THREAD_LABEL);
        s_cv.notify_all();
    }


    void unregister_label_for_thread(std::thread::id thread_id) {

        std::string loc_msg;
        {
            std::lock_guard<std::mutex> lock(s_general_mutex);
            if (s_thread_labels.find(thread_id) == s_thread_labels.end())
                loc_msg = std::format("[LOGGER] Tried to unregister label for unknown thread with ID: [{}]. IGNORED",
                                      thread_id_to_string(thread_id));
        }

        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_log_queue.emplace(GLT::logger::severity::trace, std::source_location::current(), GLT_MODULE_NAME, thread_id, std::move(loc_msg), CMD_UNREGISTER_THREAD_LABEL);
        s_cv.notify_all();
    }


    void set_buffer_threshold(const GLT::logger::severity new_threshold) {

        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_log_queue.emplace(new_threshold, std::source_location::current(), GLT_MODULE_NAME, std::thread::id(), 
            std::format("[LOGGER] Changed buffering threshold to [{}]", severity_names[static_cast<u8>(new_threshold)]), 
            CMD_CHANGE_THRESHOLD);
        s_cv.notify_all();
    }


    void set_buffer_size(const size_t new_size) {

        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_log_queue.emplace(GLT::logger::severity::trace, /*static_cast<int>(new_size)*/ std::source_location::current(), GLT_MODULE_NAME, 
            std::thread::id(), std::format("[LOGGER] Changed buffer size to [{}]", new_size), CMD_CHANGE_BUFFER_SIZE);
        s_cv.notify_all();
    }


    void flush_buffer() {

        if (!s_is_init) {
            std::cerr << "Tried to flush buffer befor logger was initialized" << std::endl;
            return;
        }

        std::unique_lock<std::mutex> lock(s_general_mutex);

        OPEN_FILE
        s_main_file << s_buffered_messages;
        CLOSE_FILE

        s_buffered_messages.clear();
    }

    // message queue ---------------------------------------------------------------------------------------------------

    void process_queue() {

        std::unique_lock<std::mutex> lock(s_queue_mutex);
        while (!s_stop) {

            s_cv.wait_for(lock, std::chrono::milliseconds(100), [] { return !s_log_queue.empty() || s_stop; });

            if (s_stop) break;


            std::queue<GLT::logger::message_data> local_queue;
            while (!s_log_queue.empty()) {                                    // Move all current messages to a local queue
                local_queue.push(std::move(s_log_queue.front()));
                s_log_queue.pop();
            }
            lock.unlock();                                                  // Unlock while processing messages

            // Process each message from the local queue
            while (!local_queue.empty()) {
                GLT::logger::message_data message = std::move(local_queue.front());
                local_queue.pop();
                // Process control messages and log messages

                switch (message.command) {
                    case CMD_UPDATE_FORMAT: {

                        std::lock_guard<std::mutex> lock(s_general_mutex);
                        s_format_prev = s_format_current;
                        s_format_current = message.message;

                        WRITE_TO_FILE("[LOGGER] Changing log-format. From [" << s_format_prev << "] to [" << s_format_current << "]\n");
                        break;
                    }

                    case CMD_REVERSE_FORMAT: {

                        std::lock_guard<std::mutex> lock(s_general_mutex);
                        const std::string buffer = s_format_current;
                        s_format_current = s_format_prev;
                        s_format_prev = buffer;
                        break;
                    }

                    case CMD_CHANGE_THRESHOLD: {

                        std::lock_guard<std::mutex> lock(s_general_mutex);
                        s_severity_level_buffering_threshold = static_cast<GLT::logger::severity>(
                            std::min(static_cast<u8>(message.msg_sev), static_cast<u8>(GLT::logger::severity::error))
                        );
                        break;
                    }

                    case CMD_CHANGE_BUFFER_SIZE: {

                        std::lock_guard<std::mutex> lock(s_general_mutex);
                        s_buffer_size = static_cast<size_t>(message.location.line());

                        OPEN_FILE
                        s_main_file << message.message;
                        if (s_is_init && s_buffered_messages.size() >= s_buffer_size) {                   // Handle buffer overflow if the new size is smaller than the current buffer content

                            s_main_file << s_buffered_messages;
                            // if (s_write_log_to_console)
                            // std::cout << s_buffered_messages;

                            s_buffered_messages.clear();
                        }
                        CLOSE_FILE

                        s_buffered_messages.shrink_to_fit();
                        s_buffered_messages.reserve(s_buffer_size);
                        break;
                    }

                    case CMD_REGISTER_THREAD_LABEL: {

                        std::lock_guard<std::mutex> lock(s_general_mutex);

                        if (s_thread_labels.find(message.thread_id) != s_thread_labels.end())
                            WRITE_TO_FILE("[LOGGER] Thread with ID: [" << thread_id_to_string(message.thread_id) << "] already has label [" << s_thread_labels[message.thread_id] << "] registered. Overriding with the label: [" << message.message << "]\n")
                        else
                            WRITE_TO_FILE("[LOGGER] Registering Thread-ID: [" << thread_id_to_string(message.thread_id) << "] with the label: [" << message.message << "]\n")

                        s_thread_labels[message.thread_id] = message.message;
                        break;
                    }

                    case CMD_UNREGISTER_THREAD_LABEL: {

                        std::lock_guard<std::mutex> lock(s_general_mutex);
                        s_thread_labels.erase(message.thread_id);
                        break;
                    }

                    default:    process_log_message(std::move(message)); break;
                }
            }

            // Re-lock before next iteration
            lock.lock();
        }
    }

    // handle message --------------------------------------------------------------------------------------------------

    void log_msg_internal(const GLT::logger::severity msg_sev, const std::source_location location, const char* module_name, 
        std::thread::id thread_id, std::string message) {

        if (message.empty())
            return;

        std::lock_guard<std::mutex> lock(s_queue_mutex);
        s_log_queue.emplace(msg_sev, location, module_name, thread_id, std::move(message));

        if (static_cast<u8>(msg_sev) >= static_cast<u8>(s_severity_level_buffering_threshold) 
            || s_log_queue.size() >= static_cast<size_t>(QUEUE_MAX_SIZE))           // check if thread should be notified
            s_cv.notify_all();
    }


    void process_log_message(const GLT::logger::message_data&& message) {

    #define SHORTEN_FUNC_NAME(text)                                 (strstr(text, "::") ? strstr(text, "::") + 2 : text)

        // create helper vars
        std::string formatted_message;
        formatted_message.reserve(256); // Pre-allocate to avoid reallocations
        char format_command{};
        GLT::system_time loc_sys_time = GLT::util::get_system_time();

        // loop over format string and build final message
        std::unique_lock<std::mutex> lock(s_general_mutex);
        size_t format_length = s_format_current.length();
        for (size_t x = 0; x < format_length; x++) {

            if (s_format_current[x] == '$' && x+1 < format_length) {          // detected a format specifier prefix

                format_command = s_format_current[x + 1];
                switch (format_command) {

                // ------------------------ Basic info ------------------------
                case 'B': formatted_message.append(console_color_table[static_cast<u8>(message.msg_sev)]); break;                   // Color start
                case 'E': formatted_message.append(console_rest); break;                                                            // Color end
                case 'C': formatted_message.append(message.message); break;                                                         // input text (message)
                case 'L': formatted_message.append(severity_names[static_cast<u8>(message.msg_sev)]); break;                        // log severity
                case 'X': if(message.msg_sev == GLT::logger::severity::info || message.msg_sev == GLT::logger::severity::warn) { formatted_message.append(" "); } break; // alignment
                case 'Z': formatted_message.append("\n"); break;                                                                    // line break

                // ------------------------ Basic info ------------------------
                case 'Q':   if (s_thread_labels.find(message.thread_id) != s_thread_labels.end()) {
                                formatted_message.append(s_thread_labels[message.thread_id]);
                            } else {
                                formatted_message.append(thread_id_to_string(message.thread_id));
                            } break;                                                                                                // Thread id or associated label
                case 'F': formatted_message.append(message.location.function_name()); break;                                                   // function name
                case 'R': formatted_message.append(message.module_name); break;                                                     // function name
                case 'P': formatted_message.append(SHORTEN_FUNC_NAME(message.location.function_name())); break;                                // short function name
                case 'A': formatted_message.append(message.location.file_name()); break;                                                       // file name
                case 'I': formatted_message.append(get_filename(message.location.file_name())); break;                                         // short file name
                case 'G': formatted_message.append(std::to_string(message.location.line())); break;                                            // line

                // ------------------------ time ------------------------
                case 'T': formatted_message.append(std::format("{:02}:{:02}:{:02}",
                                                              static_cast<u16>(loc_sys_time.hour),
                                                              static_cast<u16>(loc_sys_time.minute),
                                                              static_cast<u16>(loc_sys_time.secund))); break;                        // formatted time
                case 'H': formatted_message.append(std::format("{:02}", static_cast<u16>(loc_sys_time.hour))); break;                // hour
                case 'M': formatted_message.append(std::format("{:02}", static_cast<u16>(loc_sys_time.minute))); break;              // minute
                case 'S': formatted_message.append(std::format("{:02}", static_cast<u16>(loc_sys_time.secund))); break;              // second
                case 'J': formatted_message.append(std::format("{:03}", static_cast<u16>(loc_sys_time.millisecond))); break;         // milliseconds

                // ------------------------ data ------------------------
                case 'N': formatted_message.append(std::format("{:04}/{:02}/{:02}",
                                                              static_cast<u16>(loc_sys_time.year),
                                                              static_cast<u16>(loc_sys_time.month),
                                                              static_cast<u16>(loc_sys_time.day))); break;                           // date yy/mm/dd
                case 'Y': formatted_message.append(std::format("{:04}", static_cast<u16>(loc_sys_time.year))); break;                // year
                case 'O': formatted_message.append(std::format("{:02}", static_cast<u16>(loc_sys_time.month))); break;               // month
                case 'D': formatted_message.append(std::format("{:02}", static_cast<u16>(loc_sys_time.day))); break;                 // day

                default: break;
                }

                x++;
            }

            else
                formatted_message.push_back(s_format_current[x]);
        }

        if (s_write_log_to_console) {                              // write to console before checking for file write conditions
            if (message.msg_sev >= GLT::logger::severity::error) {
                std::cerr << formatted_message;
                std::cerr.flush();
            } else {
                std::cout << formatted_message;
                std::cout.flush();
            }
        }

        if (!((static_cast<u8>(message.msg_sev) >= static_cast<u8>(s_severity_level_buffering_threshold)) ||
              (s_buffered_messages.capacity() - s_buffered_messages.size()) <= formatted_message.size())) {

            s_buffered_messages.append(formatted_message);
            return;
        }

        OPEN_FILE
        s_main_file << s_buffered_messages << formatted_message;
        CLOSE_FILE

        s_buffered_messages.clear();
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

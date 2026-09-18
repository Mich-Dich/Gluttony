
#pragma once

#include "util/data_structures/data_types.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================


    // @brief Executes an external program with the specified command-line arguments.
    //          This function supports both Windows and Linux platforms, handling process creation
    //          and execution differently based on the operating system. On Windows, it uses
    //          `CreateProcessA`, while on Linux, it uses `fork` and `execv`.
    // @param [path_to_exe] The path to the executable file to be run.
    // @param [cmd_args] The command-line arguments to pass to the executable.
    // @param [open_console] If true, opens a new console window for the program (Windows) or
    //          uses a terminal emulator like `xterm` (Linux).
    // @return Returns true if the program was successfully executed, false otherwise.
    bool run_program(const std::filesystem::path& path_to_exe, const std::string& cmd_args = "", bool open_console = false, const bool display_output_on_success = false, const bool display_output_on_failure = true, std::string* output = nullptr);


    // @brief Overload of `run_program` that accepts a C-style string for command-line arguments.
    // @param [path_to_exe] The path to the executable file to be run.
    // @param [cmd_args] The command-line arguments as a C-style string.
    // @param [open_console] If true, opens a new console window for the program (Windows) or
    //          uses a terminal emulator like `xterm` (Linux).
    // @return Returns true if the program was successfully executed, false otherwise.
    bool run_program(const std::filesystem::path& path_to_exe, const char* cmd_args = "", bool open_console = false, const bool display_output_on_success = false, const bool display_output_on_failure = true, std::string* output = nullptr);


    // @brief Pauses the execution of the current thread for a specified duration with high precision.
    void precision_sleep_until(const std::chrono::high_resolution_clock::time_point target_time);


    // @brief Retrieves the current system time, including year, month, day, hour, minute, second, and millisecond.
    //          This function is platform-specific, using `GetLocalTime` on Windows and `gettimeofday` on Linux.
    // @return Returns a `system_time` struct containing the current system time.
    GLT::system_time get_system_time();


    // @brief Retrieves the directory containing the currently running executable.
    //          This function is platform-specific, using `GetModuleFileNameW` on Windows and
    //          `/proc/self/exe` on Linux to determine the executable path.
    // @return Returns the path to the directory containing the executable, or an empty path on error.
	std::filesystem::path get_executable_path();


    // Returns the current resident set size (RSS) of this process in bytes.
    // This is the amount of physical RAM the process is actually using right
    // now, not its virtual address space.
    //
    // Linux only. Returns 0 if the value could not be read.
    [[nodiscard]] u64 get_process_ram_bytes();


    // Returns the peak RSS this process has reached since it started, in bytes.
    // Useful for a "max RAM" indicator alongside the live value.
    [[nodiscard]] u64 get_process_peak_ram_bytes();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

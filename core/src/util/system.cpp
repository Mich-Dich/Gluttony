
#include "util/pch.h"

#if defined(PLATFORM_LINUX)
    #include <sys/types.h>          // For pid_t
    #include <sys/wait.h>           // For waitpid
    #include <unistd.h>             // For fork, execv, etc.
    #include <sys/time.h>
    #include <ctime>
    #include <limits.h>
    #include <fcntl.h>
    #include <cstring>
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <fcntl.h>
#elif defined(PLATFORM_WINDOWS)
    #include <Windows.h>
    #include <commdlg.h>
    #include <iostream>
    #include <tchar.h>              // For _T() macros
#endif

#include "system.h"


// FORWARD DECLARATIONS ================================================================================================


namespace GLT::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    //
    bool run_program(const std::filesystem::path& path_to_exe, const std::string& cmd_args, bool open_console, const bool display_output_on_success,
        const bool display_output_on_failure, std::string* output) {

        return run_program(path_to_exe, cmd_args.c_str(), open_console, display_output_on_success, display_output_on_failure, output);
    }

    //
    bool run_program(const std::filesystem::path& path_to_exe, const char* cmd_args, bool open_console, const bool display_output_on_success, const bool display_output_on_failure, std::string* output) {

        //LOG(trace, "executing program at [{}]", path_to_exe.generic_string());

        #if defined(PLATFORM_WINDOWS)

            STARTUPINFOA startupInfo;
            PROCESS_INFORMATION processInfo;

            ZeroMemory(&startupInfo, sizeof(startupInfo));
            startupInfo.cb = sizeof(startupInfo);
            ZeroMemory(&processInfo, sizeof(processInfo));

            std::string cmdArguments = path_to_exe.generic_string() + " " + cmd_args;
            auto working_dir = util::get_executable_path().generic_string();

            // Start the program
            bool result = CreateProcessA(
                NULL,							            // Application Name
                (LPSTR)cmdArguments.c_str(),	            // Command Line Args
                NULL,							            // Process Attributes
                NULL,							            // Thread Attributes
                FALSE,							            // Inherit Handles
                (open_console) ? CREATE_NEW_CONSOLE : 0,	// Creation Flags
                NULL,							            // Environment
                working_dir.c_str(),			            // Current Directory
                &startupInfo,					            // Startup Info
                &processInfo					            // Process Info
            );

            WaitForSingleObject(processInfo.hProcess, INFINITE);                                        // Wait for the process to finish

            if (result) {                                                                               // Close process and thread handles

                CloseHandle(processInfo.hProcess);
                CloseHandle(processInfo.hThread);
            } else
                LOG(error, "Unsuccessfully started process: ", path_to_exe.generic_string());

            return true;

        #elif defined(PLATFORM_LINUX)

            // Build command arguments
            std::string full_command = path_to_exe.generic_string() + " " + cmd_args;
            std::istringstream iss(full_command);
            std::vector<std::string> args;
            std::string arg;
            while (iss >> arg) {
                args.push_back(arg);
            }

            // Prepare exec arguments
            std::vector<char*> execArgs;
            for (auto& a : args) {
                execArgs.push_back(const_cast<char*>(a.c_str()));
            }
            execArgs.push_back(nullptr);

            // Create pipe
            int pipefd[2];
            if (pipe(pipefd) == -1) {
                std::cerr << "Failed to create pipe." << std::endl;
                return false;
            }

            pid_t pid = fork();
            if (pid == -1) {
                std::cerr << "Failed to fork process." << std::endl;
                close(pipefd[0]);
                close(pipefd[1]);
                return false;
            }

            if (pid == 0) {  // Child process
                close(pipefd[0]);
                dup2(pipefd[1], STDOUT_FILENO);
                dup2(pipefd[1], STDERR_FILENO);
                close(pipefd[1]);

                if (open_console) {
                    // Build stable arguments for xterm
                    std::vector<char*> xtermArgs = {
                        const_cast<char*>("xterm"),
                        const_cast<char*>("-e"),
                        const_cast<char*>(path_to_exe.c_str()),
                        nullptr
                    };
                    execvp("xterm", xtermArgs.data());
                } else {
                    execvp(path_to_exe.c_str(), execArgs.data());
                }

                // If we get here, exec failed
                std::cerr << "Failed to execute program: " << path_to_exe << std::endl;
                _exit(EXIT_FAILURE);
            }
            else {  // Parent process
                close(pipefd[1]);

                char buffer[1024];
                ssize_t count;
                while ((count = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
                    if (output) {
                        buffer[count] = '\0';
                        output->append(buffer);
                    }
                }
                close(pipefd[0]);

                int status;
                waitpid(pid, &status, 0);

                const bool exited = WIFEXITED(status);
                const bool exit_status = (WEXITSTATUS(status) == 0);
                return exited && !exit_status;
            }

        #endif

    }


    void precision_sleep_until(const std::chrono::high_resolution_clock::time_point target_time) {

        // Sleep until 2ms before target
        auto sleep_until_time = target_time - std::chrono::microseconds(1500);
        std::this_thread::sleep_until(sleep_until_time);

        while (std::chrono::high_resolution_clock::now() < target_time) {       // busy-wait for the remaining time with minimal overhead

            COMPILER_BARRIER
        }
    }


    system_time get_system_time() {

        system_time loc_system_time{};

        #if defined(PLATFORM_WINDOWS)

            SYSTEMTIME win_time;
            GetLocalTime(&win_time);
            loc_system_time.year = static_cast<u16>(win_time.wYear);
            loc_system_time.month = static_cast<u8>(win_time.wMonth);
            loc_system_time.day = static_cast<u8>(win_time.wDay);
            loc_system_time.day_of_week = static_cast<u8>(win_time.wDayOfWeek);
            loc_system_time.hour = static_cast<u8>(win_time.wHour);
            loc_system_time.minute = static_cast<u8>(win_time.wMinute);
            loc_system_time.secund = static_cast<u8>(win_time.wSecond);
            loc_system_time.millisecond = static_cast<u16>(win_time.wMilliseconds);

        #elif defined(PLATFORM_LINUX)

            struct timeval tv;
            gettimeofday(&tv, NULL);
            struct tm* ptm = localtime(&tv.tv_sec);
            loc_system_time.year = static_cast<u16>(ptm->tm_year + 1900);
            loc_system_time.month = static_cast<u8>(ptm->tm_mon + 1);
            loc_system_time.day = static_cast<u8>(ptm->tm_mday);
            loc_system_time.day_of_week = static_cast<u8>(ptm->tm_wday);
            loc_system_time.hour = static_cast<u8>(ptm->tm_hour);
            loc_system_time.minute = static_cast<u8>(ptm->tm_min);
            loc_system_time.secund = static_cast<u8>(ptm->tm_sec);
            loc_system_time.millisecond = static_cast<u16>(tv.tv_usec / 1000);

        #endif
        return loc_system_time;
    }


    std::filesystem::path get_executable_path() {

        #if defined(PLATFORM_WINDOWS)

            wchar_t path[MAX_PATH];
            if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
                std::filesystem::path execPath(path);
                return execPath.parent_path();
            }

        #elif defined(PLATFORM_LINUX)

            char path[PATH_MAX];
            ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
            if (count != -1) {
                path[count] = '\0'; // Null-terminate the string
                std::filesystem::path execPath(path);
                return execPath.parent_path();
            }

        #endif

        std::cerr << "Error retrieving the executable path." << std::endl;
        return std::filesystem::path();
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

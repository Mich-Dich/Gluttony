
#pragma once

#include <filesystem>
#include "util/macros.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::vfs {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class file_open_mode : u8 {

        read        = BIT(0),
        write       = BIT(1),
        append      = BIT(2),
        truncate    = BIT(3),
        create      = BIT(4)
    };


    enum class filesystem_type : u8 {

        native = 0,
        memory,
        zip,
    };


    inline file_open_mode operator|(file_open_mode a, file_open_mode b) {

        return static_cast<file_open_mode>(static_cast<u32>(a) | static_cast<u32>(b));
    }


    inline bool operator&(file_open_mode a, file_open_mode b) {

        return (static_cast<u32>(a) & static_cast<u32>(b)) != 0;
    }


    using exists_func = bool (*)(const std::filesystem::path& path, std::error_code& error);

    using create_file_func = void (*)(const std::filesystem::path& path, std::error_code& error);

    using is_directory_func = bool (*)(const std::filesystem::path& path, std::error_code& error);

    using is_regular_file_func = bool (*)(const std::filesystem::path& path, std::error_code& error);

    using create_directory_func = void (*)(const std::filesystem::path& path, std::error_code& error);

    using create_directories_func = void (*)(const std::filesystem::path& path, std::error_code& error);

    using remove_func = void (*)(const std::filesystem::path& path, std::error_code& error);

    using remove_all_func = void (*)(const std::filesystem::path& path, std::error_code& error);

    using rename_func = void (*)(const std::filesystem::path& old_path, const std::filesystem::path& new_path, std::error_code& error);

    using copy_file_func = void (*)(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error, bool overwrite);

    using file_size_func = u64 (*)(const std::filesystem::path& path, std::error_code& error);

    using last_write_time_func = GLT::system_time (*)(const std::filesystem::path& path, std::error_code& error);

    using read_text_file_func = std::string (*)(const std::filesystem::path& path, std::error_code& error);

    using write_text_file_func = bool (*)(const std::filesystem::path& path, const std::string& content);

    using open_file_func = handle (*)(const std::filesystem::path& path, GLT::vfs::file_open_mode mode, std::error_code& error) noexcept;

    using read_file_func = size_t (*)(handle handle, void* buffer, size_t size, size_t offset);

    using write_file_func = size_t (*)(handle handle, const void* data, size_t size, size_t offset);

    using seek_file_func = bool (*)(handle handle, i64 offset, int origin);

    using tell_file_func = u64 (*)(handle handle);

    using close_file_func = void (*)(handle handle);


    struct vfs_functions {

        exists_func                                     exists;
        create_file_func                                create_file;
        is_directory_func                               is_directory;
        is_regular_file_func                            is_regular_file;
        create_directory_func                           create_directory;
        create_directories_func                         create_directories;
        remove_func                                     remove;
        remove_all_func                                 remove_all;
        rename_func                                     rename;
        copy_file_func                                  copy_file;
        file_size_func                                  file_size;
        last_write_time_func                            last_write_time;
        read_text_file_func                             read_text_file;
        write_text_file_func                            write_text_file;
        open_file_func                                  open_file;
        read_file_func                                  read_file;
        write_file_func                                 write_file;
        seek_file_func                                  seek_file;
        tell_file_func                                  tell_file;
        close_file_func                                 close_file;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    void install_vfs_functions(const vfs_functions& funcs);


    [[nodiscard]] filesystem_type get_filesystem_type();


    void set_filesystem_type(const filesystem_type type);

    // ------ Standard file system operations (path‑based) ---------------------------------------------------------


    // Checks whether a file or directory exists at the given path.
    [[nodiscard]] bool exists(const std::filesystem::path& path, std::error_code& error);


    void create_file(const std::filesystem::path& path, std::error_code& error);


    // Checks whether the given path points to a directory.
    [[nodiscard]] bool is_directory(const std::filesystem::path& path, std::error_code& error);


    // Checks whether the given path points to a regular file.
    [[nodiscard]] bool is_regular_file(const std::filesystem::path& path, std::error_code& error);


    // Creates a directory. Returns true on success.
    void create_directory(const std::filesystem::path& path, std::error_code& error);


    // Creates a directory. Returns true on success.
    void create_directories(const std::filesystem::path& path, std::error_code& error);


    // Removes a file or an empty directory. Returns true on success.
    void remove(const std::filesystem::path& path, std::error_code& error);


    void remove_all(const std::filesystem::path& path, std::error_code& error);


    // Renames (moves) a file or directory. Returns true on success.
    void rename(const std::filesystem::path& old_path, const std::filesystem::path& new_path, std::error_code& error);


    // Copies a file. Overwrites destination only if overwrite == true.
    void copy_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error, bool overwrite = false);


    // Returns the size in bytes of a regular file. Returns 0 if file does not exist.
    [[nodiscard]] u64 file_size(const std::filesystem::path& path, std::error_code& error);


    // Returns the last write time of a file. Zero-initialized system_time on failure.
    [[nodiscard]] system_time last_write_time(const std::filesystem::path& path, std::error_code& error);

    // ------ Convenience text file operations ---------------------------------------------------------------------

    // Reads the entire content of a text file as a string. Returns empty string on failure.
    [[nodiscard]] std::string read_text_file(const std::filesystem::path& path, std::error_code& error);


    // Writes a string to a text file (overwrites). Returns true on success.
    bool write_text_file(const std::filesystem::path& path, const std::string& content);

    // ------ Handle‑based binary file I/O -------------------------------------------------------------------------

    // Opens a file with the given mode flags. Returns a non‑zero handle or INVALID_HANDLE on error.
    [[nodiscard]] handle open_file(const std::filesystem::path& path, GLT::vfs::file_open_mode mode, std::error_code& error) noexcept;


    // Reads up to 'size' bytes into 'buffer' from the open file.
    //      If offset == static_cast<size_t>(-1), reads from the current file position.
    //      Returns the number of bytes actually read.
    size_t read_file(handle handle, void* buffer, size_t size, size_t offset = static_cast<size_t>(-1));


    // Writes 'size' bytes from 'data' into the open file.
    //      If offset == static_cast<size_t>(-1), writes at the current file position.
    //      Returns the number of bytes actually written.
    size_t write_file(handle handle, const void* data, size_t size, size_t offset = static_cast<size_t>(-1));


    // Repositions the file pointer.
    //      origin: 0 = seek_set (absolute offset), 1 = seek_cur (relative), 2 = seek_end (relative, offset <= 0).
    //      Returns true on success.
    bool seek_file(handle handle, i64 offset, int origin);


    // Returns the current position of the file pointer. Returns 0 on error.
    [[nodiscard]] u64 tell_file(handle handle);


    // Closes an open file handle. Does nothing if handle is invalid.
    void close_file(handle handle);
    
    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

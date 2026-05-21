
#pragma once

#include <filesystem>
#include "util/macros.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::vfs {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class file_open_mode : u32 {
        read        = BIT(0),
        write       = BIT(1),
        append      = BIT(2),
        truncate    = BIT(3),
        create      = BIT(4)
    };


    enum class filesystem_type {
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


    using file_handle = u64;
    constexpr file_handle invalid_file_handle = 0;


    using exists_func = bool (*)(const std::filesystem::path& path);
    using is_directory_func = bool (*)(const std::filesystem::path& path);
    using is_regular_file_func = bool (*)(const std::filesystem::path& path);
    using create_directory_func = bool (*)(const std::filesystem::path& path);
    using create_directories_func = bool (*)(const std::filesystem::path& path);
    using remove_func = bool (*)(const std::filesystem::path& path);
    using rename_func = bool (*)(const std::filesystem::path& old_path, const std::filesystem::path& new_path);
    using copy_file_func = bool (*)(const std::filesystem::path& from, const std::filesystem::path& to, bool overwrite);
    using file_size_func = u64 (*)(const std::filesystem::path& path);
    using list_directory_func = std::vector<std::filesystem::path> (*)(const std::filesystem::path& path);
    using read_text_file_func = std::string (*)(const std::filesystem::path& path);
    using write_text_file_func = bool (*)(const std::filesystem::path& path, const std::string& content);
    using open_file_func = file_handle (*)(const std::filesystem::path& path, file_open_mode mode);
    using read_file_func = size_t (*)(file_handle handle, void* buffer, size_t size, size_t offset);
    using write_file_func = size_t (*)(file_handle handle, const void* data, size_t size, size_t offset);
    using seek_file_func = bool (*)(file_handle handle, i64 offset, int origin);
    using tell_file_func = u64 (*)(file_handle handle);
    using close_file_func = void (*)(file_handle handle);

    struct vfs_functions {
        exists_func                 exists;
        is_directory_func           is_directory;
        is_regular_file_func        is_regular_file;
        create_directory_func       create_directory;
        create_directories_func     create_directories;
        remove_func                 remove;
        rename_func                 rename;
        copy_file_func              copy_file;
        file_size_func              file_size;
        list_directory_func         list_directory;
        read_text_file_func         read_text_file;
        write_text_file_func        write_text_file;
        open_file_func              open_file;
        read_file_func              read_file;
        write_file_func             write_file;
        seek_file_func              seek_file;
        tell_file_func              tell_file;
        close_file_func             close_file;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    CORE_API void install_vfs_functions(const vfs_functions& funcs);


    CORE_API [[nodiscard]] filesystem_type get_filesystem_type();


    CORE_API void set_filesystem_type(const filesystem_type type);

    // ------ Standard file system operations (path‑based) ---------------------------------------------------------


    // Checks whether a file or directory exists at the given path.
    [[nodiscard]] bool exists(const std::filesystem::path& path);


    // Checks whether the given path points to a directory.
    [[nodiscard]] bool is_directory(const std::filesystem::path& path);


    // Checks whether the given path points to a regular file.
    [[nodiscard]] bool is_regular_file(const std::filesystem::path& path);


    // Creates a directory. Returns true on success.
    bool create_directory(const std::filesystem::path& path);


    // Creates a directory. Returns true on success.
    bool create_directories(const std::filesystem::path& path);


    // Removes a file or an empty directory. Returns true on success.
    bool remove(const std::filesystem::path& path);


    // Renames (moves) a file or directory. Returns true on success.
    bool rename(const std::filesystem::path& old_path, const std::filesystem::path& new_path);


    // Copies a file. Overwrites destination only if overwrite == true.
    bool copy_file(const std::filesystem::path& from, const std::filesystem::path& to, bool overwrite = false);


    // Returns the size in bytes of a regular file. Returns 0 if file does not exist.
    [[nodiscard]] u64 file_size(const std::filesystem::path& path);


    // Lists the contents of a directory. Returns empty vector on failure or empty directory.
    [[nodiscard]] std::vector<std::filesystem::path> list_directory(const std::filesystem::path& path);

    // ------ Convenience text file operations ---------------------------------------------------------------------


    // Reads the entire content of a text file as a string. Returns empty string on failure.
    [[nodiscard]] std::string read_text_file(const std::filesystem::path& path);


    // Writes a string to a text file (overwrites). Returns true on success.
    bool write_text_file(const std::filesystem::path& path, const std::string& content);

    // ------ Handle‑based binary file I/O -------------------------------------------------------------------------

    // Opens a file with the given mode flags. Returns a non‑zero handle or invalid_file_handle on error.
    [[nodiscard]] file_handle open_file(const std::filesystem::path& path, file_open_mode mode);


    // Reads up to 'size' bytes into 'buffer' from the open file.
    //      If offset == static_cast<size_t>(-1), reads from the current file position.
    //      Returns the number of bytes actually read.
    size_t read_file(file_handle handle, void* buffer, size_t size, size_t offset = static_cast<size_t>(-1));


    // Writes 'size' bytes from 'data' into the open file.
    //      If offset == static_cast<size_t>(-1), writes at the current file position.
    //      Returns the number of bytes actually written.
    size_t write_file(file_handle handle, const void* data, size_t size, size_t offset = static_cast<size_t>(-1));


    // Repositions the file pointer.
    //      origin: 0 = seek_set (absolute offset), 1 = seek_cur (relative), 2 = seek_end (relative, offset <= 0).
    //      Returns true on success.
    bool seek_file(file_handle handle, i64 offset, int origin);


    // Returns the current position of the file pointer. Returns 0 on error.
    [[nodiscard]] u64 tell_file(file_handle handle);


    // Closes an open file handle. Does nothing if handle is invalid.
    void close_file(file_handle handle);
    
    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

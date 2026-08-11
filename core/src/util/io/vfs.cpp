
#include "util/pch.h"
#include "vfs.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::vfs {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    
    #if defined(BUILD_GAME)

        static filesystem_type      s_filesystem_type = filesystem_type::zip;

    #else

        static filesystem_type      s_filesystem_type = filesystem_type::native;
        
    #endif

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Convert file_open_mode flags to fopen mode string
    static const char* mode_string_from_flags(file_open_mode mode) {

        if ((mode & file_open_mode::read) && (mode & file_open_mode::write)) {
            if (mode & file_open_mode::append)
                return "a+";          // read + append
            else if (mode & file_open_mode::truncate)
                return "w+";          // read + write, truncate
            else
                return "r+";          // read + write, no truncate

        } else if (mode & file_open_mode::read) {
            return "rb";

        } else if (mode & file_open_mode::write) {
            if (mode & file_open_mode::append)
                return "ab";
            else if (mode & file_open_mode::truncate)
                return "wb";
            else
                return "wb";          // write without truncate? Not portable, use wb
        }
        // fallback
        return "rb";
    }


    bool default_exists(const std::filesystem::path& path, std::error_code& error) {

        return std::filesystem::exists(path, error);
    }


    void default_create_file(const std::filesystem::path& path, std::error_code& error) noexcept {

        if (std::filesystem::exists(path, error))           // Check existence (non‑throwing)
            return;         // File already exists – success (error is cleared by exists() on success)

        if (error)
            return;         // An error occurred during the existence check – propagate it

        // File does not exist → create it exclusively.
        std::FILE* f = std::fopen(path.c_str(), "wx");      // "wx" mode: create for writing, fail if file already exists.
        if (f) {

            std::fclose(f);
            error.clear();                                  // success

        } else {

            error.assign(errno, std::generic_category());   // capture failure

            // If someone else created the file between our exists() and fopen(),
            // that's still a successful outcome – the file now exists.
            if (error == std::errc::file_exists)
                error.clear();
        }
    }


    bool default_is_directory(const std::filesystem::path& path, std::error_code& error) {

        return std::filesystem::is_directory(path, error);
    }


    bool default_is_regular_file(const std::filesystem::path& path, std::error_code& error) {

        return std::filesystem::is_regular_file(path, error);
    }


    void default_create_directory(const std::filesystem::path& path, std::error_code& error) {

        std::filesystem::create_directory(path, error);
    }


    void default_create_directories(const std::filesystem::path& path, std::error_code& error) {

        std::filesystem::create_directories(path, error);
    }


    void default_remove(const std::filesystem::path& path, std::error_code& error) {

        std::filesystem::remove(path, error);
    }


    void default_rename(const std::filesystem::path& old_path, const std::filesystem::path& new_path, std::error_code& error) {

        std::filesystem::rename(old_path, new_path, error);
    }


    void default_copy_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error, bool overwrite) {

        std::filesystem::copy_options options = std::filesystem::copy_options::none;
        if (overwrite) {
            options = std::filesystem::copy_options::overwrite_existing;
        }

        std::filesystem::copy_file(from, to, options, error);
    }


    [[nodiscard]] u64 default_file_size(const std::filesystem::path& path, std::error_code& error) {

        auto size = std::filesystem::file_size(path);
        return error ? 0 : static_cast<u64>(size);
    }


    [[nodiscard]] std::string default_read_text_file(const std::filesystem::path& path, std::error_code& error) {

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            // Capture errno immediately – it may be reset by other calls.
            error = std::error_code(errno, std::generic_category());
            return {};
        }
        std::ostringstream oss;
        oss << file.rdbuf();

        // If we reach here, no error occurred – clear any previous error.
        error.clear();
        return oss.str();
    }


    bool default_write_text_file(const std::filesystem::path& path, const std::string& content) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        return !file.fail();
    }


    [[nodiscard]] file_handle default_open_file(const std::filesystem::path& path, GLT::vfs::file_open_mode mode, std::error_code& error) noexcept {

        const char* mode_str = mode_string_from_flags(mode);                // Obtain the correct fopen mode string
        if (!mode_str)
            return 0;                                                       // invalid mode

        std::FILE* f = std::fopen(path.c_str(), mode_str);                  // First attempt to open with the chosen mode
        if (f)
            return reinterpret_cast<u64>(f);

        // If it failed, decide whether we should create the file and retry.
        // The "r" and "r+" modes do NOT create a missing file.
        // If the caller asked for 'create', we create the file now and retry.
        bool use_create_fallback = false;
        if (mode & GLT::vfs::file_open_mode::create) {
            int e = errno;                                                  // Check if the error is "file not found"
            if (e == ENOENT) {
                // Only "r" and "r+" would have failed with ENOENT.
                // The other modes ("w","w+","a","a+") already create,
                // so they would have succeeded or failed for another reason.
                if (std::strcmp(mode_str, "r") == 0 || std::strcmp(mode_str, "r+") == 0) {
                    use_create_fallback = true;
                }
            }
        }

        if (use_create_fallback) {
            std::FILE* creator = std::fopen(path.c_str(), "wx");            // Create the file exclusively (like 'wx'), then reopen with the original mode.
            if (creator) {
                std::fclose(creator);
                f = std::fopen(path.c_str(), mode_str);                     // Now the file exists → reopen with the desired mode
                if (f)
                    return reinterpret_cast<u64>(f);
            } else if (errno == EEXIST) {                                   // Reopen failed for some other reason – fall through to error
                // Race: someone else created it between our failed open and wx.
                // That's fine – try again with the desired mode.
                f = std::fopen(path.c_str(), mode_str);
                if (f)
                    return reinterpret_cast<u64>(f);
            }
        }

        error.assign(errno, std::generic_category());                       // If we get here, all attempts failed.
        return 0;
    }


    size_t default_read_file(file_handle handle, void* buffer, size_t size, size_t offset) {

        if (handle == invalid_file_handle || !buffer || size == 0) {
            return 0;
        }
        FILE* f = reinterpret_cast<FILE*>(handle);
        if (offset != static_cast<size_t>(-1)) {
            if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) {
                return 0;
            }
        }
        return std::fread(buffer, 1, size, f);
    }


    size_t default_write_file(file_handle handle, const void* data, size_t size, size_t offset) {
        
        if (handle == invalid_file_handle || !data || size == 0) {
            return 0;
        }
        FILE* f = reinterpret_cast<FILE*>(handle);
        if (offset != static_cast<size_t>(-1)) {
            if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) {
                return 0;
            }
        }
        return std::fwrite(data, 1, size, f);
    }


    bool default_seek_file(file_handle handle, i64 offset, int origin) {

        if (handle == invalid_file_handle) {
            return false;
        }
        FILE* f = reinterpret_cast<FILE*>(handle);
        int std_origin;
        switch (origin) {
            case 0: std_origin = SEEK_SET; break;
            case 1: std_origin = SEEK_CUR; break;
            case 2: std_origin = SEEK_END; break;
            default: return false;
        }
        return std::fseek(f, static_cast<long>(offset), std_origin) == 0;
    }


    [[nodiscard]] u64 default_tell_file(file_handle handle) {
        if (handle == invalid_file_handle) {
            return 0;
        }
        FILE* f = reinterpret_cast<FILE*>(handle);
        long pos = std::ftell(f);
        return (pos == -1L) ? 0 : static_cast<u64>(pos);
    }


    void default_close_file(file_handle handle) {
        if (handle != invalid_file_handle) {
            std::fclose(reinterpret_cast<FILE*>(handle));
        }
    }


    static vfs_functions g_vfs = {
        default_exists,
        default_create_file,
        default_is_directory,
        default_is_regular_file,
        default_create_directory,
        default_create_directories,
        default_remove,
        default_rename,
        default_copy_file,
        default_file_size,
        default_read_text_file,
        default_write_text_file,
        default_open_file,
        default_read_file,
        default_write_file,
        default_seek_file,
        default_tell_file,
        default_close_file,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    [[nodiscard]] filesystem_type get_filesystem_type() { return s_filesystem_type; }


    void set_filesystem_type(const filesystem_type type) { s_filesystem_type = type; }


    void install_vfs_functions(const vfs_functions& funcs) {
        g_vfs = funcs;   // safe if called before any VFS operations run
    }


    bool exists(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        return g_vfs.exists(path, error);
    }


    void create_file(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        g_vfs.create_file(path, error);
    }


    bool is_directory(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        return g_vfs.is_directory(path, error);
    }


    bool is_regular_file(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        return g_vfs.is_regular_file(path, error);
    }


    void create_directory(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        g_vfs.create_directory(path, error);
    }

        
    void create_directories(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        g_vfs.create_directories(path, error);
    }


    void remove(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        g_vfs.remove(path, error);
    }


    void rename(const std::filesystem::path& old_path, const std::filesystem::path& new_path, std::error_code& error) {

        error.clear();
        g_vfs.rename(old_path, new_path, error);
    }


    void copy_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error, bool overwrite) {

        error.clear();
        g_vfs.copy_file(from, to, error, overwrite);
    }


    u64 file_size(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        return g_vfs.file_size(path, error);
    }


    [[nodiscard]] std::string read_text_file(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
        return g_vfs.read_text_file(path, error);
    }


    bool write_text_file(const std::filesystem::path& path, const std::string& content) {

        return g_vfs.write_text_file(path, content);
    }


    [[nodiscard]] file_handle open_file(const std::filesystem::path& path, GLT::vfs::file_open_mode mode, std::error_code& error) noexcept {

        error.clear();
        return g_vfs.open_file(path, mode, error);
    }


    size_t read_file(file_handle handle, void* buffer, size_t size, size_t offset) {

        return g_vfs.read_file(handle, buffer, size, offset);
    }


    size_t write_file(file_handle handle, const void* data, size_t size, size_t offset) {

        return g_vfs.write_file(handle, data, size, offset);
    }


    bool seek_file(file_handle handle, i64 offset, int origin) {

        return g_vfs.seek_file(handle, offset, origin);
    }


    u64 tell_file(file_handle handle) {

        return g_vfs.tell_file(handle);
    }


    void close_file(file_handle handle) {

        g_vfs.close_file(handle);
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

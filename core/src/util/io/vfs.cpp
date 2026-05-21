
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

    CORE_API [[nodiscard]] filesystem_type get_filesystem_type() { return s_filesystem_type; }


    CORE_API void set_filesystem_type(const filesystem_type type) { s_filesystem_type = type; }


    [[nodiscard]] bool default_exists(const std::filesystem::path& path) {

        return std::filesystem::exists(path);
    }


    [[nodiscard]] bool default_is_directory(const std::filesystem::path& path) {

        return std::filesystem::is_directory(path);
    }


    [[nodiscard]] bool default_is_regular_file(const std::filesystem::path& path) {

        return std::filesystem::is_regular_file(path);
    }


    bool default_create_directory(const std::filesystem::path& path) {

        return std::filesystem::create_directory(path);
    }


    bool default_create_directories(const std::filesystem::path& path) {

		std::error_code error{};
        std::filesystem::create_directories(path, error);
        return !error;
    }


    bool default_remove(const std::filesystem::path& path) {
        return std::filesystem::remove(path);
    }


    bool default_rename(const std::filesystem::path& old_path, const std::filesystem::path& new_path) {
        std::error_code ec;
        std::filesystem::rename(old_path, new_path, ec);
        return !ec;
    }


    bool default_copy_file(const std::filesystem::path& from, const std::filesystem::path& to, bool overwrite) {
        std::filesystem::copy_options options = std::filesystem::copy_options::none;
        if (overwrite) {
            options = std::filesystem::copy_options::overwrite_existing;
        }
        std::error_code ec;
        std::filesystem::copy_file(from, to, options, ec);
        return !ec;
    }


    [[nodiscard]] u64 default_file_size(const std::filesystem::path& path) {
        std::error_code ec;
        auto size = std::filesystem::file_size(path, ec);
        return ec ? 0 : static_cast<u64>(size);
    }


    [[nodiscard]] std::vector<std::filesystem::path> default_list_directory(const std::filesystem::path& path) {
        std::vector<std::filesystem::path> result;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            if (!ec) {
                result.push_back(entry.path());
            }
        }
        return result;
    }


    [[nodiscard]] std::string default_read_text_file(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return {};
        }
        std::ostringstream oss;
        oss << file.rdbuf();
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


    [[nodiscard]] file_handle default_open_file(const std::filesystem::path& path, file_open_mode mode) {
        const char* mode_str = mode_string_from_flags(mode);
        FILE* f = std::fopen(path.string().c_str(), mode_str);
        if (!f) {
            return invalid_file_handle;
        }
        // Store FILE* as u64 (pointer fits on 64‑bit platforms)
        return reinterpret_cast<file_handle>(f);
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
        default_is_directory,
        default_is_regular_file,
        default_create_directory,
        default_create_directories,
        default_remove,
        default_rename,
        default_copy_file,
        default_file_size,
        default_list_directory,
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

    void install_vfs_functions(const vfs_functions& funcs) {
        g_vfs = funcs;   // safe if called before any VFS operations run
    }


    bool exists(const std::filesystem::path& path) {

        return g_vfs.exists(path);
    }


    bool is_directory(const std::filesystem::path& path) {

        return g_vfs.is_directory(path);
    }


    bool is_regular_file(const std::filesystem::path& path) {

        return g_vfs.is_regular_file(path);
    }


    bool create_directory(const std::filesystem::path& path) {

        return g_vfs.create_directory(path);
    }

        
    bool create_directories(const std::filesystem::path& path) {

        return g_vfs.create_directories(path);
    }


    bool remove(const std::filesystem::path& path) {

        return g_vfs.remove(path);
    }


    bool rename(const std::filesystem::path& old_path, const std::filesystem::path& new_path) {

        return g_vfs.rename(old_path, new_path);
    }


    bool copy_file(const std::filesystem::path& from, const std::filesystem::path& to, bool overwrite) {

        return g_vfs.copy_file(from, to, overwrite);
    }


    u64 file_size(const std::filesystem::path& path) {

        return g_vfs.file_size(path);
    }


    std::vector<std::filesystem::path> list_directory(const std::filesystem::path& path) {

        return g_vfs.list_directory(path);
    }


    std::string read_text_file(const std::filesystem::path& path) {

        return g_vfs.read_text_file(path);
    }


    bool write_text_file(const std::filesystem::path& path, const std::string& content) {

        return g_vfs.write_text_file(path, content);
    }


    file_handle open_file(const std::filesystem::path& path, file_open_mode mode) {

        return g_vfs.open_file(path, mode);
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

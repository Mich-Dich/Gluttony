
#include <vfspp/VFS.h>
#include <unordered_map>
#include <shared_mutex>
#include <cstring>
#include <cstdio>

#include "native.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::vfs_plugin::native {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static std::unique_ptr<vfspp::VirtualFileSystem>        g_vfs;

    static std::string                                      g_native_base_path;                 // resolved real path of mounted "/"

    static std::unordered_map<u64, vfspp::IFilePtr>         g_open_files;

    static std::shared_mutex                                g_handle_mutex;

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Convert file_open_mode flags to fopen mode string
    static const char* mode_string_from_flags(GLT::vfs::file_open_mode mode) {

        if ((mode & GLT::vfs::file_open_mode::read) && (mode & GLT::vfs::file_open_mode::write)) {
            if (mode & GLT::vfs::file_open_mode::append)
                return "a+";          // read + append
            else if (mode & GLT::vfs::file_open_mode::truncate)
                return "w+";          // read + write, truncate
            else
                return "r+";          // read + write, no truncate

        } else if (mode & GLT::vfs::file_open_mode::read) {
            return "rb";

        } else if (mode & GLT::vfs::file_open_mode::write) {
            if (mode & GLT::vfs::file_open_mode::append)
                return "ab";
            else if (mode & GLT::vfs::file_open_mode::truncate)
                return "wb";
            else
                return "wb";          // write without truncate? Not portable, use wb
        }
        // fallback
        return "rb";
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    bool init() {

        if (g_vfs)
            return true;
            
        g_vfs = std::make_unique<vfspp::VirtualFileSystem>();
            
        const auto used_type = GLT::vfs::get_filesystem_type();
        switch (used_type) {
            default:                                    [[fallthrough]];
            case GLT::vfs::filesystem_type::native:     break;              // Dont need VFSPP for native   
            case GLT::vfs::filesystem_type::memory: {

                ASSERT(false, "", "Memory filesystem not yet implemented")
                break;
            }
            case GLT::vfs::filesystem_type::zip: {

                ASSERT(false, "", "Zip filesystem not yet implemented")
                break;
            }
        }

        return true;
    }


    void shutdown() {

        // Close all remaining open handles
        {
            std::unique_lock lock(g_handle_mutex);
            for (auto& [handle, file] : g_open_files) {
                if (file && file->IsOpened())
                    file->Close();
            }
            g_open_files.clear();
        }
        g_vfs.reset();
        g_native_base_path.clear();
    }

    // ----- core VFS callback implementations -------------------------------------------------------------------------

    bool exists(const std::filesystem::path& path, std::error_code& error) {

        return std::filesystem::exists(path, error);
    }


    void create_file(const std::filesystem::path& path, std::error_code& error) {

        error.clear();
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


    bool is_directory(const std::filesystem::path& path, std::error_code& error) {

        return std::filesystem::is_directory(path, error);
    }


    bool is_regular_file(const std::filesystem::path& path, std::error_code& error) {

        return std::filesystem::is_regular_file(path, error);
    }


    void create_directory(const std::filesystem::path& path, std::error_code& error) {

        std::filesystem::create_directory(path, error);
    }


    void create_directories(const std::filesystem::path& path, std::error_code& error) {
        
        std::filesystem::create_directories(path, error);
    }


    void remove(const std::filesystem::path& path, std::error_code& error) {

        std::filesystem::remove(path, error);
    }


    void rename(const std::filesystem::path& old_path, const std::filesystem::path& new_path, std::error_code& error) {

        std::filesystem::rename(old_path, new_path, error);
    }


    void copy_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error, bool overwrite) {

        std::filesystem::copy_options opt = overwrite ? std::filesystem::copy_options::overwrite_existing
                                                      : std::filesystem::copy_options::skip_existing;
        std::filesystem::copy_file(from, to, opt, error);
    }


    u64 file_size(const std::filesystem::path& path, std::error_code& error) {

        auto size = std::filesystem::file_size(path, error);
        return error ? 0 : size;
    }


    // std::vector<std::filesystem::path> list_directory(const std::filesystem::path& path, std::error_code& error) {

    //     std::vector<std::filesystem::path> result;
    //     for (auto& entry : std::filesystem::directory_iterator(path, error)) {
    //         result.push_back(entry.path());
    //     }
    //     return result;
    // }


    std::string read_text_file(const std::filesystem::path& path) {

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string content(size, '\0');
        if (file.read(content.data(), size))
            return content;
        return {};
    }


    bool write_text_file(const std::filesystem::path& path, const std::string& content) {

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file.write(content.data(), content.size());
        return file.good();
    }

    // ----- handle‑based I/O (uses vfspp through g_vfs) ---------------------------------------------------------------

    [[nodiscard]] GLT::vfs::file_handle open_file(const std::filesystem::path& path, GLT::vfs::file_open_mode mode, std::error_code& error) noexcept {

        error.clear();

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


    size_t read_file(u64 handle, void* buffer, size_t size, size_t offset) {

        std::shared_lock lock(g_handle_mutex);
        auto it = g_open_files.find(handle);
        if (it == g_open_files.end())
            return 0;
        auto& file = it->second;
        lock.unlock();

        if (!file->IsOpened())
            return 0;

        if (offset != static_cast<size_t>(-1))
            file->Seek(offset, vfspp::IFile::Origin::Set);

        std::span<uint8_t> span(static_cast<uint8_t*>(buffer), size);
        return static_cast<size_t>(file->Read(span));
    }


    size_t write_file(u64 handle, const void* data, size_t size, size_t offset) {

        std::shared_lock lock(g_handle_mutex);
        auto it = g_open_files.find(handle);
        if (it == g_open_files.end())
            return 0;
        auto& file = it->second;
        lock.unlock();

        if (!file->IsOpened())
            return 0;

        if (offset != static_cast<size_t>(-1))
            file->Seek(offset, vfspp::IFile::Origin::Set);

        std::span<const uint8_t> span(static_cast<const uint8_t*>(data), size);
        return static_cast<size_t>(file->Write(span));
    }


    bool seek_file(u64 handle, i64 offset, int origin) {

        std::shared_lock lock(g_handle_mutex);
        auto it = g_open_files.find(handle);
        if (it == g_open_files.end())
            return false;
        auto& file = it->second;
        lock.unlock();

        if (!file->IsOpened())
            return false;

        vfspp::IFile::Origin vfsOrigin;
        switch (origin) {
            case 0: vfsOrigin = vfspp::IFile::Origin::Set; break;   // SEEK_SET
            case 1: vfsOrigin = vfspp::IFile::Origin::Begin; break; // SEEK_CUR (note: vfspp uses Begin, Cur? Actually vfspp has Begin, End, Set)
            case 2: vfsOrigin = vfspp::IFile::Origin::End; break;   // SEEK_END
            default: return false;
        }
        file->Seek(static_cast<u64>(offset), vfsOrigin);
        return true;
    }


    u64 tell_file(u64 handle) {

        std::shared_lock lock(g_handle_mutex);
        auto it = g_open_files.find(handle);
        if (it == g_open_files.end())
            return 0;
        auto& file = it->second;
        lock.unlock();

        if (!file->IsOpened())
            return 0;

        return file->Tell();
    }


    void close_file(u64 handle) {

        std::unique_lock lock(g_handle_mutex);
        auto it = g_open_files.find(handle);
        if (it == g_open_files.end())
            return;
        auto file = it->second;
        g_open_files.erase(it);
        lock.unlock();

        if (file && file->IsOpened())
            file->Close();
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

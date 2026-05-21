
#include <vfspp/VFS.h>
#include <unordered_map>
#include <shared_mutex>
#include <cstring>

#include "vfs.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::vfs_plugin {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static std::unique_ptr<vfspp::VirtualFileSystem>        g_vfs;
    static std::string                                      g_native_base_path;                 // resolved real path of mounted "/"
    static std::unordered_map<u64, vfspp::IFilePtr>         g_open_files;
    static std::shared_mutex                                g_handle_mutex;
    static u64                                              g_next_handle = 1;

    // FUNCTION IMPLEMENTATION =========================================================================================
        
    // helper: convert core’s file_open_mode to vfspp::IFile::FileMode
    static vfspp::IFile::FileMode ToVfsppMode(GLT::vfs::file_open_mode mode) {

        using core_mode = GLT::vfs::file_open_mode;
        using vfs_mode = vfspp::IFile::FileMode;

        int result = 0;
        if (mode & core_mode::read)         result |= static_cast<int>(vfs_mode::Read);
        if (mode & core_mode::write)        result |= static_cast<int>(vfs_mode::Write);
        if (mode & core_mode::append)       result |= static_cast<int>(vfs_mode::Append);
        if (mode & core_mode::truncate)     result |= static_cast<int>(vfs_mode::Truncate);

        // If neither read nor write was requested, default to read
        if ((result & (static_cast<int>(vfs_mode::Read) | static_cast<int>(vfs_mode::Write))) == 0)
            result |= static_cast<int>(vfs_mode::Read);

        return static_cast<vfs_mode>(result);
    }

    // ----- plugin lifecycle ------------------------------------------------------------------------------------------

    bool init() {

        if (g_vfs)
            return true;
            
        g_vfs = std::make_unique<vfspp::VirtualFileSystem>();
            
        const auto used_type = GLT::vfs::get_filesystem_type();
        switch (used_type) {
            default:                                    [[fallthrough]];
            case GLT::vfs::filesystem_type::native: {

                // Determine a base path – here we use the current working directory.
                // TODO: read a config value.
                std::filesystem::path base = std::filesystem::current_path();
                g_native_base_path = base.generic_string();

                // Create and mount a native filesystem at alias "/"
                auto native_fs = std::make_shared<vfspp::NativeFileSystem>("/", g_native_base_path);
                VALIDATE(native_fs->Initialize(), g_vfs.reset(); return false, "", "")
                g_vfs->AddFileSystem("/", native_fs);
                break;
            }   
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

    // ----- helper: resolve a virtual path to a native path -----------------------------------------------------------
    // For a simple setup where "/" maps to g_native_base_path, we just strip the leading '/'
    static std::filesystem::path to_native_path(const std::filesystem::path& virtual_path) {

        std::string v = virtual_path.generic_string();
        if (v.empty() || v == "/")
            return g_native_base_path;

        if (v.front() == '/')
            v.erase(0, 1);

        return std::filesystem::path(g_native_base_path) / v;
    }

    // ----- core VFS callback implementations -------------------------------------------------------------------------

    bool exists_impl(const std::filesystem::path& path) {

        if (!g_vfs) return false;
        // vfspp only has IsFileExists (returns true for files and directories)
        return g_vfs->IsFileExists(path.generic_string());
    }


    bool is_directory_impl(const std::filesystem::path& path) {

        // Use std::filesystem on the native path because vfspp does not expose is_directory
        return std::filesystem::is_directory(to_native_path(path));
    }


    bool is_regular_file_impl(const std::filesystem::path& path) {

        return std::filesystem::is_regular_file(to_native_path(path));
    }


    bool create_directory_impl(const std::filesystem::path& path) {

        return std::filesystem::create_directory(to_native_path(path));
    }


    bool default_create_directories(const std::filesystem::path& path) {
        
        return std::filesystem::create_directories(path);
    }


    bool remove_impl(const std::filesystem::path& path) {

        return std::filesystem::remove(to_native_path(path));
    }


    bool rename_impl(const std::filesystem::path& old_path, const std::filesystem::path& new_path) {

        std::error_code ec;
        std::filesystem::rename(to_native_path(old_path), to_native_path(new_path), ec);
        return !ec;
    }


    bool copy_file_impl(const std::filesystem::path& from, const std::filesystem::path& to, bool overwrite) {

        std::filesystem::copy_options opt = overwrite ? std::filesystem::copy_options::overwrite_existing
                                                      : std::filesystem::copy_options::skip_existing;
        return std::filesystem::copy_file(to_native_path(from), to_native_path(to), opt);
    }


    u64 file_size_impl(const std::filesystem::path& path) {

        std::error_code ec;
        auto sz = std::filesystem::file_size(to_native_path(path), ec);
        return ec ? 0 : sz;
    }


    std::vector<std::filesystem::path> list_directory_impl(const std::filesystem::path& path) {

        std::vector<std::filesystem::path> result;
        std::error_code ec;
        for (auto& entry : std::filesystem::directory_iterator(to_native_path(path), ec)) {
            result.push_back(entry.path());
        }
        return result;
    }


    std::string read_text_file_impl(const std::filesystem::path& path) {

        std::ifstream file(to_native_path(path), std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::string content(size, '\0');
        if (file.read(content.data(), size))
            return content;
        return {};
    }


    bool write_text_file_impl(const std::filesystem::path& path, const std::string& content) {

        std::ofstream file(to_native_path(path), std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file.write(content.data(), content.size());
        return file.good();
    }

    // ----- handle‑based I/O (uses vfspp through g_vfs) ---------------------------------------------------------------

    u64 open_file_impl(const std::filesystem::path& path, GLT::vfs::file_open_mode mode) {

        if (!g_vfs)
            return 0;

        auto vfsMode = ToVfsppMode(mode);
        auto file = g_vfs->OpenFile(path.generic_string(), vfsMode);
        if (!file || !file->IsOpened())
            return 0;

        std::unique_lock lock(g_handle_mutex);
        u64 handle = g_next_handle++;
        g_open_files[handle] = file;
        return handle;
    }


    size_t read_file_impl(u64 handle, void* buffer, size_t size, size_t offset) {

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


    size_t write_file_impl(u64 handle, const void* data, size_t size, size_t offset) {

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


    bool seek_file_impl(u64 handle, i64 offset, int origin) {

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


    u64 tell_file_impl(u64 handle) {

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


    void close_file_impl(u64 handle) {

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


#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <thread>

#include <util/data_structures/data_types.h>
#include <util/core_config.h>
#include <util/macros.h>
#include <util/io/logger.h>
#include <util/io/vfs.h>

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::vfs_plugin {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // ----- plugin lifecycle ------------------------------------------------------------------------------------------

    bool init();


    void shutdown();

    // ----- implementation of every core vfs_functions entry point ----------------------------------------------------
        
    bool exists_impl(const std::filesystem::path& path);
    
    
    bool is_directory_impl(const std::filesystem::path& path);
    
    
    bool is_regular_file_impl(const std::filesystem::path& path);
    
    
    bool create_directory_impl(const std::filesystem::path& path);


    bool create_directories_impl(const std::filesystem::path& path);
    
    
    bool remove_impl(const std::filesystem::path& path);
    
    
    bool rename_impl(const std::filesystem::path& old_path, const std::filesystem::path& new_path);
    
    
    bool copy_file_impl(const std::filesystem::path& from, const std::filesystem::path& to, bool overwrite);
    
    
    u64 file_size_impl(const std::filesystem::path& path);
    
    
    std::vector<std::filesystem::path> list_directory_impl(const std::filesystem::path& path);
    
    
    std::string read_text_file_impl(const std::filesystem::path& path);
    
    
    bool write_text_file_impl(const std::filesystem::path& path, const std::string& content);

    // ----- handle‑based I/O ------------------------------------------------------------------------------------------
    
    u64 open_file_impl(const std::filesystem::path& path, GLT::vfs::file_open_mode mode);
    
    
    size_t read_file_impl(u64 handle, void* buffer, size_t size, size_t offset);
    
    
    size_t write_file_impl(u64 handle, const void* data, size_t size, size_t offset);
    
    
    bool seek_file_impl(u64 handle, i64 offset, int origin);
    
    
    u64 tell_file_impl(u64 handle);
    
    
    void close_file_impl(u64 handle);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}


#pragma once

#include <plugin_system/i_renderer_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::icon_manager {

    // CONSTANTS =======================================================================================================
    
    using thumbnail_handle = u32;

    constexpr thumbnail_handle invalid_thumbnail_handle = 0;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class thumbnail_state : u8 {
        pending,    // load job is queued or in flight
        ready,      // atlas slot is populated, tex_ref is valid
        failed,     // couldn't load (not an image, missing, corrupted, ...)
    };


    struct thumbnail_data {

        thumbnail_state state = thumbnail_state::failed;
        ImTextureRef    tex_ref{};      // valid only when state == ready
        ImVec2          image_size{};   // scaled size, largest side <= 128 px
        ImVec2          uv0{};          // top-left UV of the image inside its atlas page
        ImVec2          uv1{};          // bottom-right UV
    };


    struct icon_data {

        ImTextureRef    tex_ref{};      // texture to pass to ImGui::Image()
        ImVec2          image_size{};   // native pixel size (for layout math)
        ImVec2          uv0{};          // top-left UV
        ImVec2          uv1{};          // bottom-right UV
    };

    // @brief Enumeration of all available icons that can be requested from the manager.
    //
    // Icons are grouped by purpose: window controls, connection status, media control,
    // warning/refresh, and NewTec branding.
    enum class icon : u8 {

        logo = 0,               // main Gluttony logo.
        logo_small,
        play,
        pause,
        stop,
        file,
        file_big,
        script,
        script_big,
        folder_add,
        folder_big,
        folder_open,
        folder,
        hide,
        material_inst,
        material,
        material_big,
        mesh_asset,
        mesh_asset_big,
        mesh_mini,
        relation,
        settings,
        show,
        texture,
        texture_big,
        transfrom_rotation,
        transfrom_scale,
        transfrom_translation,
        warning,
        world,
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Initialise the icon manager.
    //
    // This function must be called once before any icons are requested.
    // It sets up the internal resource manager and starts the background cleanup thread.
    void init();


    // @brief Shut down the icon manager and release all loaded icons.
    //
    // Stops the background cleanup thread and releases all cached icon resources.
    // No further calls to @ref get() should be made after shutdown.
    void shutdown();


    //
    icon_data get(const icon type);


    // @brief Check whether an icon is currently loaded and has active references.
    //
    // Useful for debugging or for determining if an icon should be kept in memory.
    //
    // @param type The icon type to check.
    // @return true if the icon is loaded and its reference count > 0,
    // @return false otherwise.
    bool is_asset_in_active_use(const icon type);


    // Request a thumbnail for `path`. The first call queues a load job and
    // returns a handle in the `pending` state. Subsequent calls with the same
    // path return the same handle. Poll get_thumbnail() to know when it's done.
    //
    // Passing an empty path returns invalid_thumbnail_handle.
    thumbnail_handle request_thumbnail(const std::filesystem::path& path);


    // Poll a thumbnail handle. Cheap - safe to call every frame. Returns a
    // default-constructed (failed) thumbnail_data for an invalid handle.
    thumbnail_data get_thumbnail(thumbnail_handle handle);


    // Convenience one-shot: request_thumbnail + get_thumbnail. The first call
    // for a given path returns `pending`; subsequent calls return `ready` or
    // `failed`. Prefer the handle-based form when you can cache the handle.
    thumbnail_data get_thumbnail(const std::filesystem::path& path);


    // Must be called once per frame on the main thread, after all
    // request_thumbnail() calls for the frame have been made. Drains finished
    // load jobs into their atlas pages and uploads any dirty pages to the GPU.
    //
    // Cheap no-op when nothing is pending.
    void flush_thumbnail_uploads();


    // Approximate VRAM usage of all thumbnail atlas pages, in bytes. Debug only.
    u64 thumbnail_atlas_memory_usage();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

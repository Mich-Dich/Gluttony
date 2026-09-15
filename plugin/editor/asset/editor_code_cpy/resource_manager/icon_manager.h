
#pragma once

#include <plugin_system/i_renderer_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::icon_manager {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

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
        script,
        folder_add,
        folder_big,
        folder_open,
        folder,
        hide,
        material_inst,
        material,
        mesh_asset,
        mesh_mini,
        relation,
        settings,
        show,
        texture,
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

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

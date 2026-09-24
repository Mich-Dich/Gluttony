
#include "util/pch.h"
#include "texture_viewer.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <config/imgui_config.h>
#include <asset/texture.h>
#include <plugin_system/i_asset_registry_plugin.h>
#include <plugin_system/plugin_manager.h>
#include <render/image.h>
#include <resource_manager/icon_manager.h>

#include "util/ui/pannel_collection.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    texture_viewer_window::texture_viewer_window(const std::filesystem::path& /*path*/) { }


    texture_viewer_window::~texture_viewer_window() { }

    // CLASS PUBLIC ====================================================================================================

    void texture_viewer_window::window(const f32 /*delta_time*/) {

        if (!m_show_window)
            return;
    }


    void texture_viewer_window::update(const f32 /*delta_time*/) { }


    bool texture_viewer_window::serialize(const std::filesystem::path& /*project_file*/, const GLT::serializer::option /*option*/) { return false; }

    // CLASS PRIVATE ===================================================================================================

}

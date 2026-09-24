#pragma once

#include <imgui.h>
#include <glm/vec2.hpp>

#include <asset/type.h>                 // GLT::asset::handle, INVALID_HANDLE
#include <plugin_system/plugin_manager.h>

#include "window/base_window.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class texture_viewer_window : public base_window {
    public:

        texture_viewer_window(const std::filesystem::path& path);
        ~texture_viewer_window();

        void window(const f32 delta_time) override;


        void update(const f32 delta_time) override;


        bool serialize(const std::filesystem::path& project_file, const GLT::serializer::option option) override;

    private:

    };

}

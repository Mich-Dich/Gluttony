
#pragma once

#include <vk_ray/vk_ray.h>
#include <vk_ray/builders/builders.h>
// #include <imgui.h>
// #include <backends/imgui_impl_glfw.h>
// #include <backends/imgui_impl_vulkan.h>

#include "util/data_structures.h"
#include "util/shader_compiler.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    constexpr u32               MAX_CONCURRENT_FRAMES = 3;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class renderer {
    public:

        renderer();
        ~renderer();

        // DEFAULT_GETTER(weak_ref<GLT::camera>,           camera)
        // DEFAULT_SETTER(ref<GLT::camera>,                camera)

    private:

        // deletion_queue                                  m_deletion_queue{};
        // ref<GLT::camera>                                m_camera{};

    };

}

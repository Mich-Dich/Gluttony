
#include "util/pch.h"
#include "util/util_pch.h"

#include "plugin/plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                            dependencies_names[] = {

        nullptr
    };

    static constexpr GLT::plugin_manager::interface                         dependencies_interfaces[] = {

        GLT::plugin_manager::interface::window,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor                 descriptor = {

        .name                                                               = GLT_MODULE_NAME,
        .load_phase                                                         = GLT::plugin_manager::phase::pre_application,
        .unload_phase                                                       = GLT::plugin_manager::phase::post_application_shutdown,
        .target                                                             = GLT::plugin_manager::interface::renderer,
        .dependency_names_count                                             = ARRAY_SIZE(dependencies_names),
        .dependency_names                                                   = dependencies_names,
        .dependency_interface_count                                         = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                              = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

EXPORT_PLUGIN_CLASS(GLT::renderer::vk_rasterizer::renderer, GLT::renderer::vk_rasterizer::descriptor)

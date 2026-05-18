
#include <plugin_system/i_render_plugin.h>  

#include "renderer.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================


    static const char* dependencies_names[] = {

        nullptr
    };

    static GLT::plugin_manager::targeted_interface dependencies_interfaces[] = {
        
        GLT::plugin_manager::targeted_interface::none,
    };

    static GLT::plugin_manager::plugin_descriptor descriptor = {
        .name                               = GLT_MODULE_NAME,
        .phase                              = GLT::plugin_manager::load_phase::post_window,
        .target                             = GLT::plugin_manager::targeted_interface::graphics_api,
        .dependency_names_count             = ARRAY_SIZE(dependencies_names),
        .dependency_names                   = dependencies_names,
        .dependency_interface_count         = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces              = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class plugin : public GLT::render::i_renderer_plugin {
    public:

        bool create() override { 
            
            return false; 
        }


        void destroy() override { }

        // --- frame control -------------------------------------------------------------------------------------------

        void begin_frame() override { }


        void end_frame() override { }


        void present() override { }


        void wait_for_gpu() override { }

        // --- swapchain & configuration -------------------------------------------------------------------------------

        IGNORE_UNUSED_PARAMETER_START
        IGNORE_UNUSED_VARIABLE_START
        
        [[nodiscard]] glm::ivec2 get_swapchain_size() const override { return {}; }


        void resize(const u32 width, const u32 height) override { }


        void set_vsync(const bool enabled) override { }


        [[nodiscard]] bool get_vsync() const override { return false; }


        void set_clear_color(const glm::vec4& color) override { }

        // --- feature queries -----------------------------------------------------------------------------------------

        [[nodiscard]] GLT::render::renderer_feature get_supported_features() const override { return m_features; }

        // --- native access -------------------------------------------------------------------------------------------

        [[nodiscard]] void* get_native_device_handle() const override { return {}; }


        [[nodiscard]] void* get_native_context_handle() const override { return {}; }

        IGNORE_UNUSED_VARIABLE_STOP
        IGNORE_UNUSED_PARAMETER_STOP

    private:

        renderer                                m_renderer{};
        GLT::render::renderer_feature           m_features{};
    };

}

EXPORT_PLUGIN_CLASS(GLT::renderer_vk_ray::plugin, GLT::renderer_vk_ray::descriptor)

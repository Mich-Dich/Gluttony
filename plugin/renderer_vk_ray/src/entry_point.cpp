
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <vk_ray/vk_ray.h>
#include <vk_ray/builders/builders.h>
#include <plugin_system/i_renderer_plugin.h>
#include <plugin_system/i_window_plugin.h>

#include "util/utils.h"
#include "util/data_structures.h"
#include "util/shader_compiler.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    constexpr u32                                       MAX_CONCURRENT_FRAMES = 3;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    static const char*                                  dependencies_names[] = {

        nullptr
    };

    static GLT::plugin_manager::interface               dependencies_interfaces[] = {
        
        GLT::plugin_manager::interface::window,
    };

    static GLT::plugin_manager::plugin_descriptor       descriptor = {
        .name                                           = GLT_MODULE_NAME,
        .phase                                          = GLT::plugin_manager::load_phase::post_window,
        .target                                         = GLT::plugin_manager::interface::renderer,
        .dependency_names_count                         = ARRAY_SIZE(dependencies_names),
        .dependency_names                               = dependencies_names,
        .dependency_interface_count                     = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                          = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class renderer : public GLT::render::i_renderer_plugin {
    public:

        void on_load() { }

        
        void on_unload() { }
        

        bool create() override;


        void destroy() override;

        // --- frame control -------------------------------------------------------------------------------------------

        void begin_frame() override;


        void draw_frame() override;


        void wait_for_gpu() override { m_device.waitIdle(); }

        // --- swapchain & configuration -------------------------------------------------------------------------------

        IGNORE_UNUSED_PARAMETER_START
        IGNORE_UNUSED_VARIABLE_START
        
        [[nodiscard]] glm::ivec2 get_swapchain_size() const override { return {}; }


        void resize(const u32 width, const u32 height) override { }


        void set_vsync(const bool enabled) override { }


        [[nodiscard]] bool get_vsync() const override { return false; }


        void set_clear_color(const glm::vec4& color) override { }

        IGNORE_UNUSED_VARIABLE_STOP
        IGNORE_UNUSED_PARAMETER_STOP

        // --- feature queries -----------------------------------------------------------------------------------------

        [[nodiscard]] GLT::render::renderer_feature get_supported_features() const override { return m_features; }


        [[nodiscard]] GLT::render::backend_api get_backend_api() const override { return GLT::render::backend_api::vulkan; }

        // --- native access -------------------------------------------------------------------------------------------

        [[nodiscard]] void* get_native_device_handle() const override { return {}; }


        [[nodiscard]] void* get_native_context_handle() const override { return {}; }

    private:

        enum class image_type{
            swapchain = 0,
            render,
        };

        void init_vulkan();

        void create_base_resources();

        void transition_image_layout(vk::CommandBuffer command_buffer, const image_type type, const vk::ImageLayout new_layout);
        
	    void create_swapchain(const glm::ivec2 size);

	    void destroy_swapchain();

	    void resize_swapchain(const glm::ivec2 size);

        // --- IMGUI ---------------------------------------------------------------------------------------------------

        void imgui_init();
        
        void imgui_shutdown();

        void create_imgui_resources() {

            utils::create_imgui_resources(m_imgui_descriptor_pool, m_device, m_swapchain, m_imgui_render_pass,
                m_instance, m_physical_device, m_queues, m_imgui_framebuffers, m_imgui_initialized);
        }
        
        void destroy_imgui_resources() {

            utils::destroy_imgui_resources(m_device, m_imgui_framebuffers, m_imgui_render_pass, 
                m_imgui_descriptor_pool, m_imgui_initialized);
        }

        void begin_imgui_frame();
        
        void end_imgui_frame();
        

        GLT::render::renderer_feature                           m_features{};

        GLT::system_state                                       m_state = GLT::system_state::destroyed;
        vr::instance_wrapper                                    m_instance;
        vr::command_queues                                      m_queues{};
        vk::Device                                              m_device = nullptr;
        vk::SurfaceKHR                                          m_surface = nullptr;
        vk::PhysicalDevice                                      m_physical_device = nullptr;
        utils::deletion_queue                                   m_deletion_queue{};
        vr::swapchain_builder                                   m_swapchain_builder;
        vr::swapchain_resources                                 m_swapchain;
        vk::SwapchainKHR                                        m_old_swapchain = nullptr;

        
        vk::CommandPool                                         m_graphics_pool;
        u32                                                     m_image_count = 0;
        u32                                                     m_current_frame = 0;
        u32                                                     m_current_swapchain_image = 0;
        std::vector<vk::Semaphore>                              m_render_semaphores{};
        std::vector<vk::Semaphore>                              m_present_semaphores{};
        std::vector<vk::Fence>                                  m_in_flight_fences{};
        std::vector<vk::ImageLayout>                            m_swapchain_images_layout{};
        vk::ImageLayout                                         m_output_image_layout = vk::ImageLayout::eUndefined;
        std::array<vk::CommandBuffer, MAX_CONCURRENT_FRAMES>    m_rt_render_cmd;
        vr::device*                                             m_vr_dev = nullptr;
        vr::allocated_image                                     m_output_image_buffer;
        vr::accessible_image                                    m_output_image;
        vr::allocated_buffer                                    m_uniform_buffer = {};
        vr::allocated_buffer                                    m_vertex_buffer;
        vr::allocated_buffer                                    m_index_buffer;
        vr::blas_handle                                         m_blas_handle;
        vr::tlas_handle                                         m_tlas_handle;
        std::vector<vr::descriptor_item>                        m_resource_bindings;
        vk::DescriptorSetLayout                                 m_resource_descriptor_layout;
        vr::descriptor_buffer                                   m_resource_desc_buffer;
        vk::PipelineLayout                                      m_pipeline_layout = nullptr;
        utils::shader_compiler                                  m_shader_compiler{};
        vk::Pipeline                                            m_rt_pipeline = nullptr;
        vr::sbt_buffer                                          m_sbt_buffer; // contains the shader records for the SBT

        // ImGui resources
        vk::DescriptorPool                                      m_imgui_descriptor_pool = nullptr;
        vk::RenderPass                                          m_imgui_render_pass = nullptr;
        std::vector<vk::Framebuffer>                            m_imgui_framebuffers;
        ImTextureID                                             m_output_image_texture_id{};
        bool                                                    m_imgui_initialized = false;
        ImGuiContext*                                           m_imgui_context = nullptr;
        glm::ivec2                                              m_target_framebuffer_size{};

        GLT::platform::backend_api                              mp_window_backend_api{};
        ref<GLT::platform::i_window_plugin>                     mp_window{};

        handle                                                  m_framebuffer_resize_sub{};
        glm::vec4                                               m_clear_color{};
        // ref<GLT::camera>                                        m_camera{};

    };

}

#include "renderer.inl"

EXPORT_PLUGIN_CLASS(GLT::renderer_vk_ray::renderer, GLT::renderer_vk_ray::descriptor)

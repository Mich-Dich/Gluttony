
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <vk_ray/vk_ray.h>
#include <vk_ray/builders/builders.h>

#include <plugin_system/i_renderer_plugin.h>
#include <plugin_system/i_window_plugin.h>
#include <world/object/camera.h>

#include "util/utils.h"
#include "util/data_structures.h"
#include "util/shader_compiler.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    class image;
}


namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    constexpr u32                                               MAX_CONCURRENT_FRAMES = 3;

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    class renderer : public GLT::render::i_renderer_plugin {
    public:

        DEFAULT_GETTER(vr::device*,                             vr_dev);
        GETTER(vk::Device, vk_device,                           m_vr_dev->get_device())

        void on_load();


        void on_unload();


        bool create() override;


        void destroy() override;

        // --- frame control -------------------------------------------------------------------------------------------

        void begin_frame() override;


        void draw_frame() override;


        FORCE_INLINE void wait_for_gpu() override { m_device.waitIdle(); }

        // --- swapchain & configuration -------------------------------------------------------------------------------

        FORCE_INLINE_R glm::ivec2 get_swapchain_size() const override;
        
        
        void resize(const u32 width, const u32 height) override;

        IGNORE_UNUSED_PARAMETER_START
        IGNORE_UNUSED_VARIABLE_START

        void set_vsync(const bool enabled) override { }

        IGNORE_UNUSED_VARIABLE_STOP
        IGNORE_UNUSED_PARAMETER_STOP

        FORCE_INLINE_R bool get_vsync() const override { return false; }


        FORCE_INLINE void set_clear_color(const glm::vec4& color) override { m_clear_color = color; }


        void set_render_size(const glm::ivec2& size) override;

        // --- feature queries -----------------------------------------------------------------------------------------

        FORCE_INLINE_R GLT::render::renderer_feature get_supported_features() const override { return m_features; }


        FORCE_INLINE_R GLT::render::backend_api get_backend_api() const override { return GLT::render::backend_api::vulkan; }

        // --- native access -------------------------------------------------------------------------------------------

        [[nodiscard]] void* get_rendered_image() override;


        [[nodiscard]] glm::uvec2 get_rendered_image_size() override;


        FORCE_INLINE_R void* get_native_device_handle() const override { return {m_physical_device}; }


        FORCE_INLINE_R void* get_native_context_handle() const override { return {}; }


        void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function);

    private:

        enum class image_type{
            swapchain = 0,
            render,
        };


        void init_vulkan();


        void create_acceleration_structures();


        void create_rt_pipeline();


        void update_descriptor_set();


        void create_base_resources();


        void transition_image_layout(vk::CommandBuffer command_buffer, const image_type type, const vk::ImageLayout new_layout);


        void create_swapchain(const glm::ivec2 size);


        void destroy_swapchain();


        void resize_swapchain(const glm::ivec2 size);


        // Clear the output image to a background colour (e.g., dark blue)
        void clear_output_image(vk::CommandBuffer cmd, const glm::vec4& color);


        vk::DescriptorSet create_imgui_texture(vr::accessible_image& img);

        // --- IMGUI ---------------------------------------------------------------------------------------------------

        void imgui_init();


        void imgui_shutdown();


        void create_imgui_resources();


        void destroy_imgui_resources();


        void begin_imgui_frame(vk::CommandBuffer& current_cmd);


        void end_imgui_frame(vk::CommandBuffer& current_cmd);


        glm::ivec2                                              m_render_size{ 300, 400};

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
        std::array<vk::CommandBuffer, MAX_CONCURRENT_FRAMES>    m_rt_render_cmd;
        vr::device*                                             m_vr_dev = nullptr;
        GLT::ref<image>                                         m_output_image = nullptr;
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
        // vk::Sampler											    m_default_sampler_linear{};
        // vk::Sampler											    m_default_sampler_nearest{};

        // ImGui resources
        vk::DescriptorPool                                      m_imgui_descriptor_pool = nullptr;
        vk::RenderPass                                          m_imgui_render_pass = nullptr;
        std::vector<vk::Framebuffer>                            m_imgui_framebuffers;
        bool                                                    m_imgui_initialized = false;
        ImGuiContext*                                           m_imgui_context = nullptr;
        glm::ivec2                                              m_target_framebuffer_size{};

        GLT::platform::backend_api                              mp_window_backend_api{};
        ref<GLT::platform::i_window_plugin>                     mp_window{};

        handle                                                  m_framebuffer_resize_sub{};
        glm::vec4                                               m_clear_color{0.09f, 0.09f, 0.09f, 1.f};
        ref<GLT::world::camera>                                 m_active_camera{};

        // ---------------------------- immediate-submit ---------------------------- 
		vk::Fence										        m_immediate_submit_fence{};
		vk::CommandBuffer								        m_immediate_submit_command_buffer{};
		vk::CommandPool								            m_immediate_submit_command_pool{};
    
    };


    class image : public GLT::render::image {
    public:

        image();

        image(const glm::uvec3 size);

        image(const std::filesystem::path& image_path, const bool mipmapped = false);

        image(const void* data, const u32 width, const u32 height, const bool mipmapped = false);

        ~image();


        SETTER(VmaAllocation, allocation,                       m_allocated_image.allocation);

        DEFAULT_GETTER_REF(vr::allocated_image,                 allocated_image);
        DEFAULT_SETTER(vr::allocated_image,                     allocated_image);
        DEFAULT_GETTER_REF(vr::accessible_image,                accessible_image);
        DEFAULT_SETTER(vr::accessible_image,                    accessible_image);

        FORCE_INLINE_R glm::uvec2 get_size() override;


        FORCE_INLINE_R void* get_descriptor_set() override;


        [[nodiscard]] void* load(const std::filesystem::path& path, u32& out_width, u32& out_height) override;


        void resize(const glm::uvec3& new_size, const GLT::render::image_format format = GLT::render::image_format::RGBA,
            const bool mipmapped = false) override;

    private:

	    void allocate_memory(const void* data, const glm::uvec3 size, 
            const GLT::render::image_format format = GLT::render::image_format::RGBA, const bool mipmapped = true);


        void assign_data(const void* data, const glm::uvec3 size, const GLT::render::image_format format, const u32 mip_levels);


        void release();


        vr::allocated_image                                     m_allocated_image{};
        vr::accessible_image                                    m_accessible_image{};
        GLT::ref<GLT::renderer_vk_ray::renderer>                m_renderer{};

    };

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };

    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {

        GLT::plugin_manager::interface::window,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .load_phase                                             = GLT::plugin_manager::phase::post_window,
        .unload_phase                                           = GLT::plugin_manager::phase::post_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::renderer,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

#include "image.inl"
#include "plugin.inl"
#include "renderer.inl"

EXPORT_PLUGIN_CLASS(GLT::renderer_vk_ray::renderer, GLT::renderer_vk_ray::descriptor)

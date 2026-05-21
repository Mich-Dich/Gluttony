
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

        void init_vulkan();
        void create_base_resources();
        void create_acceleration_structures();
        void create_rt_pipeline();
        void update_descriptor_set();
        void imgui_init();
        void imgui_shutdown();
        void create_imgui_resources();
        void destroy_imgui_resources();


        GLT::system_state                                       m_state = GLT::system_state::shutdown;
        vr::instance_wrapper                                    m_instance;
        vr::command_queues                                      m_queues{};
        vk::Device                                              m_device = nullptr;
        vk::SurfaceKHR                                          m_surface = nullptr;
        vk::PhysicalDevice                                      m_physical_device = nullptr;
        utils::deletion_queue                                   m_deletion_queue{};
        vr::swapchain_builder                                   m_swapchain_builder;
        vr::swapchain_resources                                 m_swapchain_resources;
        vk::CommandPool                                         m_graphics_pool;
        u32                                                     m_image_count = 0;
        std::vector<vk::Semaphore>                              m_render_semaphores{};
        std::vector<vk::Semaphore>                              m_present_semaphores{};
        std::vector<vk::Fence>                                  m_in_flight_fences{};
        std::vector<vk::ImageLayout>                            m_swapchain_images_layout{};
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

        // ref<GLT::camera>                                        m_camera{};

    };

}

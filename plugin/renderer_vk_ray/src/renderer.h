
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

        // deletion_queue                                  m_deletion_queue{};
        // ref<GLT::camera>                                m_camera{};

    };

}

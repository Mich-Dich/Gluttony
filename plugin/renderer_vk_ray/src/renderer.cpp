
#include <util/pch.h>
#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_window_plugin.h>

#include <vk_ray/vk_ray.h>

#include "util/utils.h"

#include "renderer.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    #if defined(DEBUG)

        constexpr bool                  USE_VULKAN_VALIDATION = true;

    #else

        constexpr bool                  USE_VULKAN_VALIDATION = false;

    #endif

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    renderer::renderer() {

    }


    renderer::~renderer() {

    }
    
    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void renderer::init_vulkan() {

        util::stopwatch stopwatch(time_unit::milliseconds);

        LOG(trace, "Renderer configuration:");
        LOG(trace, "  - Vulkan validation: [{}]", (USE_VULKAN_VALIDATION ? "ENABLED" : "DISABLED"));

        auto p_window = GLT::plugin_manager::get_plugin<GLT::platform::i_window_plugin>(GLT::plugin_manager::targeted_interface::window);
        ASSERT(!p_window.expired(), "", "Failed to get window plugin")
        u32 count;
        const char** extensions = p_window.lock()->get_required_render_extensions(&count);

        vr::vulkan_builder builder;
        builder.enable_debug = USE_VULKAN_VALIDATION;
        builder.debug_callback = utils::vulkan_debug_callback;
        builder.physical_device_features10.samplerAnisotropy = true;

        for (u32 i = 0; i < count; i++)                                      // Add the extensions to the builder
            builder.instance_extensions.push_back(extensions[i]);

        m_instance = builder.create_instance();                             // Create the instance
        m_surface = utils::create_surface(m_instance.instance_handle);
        m_physical_device = builder.pick_physical_device(m_surface);        // Pick the physical device to use
        m_device = builder.create_device();                                 // Create the logical device
        m_queues = builder.get_queues();                                    // Get the queues for the logical device
        ASSERT(m_queues.graphics_queue, "", "Failed to select the graphics queue")

        m_deletion_queue.setup(m_device);

        // create a swapchain
        m_swapchain_builder = vr::swapchain_builder(m_device, m_physical_device, m_surface, m_queues.graphics_index, m_queues.present_index);
        m_swapchain_builder.height = static_cast<u32>(400);
        m_swapchain_builder.width = static_cast<u32>(600);
        m_swapchain_builder.back_buffer_count = 2;
        m_swapchain_builder.image_usage = vk::ImageUsageFlagBits::eTransferDst;
        m_swapchain_builder.desired_format = vk::Format::eB8G8R8A8Unorm;
        LOG(trace, "Creating swapchain with dimensions: {}x{}", m_swapchain_builder.width, m_swapchain_builder.height);

        try {

            m_swapchain_resources = m_swapchain_builder.build_swapchain();

        } catch (const std::exception &e) {

            LOG(fatal, "Failed to create initial swapchain: {}", e.what());
            throw;
        }

        // Create semaphores and fences
        vk::SemaphoreCreateInfo semaphore_info = {};
        vk::FenceCreateInfo fence_info = {};
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

        // Resize vectors to match number of swapchain images
        m_image_count = static_cast<u32>(m_swapchain_resources.swapchain_images.size());
        m_render_semaphores.resize(m_image_count);
        m_present_semaphores.resize(m_image_count);
        m_in_flight_fences.resize(m_image_count);
        m_swapchain_images_layout.resize(m_image_count);
        for (u32 x = 0; x < m_image_count; x++) {
            m_render_semaphores[x] = m_device.createSemaphore(semaphore_info);
            m_present_semaphores[x] = m_device.createSemaphore(semaphore_info);
            m_in_flight_fences[x] = m_device.createFence(fence_info);
            m_swapchain_images_layout[x] = vk::ImageLayout::eUndefined;
        }

        // Create command pools
        vk::CommandPoolCreateInfo pool_info = {};
        pool_info.queueFamilyIndex = m_queues.graphics_index;
        pool_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_graphics_pool = m_device.createCommandPool(pool_info);

        // create command buffers
        vk::CommandBufferAllocateInfo alloc_info = {};
        alloc_info.commandPool = m_graphics_pool;
        alloc_info.level = vk::CommandBufferLevel::ePrimary;
        alloc_info.commandBufferCount = m_image_count;

        auto allocated_cmd_buffers = m_device.allocateCommandBuffers(alloc_info);
        for (u32 x = 0; x < m_image_count; x++)
            m_rt_render_cmd[x] = allocated_cmd_buffers[x];

        m_vr_dev = new vr::device(m_instance.instance_handle, m_device, m_physical_device);

        m_deletion_queue.push_func([&]() {

            // Destroy semaphores
            for (auto& sem : m_render_semaphores)
                if (sem) m_device.destroySemaphore(sem);

            for (auto& sem : m_present_semaphores)
                if (sem) m_device.destroySemaphore(sem);

            m_render_semaphores.clear();
            m_present_semaphores.clear();

            // Destroy fences
            for (u32 i = 0; i < m_image_count; i++)
                if (m_in_flight_fences[i]) m_device.destroyFence(m_in_flight_fences[i]);

            if (m_graphics_pool)
                m_device.destroyCommandPool(m_graphics_pool);

            if (m_vr_dev)
                delete m_vr_dev;
        });

        const f32 duration = stopwatch.stop();
        LOG(info, "duration [{}] ms", duration)
    }

}

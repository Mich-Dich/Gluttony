
#include <util/pch.h>

#include <event/event_bus.h>
#include <event/application_event.h>

#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_window_plugin.h>
#include <plugin_system/i_renderer_plugin.h>

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    #if defined(DEBUG)
        constexpr bool                  USE_VULKAN_VALIDATION = true;
    #else
        constexpr bool                  USE_VULKAN_VALIDATION = false;
    #endif

    // MACROS ==========================================================================================================

    #if defined(DEBUG)
        #define VALIDATE_INIT                               if (!m_imgui_initialized) { return; }
        #define VK_CHECK_S(expr)		                    ASSERT_S(expr == vk::Result::eSuccess)
        #define VK_CHECK(expr, successMsg, failureMsg)		ASSERT(expr == vk::Result::eSuccess)
    #else
        #define VALIDATE_INIT
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================
    
    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    bool renderer::create() {

        mp_window = GLT::plugin_manager::get_plugin_ref<GLT::platform::i_window_plugin>(GLT::plugin_manager::interface::window);
        ASSERT(mp_window, "", "Failed to get window plugin")

        init_vulkan();
        create_base_resources();
        imgui_init();

        m_framebuffer_resize_sub = GLT::event_bus::subscribe<GLT::window_framebuffer_resize_event>(
            [this](const GLT::window_framebuffer_resize_event& event) { 
                m_target_framebuffer_size = glm::ivec2{event.get_width(), event.get_height()};
            }
        );

        m_state = system_state::idle;
        LOG_INIT
        return true;
    }


    void renderer::destroy() {

        m_state = system_state::destroyed;
        m_device.waitIdle();

        GLT::event_bus::unsubscribe(m_framebuffer_resize_sub);

        // Wait for all frames to complete
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
            if (m_in_flight_fences[i])
                VK_CHECK_S(m_device.waitForFences(m_in_flight_fences[i], VK_TRUE, UINT64_MAX));
        }

        imgui_shutdown();

        // Destroy swapchain resources
        if (m_swapchain.swapchain_handle) {
            for (auto& view : m_swapchain.swapchain_image_views)
                m_device.destroyImageView(view);
            m_device.destroySwapchainKHR(m_swapchain.swapchain_handle);
        }
        if (m_old_swapchain)
            m_device.destroySwapchainKHR(m_old_swapchain);

        m_deletion_queue.shutdown();

        if (m_surface) {
            m_instance.instance_handle.destroySurfaceKHR(m_surface);
            m_surface = nullptr;
        }

        mp_window.reset();
        LOG_SHUTDOWN
    }
    
    // CLASS PUBLIC ====================================================================================================

    void renderer::begin_frame() {

        m_state = system_state::running;
        // Wait for the in-flight fence for this frame
        VK_CHECK_S(m_device.waitForFences(m_in_flight_fences[m_current_frame], VK_TRUE, UINT64_MAX));
        m_device.resetFences(m_in_flight_fences[m_current_frame]);

        try {

            auto acquire_result = m_device.acquireNextImageKHR(m_swapchain.swapchain_handle, UINT64_MAX,
                m_present_semaphores[m_current_frame], nullptr);
            m_current_swapchain_image = acquire_result.value;

        } catch (const vk::OutOfDateKHRError&) {

            resize_swapchain(m_target_framebuffer_size);
            return;   // skip the rest of begin_frame this frame
        }



        // Reset command buffer and begin recording
        m_rt_render_cmd[m_current_frame].reset();
        vk::CommandBufferBeginInfo begin_info{};
        begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        m_rt_render_cmd[m_current_frame].begin(begin_info);

        begin_imgui_frame();
        
        draw_frame();
    }


    void renderer::draw_frame() {

        VALIDATE_INIT

        ImGui::Render();                                                                // Finalize ImGui draw data
        vk::CommandBuffer& cmd = m_rt_render_cmd[m_current_frame];                      // Record ImGui rendering into the command buffer
        
        vk::RenderPassBeginInfo rp_info{};                                              // Begin render pass (clears background to dark blue)
        rp_info.renderPass = m_imgui_render_pass;
        rp_info.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
        rp_info.renderArea.offset = vk::Offset2D{};
        rp_info.renderArea.extent = m_swapchain.swapchain_extent;

        std::array<vk::ClearValue, 1> clear_values{};                                   // Clear colour (dark blue)
        clear_values[0].color = {0.1f, 0.1f, 0.2f, 1.0f};
        rp_info.clearValueCount = static_cast<u32>(clear_values.size());
        rp_info.pClearValues = clear_values.data();

        try {
            cmd.beginRenderPass(rp_info, vk::SubpassContents::eInline);
        } catch(...) {
            LOG(error, "Failed to begin render pass")
            return;
        }

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
        cmd.endRenderPass();

        // After the render pass, the image layout is PRESENT_SRC_KHR (set in render pass)
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;

        cmd.end();                                                                      // End command buffer

        vk::SubmitInfo submit_info{};                                                   // Submit to graphics queue
        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &m_present_semaphores[m_current_frame];
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &m_render_semaphores[m_current_frame];
        m_queues.graphics_queue.submit(submit_info, m_in_flight_fences[m_current_frame]);

        vk::PresentInfoKHR present_info{};                                              // Present
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &m_render_semaphores[m_current_frame];
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &m_swapchain.swapchain_handle;
        present_info.pImageIndices = &m_current_swapchain_image;

        try {
            m_queues.present_queue.presentKHR(present_info);
        } catch (const vk::OutOfDateKHRError&) {
            resize_swapchain(m_target_framebuffer_size);
        }

        // Advance to next frame
        m_current_frame = (m_current_frame + 1) % MAX_CONCURRENT_FRAMES;
        m_state = system_state::idle;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void renderer::init_vulkan() {

        LOG(trace, "Renderer configuration:");
        LOG(trace, "  - Vulkan validation: [{}]", (USE_VULKAN_VALIDATION ? "ENABLED" : "DISABLED"));

        u32 count;
        const char** extensions = mp_window->get_required_render_extensions(&count);

        vr::vulkan_builder builder;
        builder.enable_debug = USE_VULKAN_VALIDATION;
        builder.debug_callback = utils::vulkan_debug_callback;
        builder.physical_device_features10.samplerAnisotropy = true;

        for (u32 i = 0; i < count; i++)                                      // Add the extensions to the builder
            builder.instance_extensions.push_back(extensions[i]);

        m_instance = builder.create_instance();                             // Create the instance
        m_surface = mp_window->create_vulkan_surface(m_instance.instance_handle);
        m_physical_device = builder.pick_physical_device(m_surface);        // Pick the physical device to use
        m_device = builder.create_device();                                 // Create the logical device
        m_queues = builder.get_queues();                                    // Get the queues for the logical device
        ASSERT(m_queues.graphics_queue, "", "Failed to select the graphics queue")

        m_deletion_queue.setup(m_device);

        // Create semaphores and fences
        vk::SemaphoreCreateInfo semaphore_info = {};
        vk::FenceCreateInfo fence_info = {};
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

        glm::ivec2 framebuffer_size = mp_window->get_framebuffer_size();
        create_swapchain(framebuffer_size);

        // Resize vectors to match number of swapchain images
        m_image_count = static_cast<u32>(m_swapchain.swapchain_images.size());
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
    }

    // ----- SWAPCHAIN -------------------------------------------------------------------------------------------------

	void renderer::create_swapchain(const glm::ivec2 size) {

        // create a swapchain
        m_swapchain_builder = vr::swapchain_builder(m_device, m_physical_device, m_surface, m_queues.graphics_index, m_queues.present_index);
        m_swapchain_builder.width = static_cast<u32>(size.x);
        m_swapchain_builder.height = static_cast<u32>(size.y);
        m_swapchain_builder.back_buffer_count = 2;
        m_swapchain_builder.image_usage = vk::ImageUsageFlagBits::eTransferDst;
        m_swapchain_builder.desired_format = vk::Format::eB8G8R8A8Unorm;
        m_swapchain_builder.present_mode = mp_window->get_vsync() ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eMailbox;
        LOG(trace, "Creating swapchain with dimensions: {}x{}", m_swapchain_builder.width, m_swapchain_builder.height);

        try {

            m_swapchain = m_swapchain_builder.build_swapchain();

        } catch (const std::exception &exception) {

            LOG(fatal, "Failed to create initial swapchain: [{}]", exception.what());
            throw;
        }
	}


	void renderer::destroy_swapchain() {

        m_swapchain_builder.destroy_swapchain(m_device, m_swapchain);
	}


	void renderer::resize_swapchain(const glm::ivec2 size) {

        VALIDATE(m_target_framebuffer_size.x > 0 && m_target_framebuffer_size.y > 0,
            return , "", "Cant resize if one dimension is to small");

		vkDeviceWaitIdle(m_device);
		destroy_swapchain();
		create_swapchain(size);

        // re‑initialise layout tracking
        m_image_count = static_cast<u32>(m_swapchain.swapchain_images.size());
        m_swapchain_images_layout.assign(m_image_count, vk::ImageLayout::eUndefined);
        
        // override imgui framebuffers
        m_imgui_framebuffers.resize(m_swapchain.swapchain_images.size());
        for (size_t x = 0; x < m_swapchain.swapchain_images.size(); x++) {

            vk::ImageView attachments[] = { m_swapchain.swapchain_image_views[x] };
            vk::FramebufferCreateInfo fb_info = {};
            fb_info.renderPass = m_imgui_render_pass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = attachments;
            fb_info.width = m_swapchain.swapchain_extent.width;
            fb_info.height = m_swapchain.swapchain_extent.height;
            fb_info.layers = 1;

            try {
                m_imgui_framebuffers[x] = m_device.createFramebuffer(fb_info);
            } catch (const vk::SystemError& e) {
                LOG(error, "Failed to create ImGui framebuffer: [{}]", e.what());
                throw;
            }
        }
	}


    void renderer::create_base_resources() {

        // Create an image to render to
        auto image_create_info = vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setExtent(vk::Extent3D(m_swapchain.swapchain_extent, 1))
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        // create the image with dedicated memory
        m_output_image_buffer = m_vr_dev->create_image(image_create_info, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        // create a view for the image
        auto view_create_info = vk::ImageViewCreateInfo()
            .setImage(m_output_image_buffer.image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk::Format::eR16G16B16A16Sfloat)
            .setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));

        m_output_image.view = m_device.createImageView(view_create_info);

        // create a uniform buffer
        u32 uniform_buffer_size = sizeof(f32) * 4 * 4 * 2; // two 4x4 matrix
        uniform_buffer_size += sizeof(f32) * 4;            // pass time, and 3 floats for padding or whatever else in the future

        // we will be writing to this buffer on the CPU
        m_uniform_buffer = m_vr_dev->create_buffer(uniform_buffer_size, vk::BufferUsageFlagBits::eUniformBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        m_deletion_queue.push_func([&]() {

            if (m_output_image.view)                                    // Destroy image view
                m_device.destroyImageView(m_output_image.view);

            if (m_output_image_buffer.image)                            // destroy the image
                m_vr_dev->destroy_image(m_output_image_buffer);

            m_output_image.view = nullptr;
            m_output_image_buffer.image = nullptr;

            if (m_uniform_buffer.buffer)
                m_vr_dev->destroy_buffer(m_uniform_buffer);
        });
    }


    void renderer::transition_image_layout(vk::CommandBuffer command_buffer, const image_type type, 
        const vk::ImageLayout new_layout) {

        switch (type) {
            case image_type::swapchain: {
                // Define the image subresource range for swapchain images
                vk::ImageSubresourceRange swapchain_range(
                    vk::ImageAspectFlagBits::eColor,            // Color aspect
                    0,                                          // Base mip level
                    1,                                          // Level count
                    0,                                          // Base array layer
                    1                                           // Layer count
                );

                m_vr_dev->transition_image_layout(
                    command_buffer,
                    m_swapchain.swapchain_images[m_current_swapchain_image],
                    m_swapchain_images_layout[m_current_swapchain_image],
                    new_layout,
                    swapchain_range,                            // Added: Image subresource range
                    vk::PipelineStageFlagBits::eAllGraphics,    // Added: Source stage (using default)
                    vk::PipelineStageFlagBits::eAllCommands     // Added: Destination stage (using default)
                );
                m_swapchain_images_layout[m_current_swapchain_image] = new_layout;

            } break;
            case image_type::render: {
                // Define the image subresource range for render images
                vk::ImageSubresourceRange render_range(
                    vk::ImageAspectFlagBits::eColor,            // Color aspect
                    0,                                          // Base mip level
                    1,                                          // Level count
                    0,                                          // Base array layer
                    1                                           // Layer count
                );

                m_vr_dev->transition_image_layout(
                    command_buffer,
                    m_output_image_buffer.image,
                    m_output_image_layout,
                    new_layout,
                    render_range,                               // Added: Image subresource range
                    vk::PipelineStageFlagBits::eAllGraphics,    // Added: Source stage
                    vk::PipelineStageFlagBits::eAllCommands     // Added: Destination stage
                );
                m_output_image_layout = new_layout;

            } break;
        }
    }

    // ----- IMGUI -----------------------------------------------------------------------------------------------------

    void renderer::imgui_init() {

        // Setup ImGui context
        IMGUI_CHECKVERSION();
        m_imgui_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_imgui_context);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

        ImGui::StyleColorsDark();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
        ImGuiStyle& style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        mp_window->imgui_init(GLT::render::backend_api::vulkan);

        ASSERT(ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4,
            [](const char* function_name, void* user_data) -> PFN_vkVoidFunction {

                vk::Instance* instance = static_cast<vk::Instance*>(user_data);
                return vkGetInstanceProcAddr(static_cast<VkInstance>(*instance), function_name);
            },
            &m_instance.instance_handle
        ), "", "Failed to load Vulkan functions for ImGui");

        create_imgui_resources();
        LOG(trace, "ImGui initialized");
    }


    void renderer::imgui_shutdown() {

        mp_window->imgui_shutdown();
        destroy_imgui_resources();
        ImGui::DestroyContext();
        LOG(trace, "ImGui shutdown");
    }


    void renderer::begin_imgui_frame() {

        VALIDATE_INIT

        // Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL for ImGui
        transition_image_layout(
            m_rt_render_cmd[m_current_frame],
            image_type::swapchain,
            vk::ImageLayout::eColorAttachmentOptimal
        );

        // Start ImGui frame (no command buffer needed)
        ImGui_ImplVulkan_NewFrame();
        mp_window->begin_imgui_frame();
        ImGui::NewFrame();

        // Example UI
        static bool show_demo = true;
        if (show_demo)
            ImGui::ShowDemoWindow(&show_demo);
    }


    void renderer::end_imgui_frame() {

        VALIDATE_INIT

        ImGui::Render();

        // Begin render pass
        vk::RenderPassBeginInfo rp_info = {};
        rp_info.renderPass = m_imgui_render_pass;
        rp_info.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
        rp_info.renderArea.offset = vk::Offset2D(0, 0);
        rp_info.renderArea.extent = m_swapchain.swapchain_extent;

        m_rt_render_cmd[m_current_frame].beginRenderPass(rp_info, vk::SubpassContents::eInline);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_rt_render_cmd[m_current_frame]);
        m_rt_render_cmd[m_current_frame].endRenderPass();

        // After the render pass, the image is in PRESENT_SRC_KHR (due to finalLayout), we need to update our tracked layout
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;

        // Update and Render additional Platform Windows
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

}

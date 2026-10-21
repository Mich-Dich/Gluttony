
#include <util/pch.h>
#include <imgui.h>

#include <event/event_bus.h>
#include <event/application_event.h>
#include <platform/i_window.h>
#include <asset/material.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    #if defined(DEBUG)
        constexpr bool                                      USE_VULKAN_VALIDATION = true;
    #else
        constexpr bool                                      USE_VULKAN_VALIDATION = false;
    #endif

    // MACROS ==========================================================================================================

    #if defined(DEBUG)
        #define VK_CHECK_S(expr)		                    ASSERT_S(expr == vk::Result::eSuccess)
        #define VK_CHECK(expr, successMsg, failureMsg)		ASSERT(expr == vk::Result::eSuccess)
    #else
        #define VK_CHECK_S(expr)
        #define VK_CHECK(expr, successMsg, failureMsg)
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    bool renderer::create() {

        mp_window = GLT::plugin_manager::get_plugin_ref<GLT::platform::i_window_plugin>(
            GLT::plugin_manager::interface::window);
        ASSERT(mp_window, "", "Failed to get window plugin")

        m_active_camera.view = glm::lookAt(glm::vec3(0.f, 0.f, 3.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
        m_active_camera.position = glm::vec3(0.f, 0.f, 3.f);
        m_active_camera.fov = 45.f;

        init_vulkan();
        create_shader_compiler();
        m_graph = GLT::create_unique_ref<graph::render_graph>(m_dev);
        create_render_target();
        create_material_resources();
        create_mesh_resources();
        install_fallback_material();
        create_shadow_resources();
        create_local_shadow_resources();        // after create_shadow_resources(), needs m_mesh_set_layout + instance buffers
        create_light_resources();
        create_cluster_resources();
        create_light_culling_pipeline();
        create_debug_view_resources();
        create_debug_draw_resources();
        create_preview_resources();
        imgui_init();

        m_framebuffer_resize_sub = GLT::event_bus::subscribe<GLT::window_framebuffer_resize_event>(
            [this](const GLT::window_framebuffer_resize_event& event) {
                m_target_framebuffer_size = glm::ivec2{event.get_width(), event.get_height()};
            });

        LOG_INIT
        return true;
    }


    void renderer::destroy() {

        m_device.waitIdle();
        GLT::event_bus::unsubscribe(m_framebuffer_resize_sub);

        m_graph.reset();        // The graph owns VMA-allocated textures. It MUST be destroyed before m_deletion_queue.shutdown()

        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; ++i)
            if (m_in_flight_fences[i])
                VK_CHECK_S(m_device.waitForFences(m_in_flight_fences[i], VK_TRUE, UINT64_MAX))

        destroy_preview_resources();
        destroy_debug_draw_resources();
        destroy_debug_view_resources();
        destroy_light_culling_pipeline();
        destroy_cluster_resources();
        destroy_light_resources();
        destroy_local_shadow_resources();
        destroy_shadow_resources();
        destroy_mesh_resources();
        destroy_material_resources();
        destroy_render_target();
        imgui_shutdown();
        destroy_shader_compiler();

        if (m_swapchain.swapchain_handle) {
            for (auto& v : m_swapchain.swapchain_image_views)
                m_device.destroyImageView(v);
            m_device.destroySwapchainKHR(m_swapchain.swapchain_handle);
        }

        m_deletion_queue.shutdown();

        if (m_surface) {
            m_instance.instance_handle.destroySurfaceKHR(m_surface);
            m_surface = nullptr;
        }

        mp_window.reset();
        LOG_SHUTDOWN
    }

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // frame -----------------------------------------------------------------------------------------------------------

    void renderer::begin_frame() {

        VK_CHECK_S(m_device.waitForFences(m_in_flight_fences[m_current_frame], VK_TRUE, UINT64_MAX));
        m_device.resetFences(m_in_flight_fences[m_current_frame]);

        // GPU timing: read the timestamps written the last time this command buffer slot was submitted. The fence wait above
        // guarantees they have completed, so the results are available without further sync
        if (m_timestamp_pool) {

            std::array<u64, 2> timestamps{ 0, 0 };
            const vk::Result result = m_device.getQueryPoolResults(m_timestamp_pool, m_current_frame * 2, 2,
                sizeof(u64) * timestamps.size(), timestamps.data(), sizeof(u64), vk::QueryResultFlagBits::e64);

            if (result == vk::Result::eSuccess && timestamps[1] > timestamps[0]) {

                const f32 elapsed_ms = static_cast<f32>(timestamps[1] - timestamps[0]) * m_timestamp_period_ns * 1e-6f;
                m_gpu_frame_time_ms[m_current_frame] = elapsed_ms;
                m_last_gpu_time_ms = elapsed_ms;
            }
        }

        m_frame_draw_calls = 0;
        m_frame_render_passes = 0;

        try {

            auto r = m_device.acquireNextImageKHR(m_swapchain.swapchain_handle, UINT64_MAX, m_present_semaphores[m_current_frame], nullptr);
            m_current_swapchain_image = r.value;

        } catch (const vk::OutOfDateKHRError&) {
            resize_swapchain(m_target_framebuffer_size);
            return;
        }

        begin_imgui_frame();

        vk::CommandBuffer cmd = m_render_cmd[m_current_frame];
        cmd.reset();
        cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        if (m_timestamp_pool) {
            cmd.resetQueryPool(m_timestamp_pool, m_current_frame * 2, 2);
            cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, m_timestamp_pool, m_current_frame * 2);
        }

        m_graph->begin_frame();

        // Per-frame CPU-side updates ----------------------------------------------------------------------------------

        update_cluster_dimensions();
        process_pending_meshes();
        build_instance_buffer();
        update_lights();
        update_shadow();
        update_camera_ubo();
        update_globals();
        update_materials();
        process_debug_primitives();

        // Frame textures + descriptor refresh -------------------------------------------------------------------------

        import_frame_textures();
        update_light_meta_ubo();
        update_light_cull_descriptors();
        update_mesh_descriptors();

        // Passes ------------------------------------------------------------------------------------------------------
        add_reset_draw_count_pass();
        add_cull_pass();

        if (m_shadow.active) {
            add_reset_shadow_draw_count_pass();
            add_shadow_cull_pass();
            add_shadow_pass();
        }

        add_local_shadow_passes();      // no-ops when no local-shadow tiles exist
        add_depth_prepass_pass();
        add_clear_light_grid_pass();
        add_light_culling_pass();
        update_debug_view_descriptors();

        if (m_active_mode == modes::Light_cull.id)
            add_light_cull_debug_pass();
        else
            add_scene_pass();

        add_debug_draw_pass();
        add_preview_passes();
        add_imgui_pass();

        m_graph->compile();
    }


    void renderer::draw_frame() {

        end_imgui_frame();                          // Produce ImGui draw data for this frame, MUST run before m_graph->execute()

        vk::CommandBuffer cmd = m_render_cmd[m_current_frame];
        m_graph->execute(cmd);                      // The graph emits all its barriers and invokes every pass's execute lambda

        m_output_image->get_accessible_image_ref().layout = m_graph->texture_layout(m_scene_color); // Sync image layout to graph last use
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;     // imgui sets layout to ePresentSrcKHR
        sync_preview_images_layout();

        m_graph->end_frame();

        static u32 dup_counter = 250;
        if (dup_counter >= 300) {
            LOG(info, "{}", m_graph->dump())
            dup_counter = 0;
        }
        dup_counter++;

        if (m_timestamp_pool)
            cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, m_timestamp_pool, m_current_frame * 2 + 1);
        cmd.end();

        vk::SubmitInfo submit_info{};                                                   // Submit to graphics queue
        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &m_present_semaphores[m_current_frame];
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &m_render_semaphores[m_current_frame];
        try {

            m_queues.graphics_queue.submit(submit_info, m_in_flight_fences[m_current_frame]); 
        
        } catch (...) { LOG(error, "submit failed"); }

        vk::PresentInfoKHR pi{};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &m_render_semaphores[m_current_frame];
        pi.swapchainCount = 1;
        pi.pSwapchains = &m_swapchain.swapchain_handle;
        pi.pImageIndices = &m_current_swapchain_image;
        try { 

            (void)m_queues.present_queue.presentKHR(pi);
        
        } catch (const vk::OutOfDateKHRError&) { resize_swapchain(m_target_framebuffer_size); }

        m_current_frame = (m_current_frame + 1) % MAX_CONCURRENT_FRAMES;
    }


    void renderer::set_render_size(const glm::ivec2& size) {

        if (size.x <= 0 || size.y <= 0 || m_render_size == size)
            return;

        m_render_size = size;
        m_device.waitIdle();

        // rebuild render target + framebuffers for the new size
        destroy_render_target();
        create_render_target();
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::init_vulkan() {

        u32 count;
        const char** ext = mp_window->get_required_render_extensions(&count);

        util::vulkan_builder builder;
        builder.enable_debug = USE_VULKAN_VALIDATION;
        builder.debug_callback = util::vulkan_debug_callback;
        builder.physical_device_features10.samplerAnisotropy = true;
        builder.physical_device_features12.shaderSampledImageArrayNonUniformIndexing = true;
        for (u32 i = 0; i < count; i++)
            builder.instance_extensions.push_back(ext[i]);

        m_instance = builder.create_instance();
        m_surface = mp_window->create_vulkan_surface(m_instance.instance_handle);
        m_physical_device = builder.pick_physical_device(m_surface);
        m_device = builder.create_device();
        m_queues = builder.get_queues();
        ASSERT(m_queues.graphics_queue, "", "Failed to select graphics queue");

        m_deletion_queue.setup(m_device);

        glm::ivec2 fb = mp_window->get_framebuffer_size();
        create_swapchain(fb);

        m_render_semaphores.resize(MAX_CONCURRENT_FRAMES);
        m_present_semaphores.resize(MAX_CONCURRENT_FRAMES);
        m_in_flight_fences.resize(MAX_CONCURRENT_FRAMES);
        m_image_count = (u32)m_swapchain.swapchain_images.size();
        m_swapchain_images_layout.assign(m_image_count, vk::ImageLayout::eUndefined);

        vk::SemaphoreCreateInfo semaphore_ci{};
        vk::FenceCreateInfo fence_ci{};
        fence_ci.flags = vk::FenceCreateFlagBits::eSignaled;
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; ++i) {
            m_render_semaphores[i] = m_device.createSemaphore(semaphore_ci);
            m_present_semaphores[i] = m_device.createSemaphore(semaphore_ci);
            m_in_flight_fences[i] = m_device.createFence(fence_ci);
        }

        vk::CommandPoolCreateInfo command_pool_ci{};
        command_pool_ci.queueFamilyIndex = m_queues.graphics_index;
        command_pool_ci.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_graphics_pool = m_device.createCommandPool(command_pool_ci);

        // main render command buffers
        vk::CommandBufferAllocateInfo ai{};
        ai.commandPool = m_graphics_pool;
        ai.level = vk::CommandBufferLevel::ePrimary;
        ai.commandBufferCount = MAX_CONCURRENT_FRAMES;
        auto bufs = m_device.allocateCommandBuffers(ai);
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; i++) m_render_cmd[i] = bufs[i];

        // immediate submit pool
        m_immediate_submit_command_pool = m_device.createCommandPool(command_pool_ci);
        vk::CommandBufferAllocateInfo iai{};
        iai.commandPool = m_immediate_submit_command_pool;
        iai.level = vk::CommandBufferLevel::ePrimary;
        iai.commandBufferCount = 1;
        m_immediate_submit_command_buffer = m_device.allocateCommandBuffers(iai)[0];
        m_immediate_submit_fence = m_device.createFence(fence_ci);

        m_dev = new util::device(m_instance.instance_handle, m_device, m_physical_device);

        // Timestamp queries: two per in-flight frame, so we can measure the GPU time of the submission that used each command buffer slot
        {
            const vk::PhysicalDeviceProperties properties = m_physical_device.getProperties();
            m_timestamp_period_ns = (properties.limits.timestampPeriod > 0.0f) ? properties.limits.timestampPeriod : 1.0f;

            vk::QueryPoolCreateInfo query_pool_info{};
            query_pool_info.queryType = vk::QueryType::eTimestamp;
            query_pool_info.queryCount = MAX_CONCURRENT_FRAMES * 2;

            m_timestamp_pool = m_device.createQueryPool(query_pool_info);
            ASSERT(m_timestamp_pool, "", "Failed to create timestamp query pool");
        }

        m_deletion_queue.push_func([&]() {

            if (m_timestamp_pool) {
                m_device.destroyQueryPool(m_timestamp_pool);
                m_timestamp_pool = nullptr;
            }

            for (auto semaphore : m_render_semaphores)
                if (semaphore)
                    m_device.destroySemaphore(semaphore);

            for (auto semaphore : m_present_semaphores)
                if (semaphore)
                    m_device.destroySemaphore(semaphore);

            for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; ++i)
                if (m_in_flight_fences[i]) m_device.destroyFence(m_in_flight_fences[i]);

            if (m_immediate_submit_fence)
                m_device.destroyFence(m_immediate_submit_fence);

            if (m_immediate_submit_command_pool)
                m_device.destroyCommandPool(m_immediate_submit_command_pool);

            if (m_graphics_pool)
                m_device.destroyCommandPool(m_graphics_pool);

            if (m_dev)
                delete m_dev;
        });
    }

    // render target ---------------------------------------------------------------------------------------------------

    void renderer::create_render_target() {

        m_output_image = GLT::create_ref<image>();
        m_output_image->resize({(u32)m_render_size.x, (u32)m_render_size.y, 1});
    }


    void renderer::destroy_render_target() { m_output_image.reset(); }

}

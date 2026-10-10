
#include <util/pch.h>
#include <imgui.h>

#include <event/event_bus.h>
#include <event/application_event.h>
#include <platform/i_window.h>



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
        create_render_target();
        create_mesh_resources();
        imgui_init();
        m_graph = GLT::create_unique_ref<graph::render_graph>(m_dev);

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

        destroy_mesh_resources();
        destroy_render_target();
        imgui_shutdown();

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
            const vk::Result result = m_device.getQueryPoolResults(m_timestamp_pool, m_current_frame * 2, 2, sizeof(u64) * timestamps.size(),
                timestamps.data(), sizeof(u64), vk::QueryResultFlagBits::e64);

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

        process_pending_meshes();               // pull meshes in/out at the frame boundary
        update_camera_ubo();                    // fill the current frame's camera UBO
        build_instance_buffer();

        // Backbuffer import -------------------------------------------------------------------------------------------
        const vk::ImageLayout bb_initial = m_swapchain_images_layout[m_current_swapchain_image];
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::eUndefined;

        using VKIU = vk::ImageUsageFlagBits;

        graph::texture_desc bb_desc{};
        bb_desc.name = "backbuffer";
        bb_desc.format = m_swapchain.swapchain_format;
        bb_desc.width = m_swapchain.swapchain_extent.width;
        bb_desc.height = m_swapchain.swapchain_extent.height;
        bb_desc.usage = VKIU::eColorAttachment | VKIU::eTransferDst;
        m_backbuffer_handle = m_graph->create_texture(bb_desc);
        m_graph->import_texture(m_backbuffer_handle,
            m_swapchain.swapchain_images[m_current_swapchain_image], m_swapchain.swapchain_image_views[m_current_swapchain_image],
            { bb_initial, {}, vk::PipelineStageFlagBits2::eNone });

        // Scene colour: import m_output_image -------------------------------------------------------------------------
        graph::texture_desc rt_desc{};
        rt_desc.name = "scene_color";
        rt_desc.format = vk::Format::eR8G8B8A8Unorm;
        rt_desc.width = m_render_size.x;
        rt_desc.height = m_render_size.y;
        rt_desc.usage = VKIU::eSampled | VKIU::eColorAttachment | VKIU::eTransferSrc | VKIU::eTransferDst;
        rt_desc.persistent = true;
        m_triangle_target = m_graph->create_texture(rt_desc);
        m_graph->import_texture(m_triangle_target,
            m_output_image->get_allocated_image_ref().image, m_output_image->get_accessible_image_ref().view,
            { m_output_image->get_accessible_image_ref().layout, {}, vk::PipelineStageFlagBits2::eNone });

        // Scene depth: graph-owned, same size as scene colour ---------------------------------------------------------
        graph::texture_desc depth_desc{};
        depth_desc.name = "scene_depth";
        depth_desc.format = vk::Format::eD32Sfloat;
        depth_desc.width = m_render_size.x;
        depth_desc.height = m_render_size.y;
        depth_desc.usage = VKIU::eDepthStencilAttachment | VKIU::eSampled;
        depth_desc.persistent = true;
        m_depth_target = m_graph->create_texture(depth_desc);

        // Pass: depth prepass -----------------------------------------------------------------------------------------
        m_graph->add_pass("depth_prepass",
            [&](graph::pass_builder& builder) {
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
            },
            [this](graph::pass_context& ctx) {

                if (m_instance_count == 0)
                    return;

                dispatch_culling(ctx.cmd);

                ctx.begin_rendering();
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_depth_pipeline);
                m_dev->bind_descriptor_buffer({ m_frame_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                draw_scene_indirect(ctx.cmd);
                ctx.end_rendering();

                m_frame_render_passes++;
            });

        // Pass: forward shading ---------------------------------------------------------------------------------------
        m_graph->add_pass("scene",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_triangle_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32,4>{m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a}));
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
            },
            [this](graph::pass_context& ctx) {

                if (m_instance_count == 0)
                    return;

                ctx.begin_rendering();
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_mesh_pipeline);
                m_dev->bind_descriptor_buffer({ m_frame_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                draw_scene_indirect(ctx.cmd);
                ctx.end_rendering();

                m_frame_render_passes++;
            });

        // Pass: ImGui -> backbuffer -----------------------------------------------------------------------------------
        m_graph->add_pass("imgui",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_backbuffer_handle, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                builder.read(m_triangle_target, graph::texture_usage::sampled);       // ImGui editor samples scene_color
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                const ImDrawData* draw_data = ImGui::GetDrawData();
                m_frame_draw_calls += count_imgui_draw_calls(draw_data);
                m_frame_render_passes += 1;

                vk::RenderPassBeginInfo rp{};
                rp.renderPass = m_imgui_render_pass;
                rp.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
                rp.renderArea.extent = m_swapchain.swapchain_extent;

                std::array<vk::ClearValue, 1> clear_values{};
                clear_values[0].color = { m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a };
                rp.clearValueCount = (u32)clear_values.size();
                rp.pClearValues = clear_values.data();

                ctx.cmd.beginRenderPass(rp, vk::SubpassContents::eInline);
                ImGui_ImplVulkan_RenderDrawData(const_cast<ImDrawData*>(draw_data), ctx.cmd);
                ctx.cmd.endRenderPass();

                ImGuiIO& io = ImGui::GetIO();
                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                }
            });

        m_graph->compile();
    }


    void renderer::draw_frame() {

        end_imgui_frame();                          // Produce ImGui draw data for this frame, MUST run before m_graph->execute()

        vk::CommandBuffer cmd = m_render_cmd[m_current_frame];
        m_graph->execute(cmd);                      // The graph emits all its barriers and invokes every pass's execute lambda

        m_output_image->get_accessible_image_ref().layout = m_graph->texture_layout(m_triangle_target); // Sync image layout to graph last use
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;         // imgui sets layout to ePresentSrcKHR

        m_graph->end_frame();

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

            m_queues.graphics_queue.submit(submit_info, m_in_flight_fences[m_current_frame]); }

        catch (...) { LOG(error, "submit failed"); }

        vk::PresentInfoKHR pi{};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &m_render_semaphores[m_current_frame];
        pi.swapchainCount = 1;
        pi.pSwapchains = &m_swapchain.swapchain_handle;
        pi.pImageIndices = &m_current_swapchain_image;
        try { 

            (void)m_queues.present_queue.presentKHR(pi); }

        catch (const vk::OutOfDateKHRError&) { resize_swapchain(m_target_framebuffer_size); }

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

        util::vulkan_builder builder;                     // from util/builders.h
        builder.enable_debug = USE_VULKAN_VALIDATION;
        builder.debug_callback = util::vulkan_debug_callback;
        builder.physical_device_features10.samplerAnisotropy = true;
        for (u32 i = 0; i < count; i++) builder.instance_extensions.push_back(ext[i]);

        m_instance = builder.create_instance();
        m_surface = mp_window->create_vulkan_surface(m_instance.instance_handle);
        m_physical_device = builder.pick_physical_device(m_surface);
        m_device = builder.create_device();
        m_queues = builder.get_queues();
        ASSERT(m_queues.graphics_queue, "", "Failed to select graphics queue");

        m_deletion_queue.setup(m_device);

        glm::ivec2 fb = mp_window->get_framebuffer_size();
        create_swapchain(fb);

        m_image_count = (u32)m_swapchain.swapchain_images.size();
        m_render_semaphores.resize(m_image_count);
        m_present_semaphores.resize(m_image_count);
        m_in_flight_fences.resize(m_image_count);
        m_swapchain_images_layout.assign(m_image_count, vk::ImageLayout::eUndefined);

        vk::SemaphoreCreateInfo sci{};
        vk::FenceCreateInfo fci{};
        fci.flags = vk::FenceCreateFlagBits::eSignaled;
        for (u32 i = 0; i < m_image_count; i++) {
            m_render_semaphores[i]  = m_device.createSemaphore(sci);
            m_present_semaphores[i] = m_device.createSemaphore(sci);
            m_in_flight_fences[i]   = m_device.createFence(fci);
        }

        vk::CommandPoolCreateInfo pci{};
        pci.queueFamilyIndex = m_queues.graphics_index;
        pci.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_graphics_pool = m_device.createCommandPool(pci);

        // main render command buffers
        vk::CommandBufferAllocateInfo ai{};
        ai.commandPool = m_graphics_pool;
        ai.level = vk::CommandBufferLevel::ePrimary;
        ai.commandBufferCount = MAX_CONCURRENT_FRAMES;
        auto bufs = m_device.allocateCommandBuffers(ai);
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; i++) m_render_cmd[i] = bufs[i];

        // immediate submit pool
        m_immediate_submit_command_pool = m_device.createCommandPool(pci);
        vk::CommandBufferAllocateInfo iai{};
        iai.commandPool = m_immediate_submit_command_pool;
        iai.level = vk::CommandBufferLevel::ePrimary;
        iai.commandBufferCount = 1;
        m_immediate_submit_command_buffer = m_device.allocateCommandBuffers(iai)[0];
        m_immediate_submit_fence = m_device.createFence(fci);

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

            for (auto s : m_render_semaphores)
                if (s)
                    m_device.destroySemaphore(s);
            for (auto s : m_present_semaphores)
                if (s)
                    m_device.destroySemaphore(s);

            for (u32 i = 0; i < m_image_count; i++)
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

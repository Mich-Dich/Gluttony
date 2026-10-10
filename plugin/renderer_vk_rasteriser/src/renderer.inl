
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
        m_graph = GLT::create_unique_ref<graph::render_graph>(m_dev);
        create_render_target();
        create_mesh_resources();
        create_light_resources();
        create_cluster_resources();
        create_light_culling_pipeline();
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

        destroy_light_culling_pipeline();
        destroy_cluster_resources();
        destroy_light_resources();
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

        update_cluster_dimensions();        // rebuild the grid/index buffers if the render size changed
        process_pending_meshes();
        update_camera_ubo();
        build_instance_buffer();
        update_lights();

        using VKIU = vk::ImageUsageFlagBits;

        // Backbuffer --------------------------------------------------------------------------------------------------
        const vk::ImageLayout backbuffer_initial_layout = m_swapchain_images_layout[m_current_swapchain_image];
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::eUndefined;

        graph::texture_desc backbuffer_desc{};
        backbuffer_desc.name = "backbuffer";
        backbuffer_desc.format = m_swapchain.swapchain_format;
        backbuffer_desc.width = m_swapchain.swapchain_extent.width;
        backbuffer_desc.height = m_swapchain.swapchain_extent.height;
        backbuffer_desc.usage = VKIU::eColorAttachment | VKIU::eTransferDst;
        backbuffer_desc.persistent = true;

        m_backbuffer_handle = m_graph->create_texture(backbuffer_desc);
        m_graph->import_texture(m_backbuffer_handle,
            m_swapchain.swapchain_images[m_current_swapchain_image],
            m_swapchain.swapchain_image_views[m_current_swapchain_image],
            { backbuffer_initial_layout, {}, vk::PipelineStageFlagBits2::eNone });

        // Scene colour (external image, sampled by ImGui) -------------------------------------------------------------
        graph::texture_desc scene_color_desc{};
        scene_color_desc.name = "scene_color";
        scene_color_desc.format = vk::Format::eR8G8B8A8Unorm;
        scene_color_desc.width = m_render_size.x;
        scene_color_desc.height = m_render_size.y;
        scene_color_desc.usage = VKIU::eSampled | VKIU::eColorAttachment | VKIU::eTransferSrc | VKIU::eTransferDst;
        scene_color_desc.persistent = true;

        m_scene_color = m_graph->create_texture(scene_color_desc);
        m_graph->import_texture(m_scene_color,
            m_output_image->get_allocated_image_ref().image,
            m_output_image->get_accessible_image_ref().view,
            { m_output_image->get_accessible_image_ref().layout, {}, vk::PipelineStageFlagBits2::eNone });

        // -------- Scene depth --------
        graph::texture_desc depth_desc{};
        depth_desc.name = "scene_depth";
        depth_desc.format = vk::Format::eD32Sfloat;
        depth_desc.width = m_render_size.x;
        depth_desc.height = m_render_size.y;
        depth_desc.usage = VKIU::eDepthStencilAttachment | VKIU::eSampled;
        depth_desc.persistent = true;

        m_depth_target = m_graph->create_texture(depth_desc);

        // Refresh light culling inputs for this frame. All the resources the culling shader reads are either per-frame (frame slot)
        // or persistent-by-name; the descriptor refresh picks up any handles that were rebuilt by update_cluster_dimensions()
        update_light_meta_ubo();
        update_light_cull_descriptors();

        // -------- Reset the draw count to 0 (transfer write) --------
        m_graph->add_pass("reset_draw_count",
            [&](graph::pass_builder& builder) {
                builder.write(m_draw_count_handles[m_current_frame], graph::buffer_usage::transfer_dst);
                builder.set_side_effects(true);     // basically "GPU state initialization", needs to run regardless of downstream
            },
            [this](graph::pass_context& ctx) {
                ctx.cmd.fillBuffer(ctx.get_buffer(m_draw_count_handles[m_current_frame]), 0, sizeof(u32), 0);
            });

        // Cull (compute) ----------------------------------------------------------------------------------------------
        // Side effects so the culler keeps it even if nothing downstream reads its outputs.
        m_graph->add_pass("cull",
            [&](graph::pass_builder& builder) {
                builder.write(m_draw_commands_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_draw_count_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                if (m_instance_count == 0)
                    return;

                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_cull_pipeline);
                m_dev->bind_descriptor_buffer({ m_cull_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_cull_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);

                const u32 dispatch_count = (m_instance_count + 63) / 64;
                ctx.cmd.dispatch(dispatch_count, 1, 1);
            });

        // Depth prepass -----------------------------------------------------------------------------------------------
        m_graph->add_pass("depth_prepass",
            [&](graph::pass_builder& builder) {
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
            },
            [this](graph::pass_context& ctx) {

                if (m_instance_count == 0)
                    return;

                ctx.begin_rendering();
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_depth_pipeline);
                m_dev->bind_descriptor_buffer({ m_frame_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                draw_scene_indirect(ctx.cmd,
                    ctx.get_buffer(m_draw_commands_handles[m_current_frame]),
                    ctx.get_buffer(m_draw_count_handles[m_current_frame]));
                ctx.end_rendering();

                m_frame_render_passes++;
            });

        // Clear light grid --------------------------------------------------------------------------------------------
        // Runs before light culling. We clear the grid (small) and the global counter (4 bytes)
        // The index list is not cleared - culling only reads offsets it wrote this frame
        m_graph->add_pass("clear_light_grid",
            [&](graph::pass_builder& builder) {
                builder.write(m_light_grid_handles[m_current_frame], graph::buffer_usage::transfer_dst);
                builder.write(m_light_counter_handles[m_current_frame], graph::buffer_usage::transfer_dst);
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                if (m_cluster_params.cluster_count == 0)
                    return;

                const vk::DeviceSize grid_bytes = static_cast<vk::DeviceSize>(m_cluster_params.cluster_count) * sizeof(glm::uvec2);

                ctx.cmd.fillBuffer(ctx.get_buffer(m_light_grid_handles[m_current_frame]), 0, grid_bytes, 0);
                ctx.cmd.fillBuffer(ctx.get_buffer(m_light_counter_handles[m_current_frame]), 0, sizeof(u32), 0);
            });

        // Light culling -----------------------------------------------------------------------------------------------
        // Reads the light SSBO (Step 1) and writes the grid/index-list/counter (Step 2). Runs after clear_light_grid so
        // every cluster starts with an empty {0,0} slot, and after depth_prepass so a future iteration can tighten the
        // depth range per cluster from real depth
        m_graph->add_pass("light_culling",
            [&](graph::pass_builder& builder) {
                builder.write(m_light_grid_handles[m_current_frame],       graph::buffer_usage::storage_readwrite);
                builder.write(m_light_index_list_handles[m_current_frame], graph::buffer_usage::storage_readwrite);
                builder.write(m_light_counter_handles[m_current_frame],    graph::buffer_usage::storage_readwrite);
            },
            [this](graph::pass_context& ctx) {

                if (m_light_count == 0 || m_cluster_params.cluster_count == 0)
                    return;

                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_light_cull_pipeline);
                m_dev->bind_descriptor_buffer({ m_light_cull_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_light_cull_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eCompute);

                // One workgroup per cluster; the shader reads gl_WorkGroupID as (gx, gy, gz)
                ctx.cmd.dispatch(m_cluster_params.grid_x, m_cluster_params.grid_y, m_cluster_params.grid_z);
            });
        
        // Forward shading ---------------------------------------------------------------------------------------------
        m_graph->add_pass("scene",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_scene_color, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32,4>{m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a}));
                builder.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
                builder.read(m_draw_commands_handles[m_current_frame], graph::buffer_usage::indirect);
                builder.read(m_draw_count_handles[m_current_frame], graph::buffer_usage::indirect);
            },
            [this](graph::pass_context& ctx) {

                if (m_instance_count == 0)
                    return;

                ctx.begin_rendering();
                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_mesh_pipeline);
                m_dev->bind_descriptor_buffer({ m_frame_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);
                draw_scene_indirect(ctx.cmd,
                    ctx.get_buffer(m_draw_commands_handles[m_current_frame]),
                    ctx.get_buffer(m_draw_count_handles[m_current_frame]));
                ctx.end_rendering();

                m_frame_render_passes++;
            });

        // Pass: ImGui -------------------------------------------------------------------------------------------------
        m_graph->add_pass("imgui",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_backbuffer_handle, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                builder.read(m_scene_color, graph::texture_usage::sampled);
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                const ImDrawData* draw_data = ImGui::GetDrawData();
                m_frame_draw_calls += count_imgui_draw_calls(draw_data);
                m_frame_render_passes += 1;

                vk::RenderPassBeginInfo render_pass_info{};
                render_pass_info.renderPass = m_imgui_render_pass;
                render_pass_info.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
                render_pass_info.renderArea.extent = m_swapchain.swapchain_extent;

                std::array<vk::ClearValue, 1> clear_values{};
                clear_values[0].color = { m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a };
                render_pass_info.clearValueCount = static_cast<u32>(clear_values.size());
                render_pass_info.pClearValues = clear_values.data();

                ctx.cmd.beginRenderPass(render_pass_info, vk::SubpassContents::eInline);
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

        m_output_image->get_accessible_image_ref().layout = m_graph->texture_layout(m_scene_color); // Sync image layout to graph last use
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;         // imgui sets layout to ePresentSrcKHR

        m_graph->end_frame();

        static u32 dup_counter = 0;
        if (dup_counter >= 250) {
            LOG(info, "[{}]", m_graph->dump())
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

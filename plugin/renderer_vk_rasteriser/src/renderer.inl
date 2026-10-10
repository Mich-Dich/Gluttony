
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

        m_graph->begin_frame();

        process_pending_meshes();               // pull meshes in/out at the frame boundary
        update_camera_ubo();                    // fill the current frame's camera UBO
        build_instance_buffer();                // pack scene instances into this frame's SSBO

        // Backbuffer import -------------------------------------------------------------------------------------------
        const vk::ImageLayout bb_initial = m_swapchain_images_layout[m_current_swapchain_image];
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::eUndefined;

        graph::texture_desc bb_desc{};
        bb_desc.name = "backbuffer";
        bb_desc.format = m_swapchain.swapchain_format;
        bb_desc.width = m_swapchain.swapchain_extent.width;
        bb_desc.height = m_swapchain.swapchain_extent.height;
        bb_desc.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;

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
        rt_desc.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc |
            vk::ImageUsageFlagBits::eTransferDst;
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
        depth_desc.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
        depth_desc.persistent = true;

        m_depth_target = m_graph->create_texture(depth_desc);

        // Pass: depth prepass -----------------------------------------------------------------------------------------
        m_graph->add_pass("depth_prepass",
            [&](graph::pass_builder& b) {
                b.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, 1.0f, 0);
            },
            [this](graph::pass_context& ctx) {

                if (m_scene_instances.empty() || m_mesh_slots.empty())
                    return;

                ctx.begin_rendering();

                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_depth_pipeline);
                m_dev->bind_descriptor_buffer({ m_frame_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                draw_scene_meshes(ctx.cmd);

                ctx.end_rendering();
                m_frame_render_passes++;
            });

        // Pass: forward shading ---------------------------------------------------------------------------------------
        m_graph->add_pass("scene",
            [&](graph::pass_builder& b) {
                b.color_attachment(m_triangle_target, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32,4>{m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a}));

                // Depth is loaded — the prepass already wrote the frontmost depth. The pipeline runs with depthTest=Equal,
                // depthWrite=OFF, so early-z culls occluded fragments before the fragment shader
                b.depth_attachment(m_depth_target, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, 1.0f, 0);
            },
            [this](graph::pass_context& ctx) {

                if (m_scene_instances.empty() || m_mesh_slots.empty())
                    return;

                ctx.begin_rendering();

                ctx.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_mesh_pipeline);
                m_dev->bind_descriptor_buffer({ m_frame_desc_buffers[m_current_frame] }, ctx.cmd);
                m_dev->bind_descriptor_set(m_mesh_pipeline_layout, 0, 0, 0, ctx.cmd, vk::PipelineBindPoint::eGraphics);

                draw_scene_meshes(ctx.cmd);

                ctx.end_rendering();
                m_frame_render_passes++;
            });

        // Pass: ImGui -> backbuffer -----------------------------------------------------------------------------------
        m_graph->add_pass("imgui",
            [&](graph::pass_builder& b) {
                b.color_attachment(m_backbuffer_handle, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);

                b.read(m_triangle_target, graph::texture_usage::sampled);       // ImGui editor samples scene_color
                b.set_side_effects(true);
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

        // MUST run before m_graph->execute() so imgui pass's execute lambda sees valid data
        end_imgui_frame();                          // Produce ImGui draw data for this frame

        vk::CommandBuffer cmd = m_render_cmd[m_current_frame];
        m_graph->execute(cmd);                      // The graph emits all its barriers and invokes every pass's execute lambda

        // The graph moved the scene colour into whatever layout its last read needed. Sync that back to the image so
        // the next frame's import starts from the correct state, and so the descriptor ImGui caches stays valid
        m_output_image->get_accessible_image_ref().layout = m_graph->texture_layout(m_triangle_target);

        // The imgui pass's VkRenderPass declares finalLayout = ePresentSrcKHR, so after execute() the backbuffer is presentable
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;

        m_graph->end_frame();
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

        m_deletion_queue.push_func([&]() {
            for (auto s : m_render_semaphores)  if (s) m_device.destroySemaphore(s);
            for (auto s : m_present_semaphores) if (s) m_device.destroySemaphore(s);
            for (u32 i = 0; i < m_image_count; i++)
                if (m_in_flight_fences[i]) m_device.destroyFence(m_in_flight_fences[i]);
            if (m_immediate_submit_fence) m_device.destroyFence(m_immediate_submit_fence);
            if (m_immediate_submit_command_pool) m_device.destroyCommandPool(m_immediate_submit_command_pool);
            if (m_graphics_pool) m_device.destroyCommandPool(m_graphics_pool);
            if (m_dev) delete m_dev;
        });
    }

    // render target ---------------------------------------------------------------------------------------------------

    void renderer::create_render_target() {

        m_output_image = GLT::create_ref<image>();
        m_output_image->resize({(u32)m_render_size.x, (u32)m_render_size.y, 1});
    }


    void renderer::destroy_render_target() { m_output_image.reset(); }

}

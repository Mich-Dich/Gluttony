
#include <util/pch.h>

#include <imgui.h>

#include <event/event_bus.h>
#include <event/application_event.h>
#include <asset/material.h>
#include <plugin_system/plugin_manager.h>
#include <platform/i_window.h>
#include <render/i_renderer.h>
#include <config/imgui_config.h>



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
        #define VK_CHECK_S(expr)
        #define VK_CHECK(expr, successMsg, failureMsg)
    #endif

    // TYPES ===========================================================================================================

    // Matches the layout declared in mesh_materials.rchit.glsl
    struct gpu_material {

        glm::vec4                                   base_color{1.0f};
        glm::vec3                                   emissive{0.0f};
        f32                                         roughness{0.5f};
        f32                                         metallic{0.0f};
        f32                                         reflectance{0.5f};
        f32                                         normal_scale{1.0f};
        f32                                         occlusion_strength{1.0f};
        std::array<u32, TEXTURE_SLOT_COUNT>         textures{};     // bindless texture indices, 0xFFFFFFFF = none
        u32                                         flags{0};
        u32                                         _pad[1];
    };
    static_assert(sizeof(gpu_material) == 80);


    struct gpu_geometry {

        u32                             vertex_base;
        u32                             index_base;
        u32                             material_index;
        u32                             _pad;
    };
    static_assert(sizeof(gpu_geometry) == 16);


    struct camera_ubo {
        glm::mat4                   view_inv{ 1.0f };
        glm::mat4                   proj_inv{ 1.0f };
        glm::vec4                   sun_direction{ 0.5f, 1.0f, 0.3f, 0.0f };
        glm::vec4                   sun_color{ 1.0f, 0.95f, 0.85f, 3.0f };

        // previous frame's (proj * view). Used by the raygen to reproject the current world-space hit point back into
        // the previous frame's screen space
        glm::mat4                   prev_view_proj{ 1.0f };

        // NEW: current frame's (proj * view). Declared in every shader's CameraUBO block — the shader-side layout will
        // not match without it, and the shader reads [temporal] from outside the buffer
        glm::mat4                   view_proj{ 1.0f };

        // x = reset flag (1 = history invalid, use current only)
        // y = max history length (clamps the running-mean weight)
        // z = write g-buffer index (0 or 1) for the current frame
        // w = frame counter — varies per frame so ray noise decorrelates and temporal accumulation can converge
        glm::uvec4 temporal{ 1u, 64u, 0u, 0u };
    };
    static_assert(sizeof(camera_ubo) == 304, "camera_ubo layout mismatch");

    // STATIC VARIABLES ================================================================================================
    
    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Counts primitive draws ImGui will issue - i.e. every cmd buffer entry
    // that isn't a user callback. This matches the Vulkan backend's actual
    // vkCmdDraw* count.
    u32 count_imgui_draw_calls(const ImDrawData* draw_data);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    u32 count_imgui_draw_calls(const ImDrawData* draw_data) {

        if (!draw_data)
            return 0;

        u32 count = 0;
        for (int i = 0; i < draw_data->CmdListsCount; ++i) {

            const ImDrawList* cmd_list = draw_data->CmdLists[i];
            for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j) {

                if (cmd_list->CmdBuffer[j].UserCallback == nullptr)
                    ++count;
            }
        }
        return count;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    bool renderer::create() {

        mp_window = GLT::plugin_manager::get_plugin_ref<GLT::platform::i_window_plugin>(GLT::plugin_manager::interface::window);
        ASSERT(mp_window, "", "Failed to get window plugin")

        // Sensible default view until the world layer pushes a real camera.
        m_active_camera.view = glm::lookAt(glm::vec3(0.f, 0.f, 80.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
        m_active_camera.position = glm::vec3(0.f, 0.f, 80.f);
        m_active_camera.fov = 45.f;

        init_vulkan();
        create_base_resources();
        create_acceleration_structures();
        create_rt_pipeline();
        update_descriptor_set();
        imgui_init();

        // TODO: only enable if needed (disable in release build)
        create_material_preview_resources();

        IGNORE_UNUSED_VARIABLE_START
        const void* unused_buffer = m_output_image->get_descriptor_set();        // ensure descriptor is available
        IGNORE_UNUSED_VARIABLE_STOP

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

        m_output_image.reset();
        m_accum_image[0].reset();
        m_accum_image[1].reset();
        m_gbuffer_pos[0].reset();
        m_gbuffer_pos[1].reset();
        m_gbuffer_nrm[0].reset();
        m_gbuffer_nrm[1].reset();
        m_preview_image.reset();
        m_current_raw.reset();
        m_preview_accum_image.reset();
        m_preview_dummy_gbuffer.reset();
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
        m_old_swapchain = nullptr;

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

        // GPU timing: read the previous use of this command buffer slot -----------------------------------------------
        // The fence above guarantees the timestamps we wrote last time this slot was submitted have completed, so the result is available without waiting.
        if (m_timestamp_pool) {

            u64 timestamps[2] = {0, 0};
            const vk::Result r = m_device.getQueryPoolResults(m_timestamp_pool, m_current_frame * 2, static_cast<u32>(2),
                sizeof(timestamps), timestamps, sizeof(u64), vk::QueryResultFlagBits::e64);

            if (r == vk::Result::eSuccess && timestamps[1] > timestamps[0]) {

                const f32 elapsed_ms = static_cast<f32>(timestamps[1] - timestamps[0]) * m_timestamp_period_ns * 1e-6f;
                m_gpu_frame_time_ms[m_current_frame] = elapsed_ms;
                m_last_gpu_time_ms = elapsed_ms;
            }
        }

        process_pending_meshes();           // Pull new meshes in / old meshes out at the frame boundary
        rebuild_tlas_from_scene();          // Rebuild the TLAS from the scene the world submitted during update

        // reset per-frame counters ------------------------------------------------------------------------------------
        m_frame_draw_calls = 0;
        m_frame_render_passes = 0;

        try {

            auto acquire_result = m_device.acquireNextImageKHR(m_swapchain.swapchain_handle, UINT64_MAX, m_present_semaphores[m_current_frame], nullptr);
            m_current_swapchain_image = acquire_result.value;

        } catch (const vk::OutOfDateKHRError&) {

            resize_swapchain(m_target_framebuffer_size);
            m_state = system_state::idle;
            return;                                                         // skip the rest of begin_frame this frame
        }

        // Reset command buffer and begin recording
        vk::CommandBuffer& current_cmd = m_rt_render_cmd[m_current_frame];  // Record ImGui rendering into the command buffer
        current_cmd.reset();
        vk::CommandBufferBeginInfo begin_info{};
        begin_info.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        current_cmd.begin(begin_info);

        // Start the GPU timer for this frame
        if (m_timestamp_pool) {

            current_cmd.resetQueryPool(m_timestamp_pool, m_current_frame * 2, 2);
            current_cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, m_timestamp_pool, m_current_frame * 2);
        }

        // Update camera + sun parameters
        {
            const f32 aspect = static_cast<f32>(m_render_size.x) / static_cast<f32>(m_render_size.y);
            glm::mat4 proj = glm::perspective(glm::radians(m_active_camera.fov), aspect, m_active_camera.near_plane, m_active_camera.far_plane);
            proj[1][1] *= -1;

            const glm::mat4 view = m_active_camera.view;
            const glm::mat4 view_proj = proj * view;

            camera_ubo ubo{};
            ubo.view_inv = glm::inverse(view);
            ubo.proj_inv = glm::inverse(proj);
            ubo.sun_direction = glm::vec4(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)), 0.0f);
            ubo.sun_color = glm::vec4(1.0f, 0.95f, 0.85f, 3.0f);
            ubo.prev_view_proj = m_prev_view_proj;
            ubo.view_proj = view_proj;
            ubo.temporal = glm::uvec4(m_temporal_valid ? 0u : 1u, 64u, m_gbuffer_index, m_frame_counter);

            void* data = m_vr_dev->map_buffer(m_uniform_buffer);
            std::memcpy(data, &ubo, sizeof(ubo));
            m_vr_dev->unmap_buffer(m_uniform_buffer);

            m_prev_view_proj = view_proj;                   // Stash for next frame
            m_frame_counter++;                              // next frame gets a different seed
        }

        {   // Descriptor buffer binding for the main pass
            // --- preview pass (before the main scene) ---
            if (m_preview_ready && m_preview_queued) {

                upload_preview_data();
                dispatch_material_preview(current_cmd);
                m_preview_queued = false;
            }

            // --- main scene ---
            m_vr_dev->bind_descriptor_buffer({ m_resource_desc_buffer }, current_cmd);
            m_vr_dev->bind_descriptor_set(m_pipeline_layout, 0, 0, 0, current_cmd);
        }

        transition_image_layout(current_cmd, image_type::swapchain, vk::ImageLayout::eTransferDstOptimal);
        clear_output_image(current_cmd, m_clear_color);
        transition_image_layout(current_cmd, image_type::render, vk::ImageLayout::eGeneral);
        transition_image_layout(current_cmd, image_type::accum,  vk::ImageLayout::eGeneral); 

        // clear the write G-buffers so pixels that miss the primary ray read as "no hit"
        clear_gbuffer(current_cmd, m_gbuffer_pos[m_gbuffer_index]);
        clear_gbuffer(current_cmd, m_gbuffer_nrm[m_gbuffer_index]);

        // pass 1: trace + write raw colour ----------------------------------------------------------------------------
        current_cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_rt_pipeline);
        m_vr_dev->dispatch_rays(m_rt_pipeline, m_sbt_buffer, m_render_size.x, m_render_size.y, 1, current_cmd);

        m_frame_draw_calls++;
        m_frame_render_passes++;

        // barrier: raygen writes to current_raw, compute reads it -----------------------------------------------------
        {
            vk::MemoryBarrier mb = vk::MemoryBarrier()
                .setSrcAccessMask(vk::AccessFlagBits::eShaderWrite)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

            current_cmd.pipelineBarrier(
                vk::PipelineStageFlagBits::eRayTracingShaderKHR,
                vk::PipelineStageFlagBits::eComputeShader,
                {}, 1, &mb, 0, nullptr, 0, nullptr);
        }

        // pass 2: temporal resolve ------------------------------------------------------------------------------------
        {
            current_cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_temporal_pipeline);
            m_vr_dev->bind_descriptor_buffer({ m_resource_desc_buffer }, current_cmd);
            m_vr_dev->bind_descriptor_set(m_pipeline_layout, 0, 0, 0, current_cmd, vk::PipelineBindPoint::eCompute);

            const u32 gx = (static_cast<u32>(m_render_size.x) + 7u) / 8u;
            const u32 gy = (static_cast<u32>(m_render_size.y) + 7u) / 8u;
            current_cmd.dispatch(gx, gy, 1);

            m_frame_draw_calls++;
            m_frame_render_passes++;
        }

        // advance temporal state
        m_temporal_valid = true;
        m_gbuffer_index = 1 - m_gbuffer_index;
        transition_image_layout(current_cmd, image_type::render, vk::ImageLayout::eShaderReadOnlyOptimal);       // to SHADER_READ_ONLY_OPTIMAL

        begin_imgui_frame(current_cmd);
    }


    void renderer::draw_frame() {

        VALIDATE_INIT
        
        vk::CommandBuffer& current_cmd = m_rt_render_cmd[m_current_frame];              // Record ImGui rendering into the command buffer
        end_imgui_frame(current_cmd);

        // close the GPU timer for this frame --------------------------------------------------------------------------
        if (m_timestamp_pool)
            current_cmd.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, m_timestamp_pool, m_current_frame * 2 + 1);
    
        current_cmd.end();                                                              // End command buffer

        vk::SubmitInfo submit_info{};                                                   // Submit to graphics queue
        vk::PipelineStageFlags wait_stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &m_present_semaphores[m_current_frame];
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &current_cmd;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &m_render_semaphores[m_current_frame];

        try {
            m_queues.graphics_queue.submit(submit_info, m_in_flight_fences[m_current_frame]);
        } catch (...) {
            LOG(error, "Failed to submit");
        }

        vk::PresentInfoKHR present_info{};                                              // Present
        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = &m_render_semaphores[m_current_frame];
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &m_swapchain.swapchain_handle;
        present_info.pImageIndices = &m_current_swapchain_image;

        try {
            IGNORE_UNUSED_VARIABLE_START
            const vk::Result result = m_queues.present_queue.presentKHR(present_info);
            IGNORE_UNUSED_VARIABLE_STOP
        } catch (const vk::OutOfDateKHRError&) {
            resize_swapchain(m_target_framebuffer_size);
        }

        // Advance to next frame
        m_current_frame = (m_current_frame + 1) % MAX_CONCURRENT_FRAMES;
        m_state = system_state::idle;
    }


    glm::ivec2 renderer::get_swapchain_size() const {

        return {m_swapchain.swapchain_extent.width, m_swapchain.swapchain_extent.height}; 
    }

    IGNORE_UNUSED_PARAMETER_START
    IGNORE_UNUSED_VARIABLE_START

    void renderer::resize(const u32 width, const u32 height) { }

    IGNORE_UNUSED_VARIABLE_STOP
    IGNORE_UNUSED_PARAMETER_STOP

    void renderer::set_render_size(const glm::ivec2& size) {

        if (size.x <= 0 || size.y <= 0)
            return;
        if (m_render_size == size)
            return;

        m_render_size = size;
        m_output_image->resize({size.x, size.y, 1});
        m_current_raw->resize({size.x, size.y, 1}, GLT::render::image_format::RGBA16F);

        for (u32 i = 0; i < 2; ++i) {
            m_accum_image[i]->resize({size.x, size.y, 1}, GLT::render::image_format::RGBA32F);
            m_gbuffer_pos[i]->resize({size.x, size.y, 1}, GLT::render::image_format::RGBA32F);
            m_gbuffer_nrm[i]->resize({size.x, size.y, 1}, GLT::render::image_format::RGBA16F);
        }

        immediate_submit([&](vk::CommandBuffer cmd) {
            const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
            auto to_general = [&](GLT::ref<image>& img) {
                m_vr_dev->transition_image_layout(cmd,
                    img->get_allocated_image_ref().image,
                    vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, range,
                    vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eAllCommands);
                img->get_accessible_image_ref().layout = vk::ImageLayout::eGeneral;
            };
            to_general(m_accum_image[0]);
            to_general(m_accum_image[1]);
            to_general(m_gbuffer_pos[0]);
            to_general(m_gbuffer_pos[1]);
            to_general(m_gbuffer_nrm[0]);
            to_general(m_gbuffer_nrm[1]);
            to_general(m_current_raw);
        });

        m_temporal_valid = false;       // new sizes -> no valid history
        m_gbuffer_index  = 0;

        update_descriptor_set();
    }


    void* renderer::get_rendered_image() { return static_cast<void*>(m_output_image->get_descriptor_set()); }


    glm::uvec2 renderer::get_rendered_image_size() { return m_output_image->get_size(); }


    [[nodiscard]] debug::render_stats renderer::get_render_stats() const {

        return debug::render_stats{

            // GPU time: most recent completed measurement (updated in begin_frame from the previous use of the current command buffer slot)
            // Zero until the first slot has wrapped at least once.
            .gpu_time_ms = m_last_gpu_time_ms,

            // rendering -----------------------------------------------------------------------------------------------
            // These are per-frame totals; the HUD reads them after draw_frame(). draw_calls includes both the scene RT dispatch 
            // and every ImGui primitive draw. render_passes is the RT dispatch plus the ImGui render pass.
            .draw_calls = m_frame_draw_calls,
            .triangles = m_index_used / 3,
            .vertices = m_vertex_used,
            .render_passes = m_frame_render_passes,

            // memory (absolute, not per-frame) ------------------------------------------------------------------------
            .vram_bytes = GLT::render::image::get_live_vram_bytes(),
            .ram_bytes = 0,                     // not tracked yet; would need a heap hook

            // resources (absolute counts) -----------------------------------------------------------------------------
            .texture_count = GLT::render::image::get_live_count(),
            .buffer_count = m_live_buffer_count,
            .descriptor_set_count = m_live_descriptor_set_count,
            .pipeline_count = m_live_pipeline_count,
        };
    }


    void renderer::set_active_camera(const GLT::world::camera_snapshot& camera) { m_active_camera = camera; }


	void renderer::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {

        VK_CHECK_S(m_device.waitForFences(m_immediate_submit_fence, VK_TRUE, UINT64_MAX));          // Wait for any previous submission to finish (fence was signaled on creation)
        m_device.resetFences(m_immediate_submit_fence);
        m_immediate_submit_command_buffer.reset();

        vk::CommandBufferBeginInfo begin_info = vk::CommandBufferBeginInfo()            // Begin command buffer
            .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            
        m_immediate_submit_command_buffer.begin(begin_info);
        function(m_immediate_submit_command_buffer);                                    // Execute user function
        m_immediate_submit_command_buffer.end();

        vk::CommandBufferSubmitInfo cmd_submit_info = vk::CommandBufferSubmitInfo()
            .setCommandBuffer(m_immediate_submit_command_buffer);

        vk::SubmitInfo2 submit_info = vk::SubmitInfo2()
            .setCommandBufferInfoCount(1)
            .setPCommandBufferInfos(&cmd_submit_info);

        m_queues.graphics_queue.submit2(submit_info, m_immediate_submit_fence);

        VK_CHECK_S(m_device.waitForFences(m_immediate_submit_fence, VK_TRUE, UINT64_MAX));
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

        // two timestamps per concurrent frame, so we can measure the GPU time of the frame that used each command buffer.
        {
            const vk::PhysicalDeviceProperties props = m_physical_device.getProperties();
            m_timestamp_period_ns = (props.limits.timestampPeriod > 0.0f) ? props.limits.timestampPeriod : 1.0f;

            vk::QueryPoolCreateInfo qp_info = vk::QueryPoolCreateInfo()
                .setQueryType(vk::QueryType::eTimestamp)
                .setQueryCount(MAX_CONCURRENT_FRAMES * 2);

            m_timestamp_pool = m_device.createQueryPool(qp_info);
            ASSERT(m_timestamp_pool, "", "Failed to create timestamp query pool");
        }

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

            if (m_timestamp_pool) {
                m_device.destroyQueryPool(m_timestamp_pool);
            }

            m_timestamp_pool = nullptr;
            m_graphics_pool = nullptr;

            if (m_vr_dev)
                delete m_vr_dev;
        });
    }


    void renderer::create_acceleration_structures() {

        // Create the persistent TLAS ----------------------------------------------------------------------------------
        // Created once with a fixed max instance count. Its device address gets baked into the descriptor set inside 
        // create_rt_pipeline(), so it must never move. Only its *contents* are rebuilt as the scene changes
        {
            vr::tlas_create_info tci{};
            tci.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;
            tci.max_instance_count = TLAS_MAX_INSTANCES;

            std::tie(m_tlas_handle, m_tlas_build_info) = m_vr_dev->create_tlas(tci);

            m_tlas_instance_buffer = m_vr_dev->create_instance_buffer(TLAS_MAX_INSTANCES);
            m_live_buffer_count += 1;
        }

        // Pre-reserve shared-buffer headroom --------------------------------------------------------------------------
        // Does two things at once:
        //   a) Makes the first load_mesh() below see a non-empty source buffer, so the "grow" branch in reserve_mesh_space()
        //      isn't hit against default-constructed buffers on the very first call.
        //   b) Gives every load/unload thereafter room to fit without triggering a realloc.
        reserve_mesh_space(VERTEX_HEADROOM_MIN, INDEX_HEADROOM_MIN, MATERIAL_HEADROOM_MIN);

        // Deferred cleanup --------------------------------------------------------------------------------------------
        // BLASes now live on the slots, so walk those. Buffers and the TLAS are renderer-owned and go straight into the deletion queue.
        m_deletion_queue.push_func([&]() {

            for (auto& slot : m_mesh_slots) {
                if (slot.alive && slot.blas.buffer.buffer)
                    m_vr_dev->destroy_blas(slot.blas);
            }
            m_mesh_slots.clear();

            if (m_vertex_buffer.buffer)             m_vr_dev->destroy_buffer(m_vertex_buffer);
            if (m_index_buffer.buffer)              m_vr_dev->destroy_buffer(m_index_buffer);
            if (m_material_buffer.buffer)           m_vr_dev->destroy_buffer(m_material_buffer);
            if (m_geometry_buffer.buffer)           m_vr_dev->destroy_buffer(m_geometry_buffer);  
            if (m_tlas_instance_buffer.buffer)      m_vr_dev->destroy_buffer(m_tlas_instance_buffer);

            m_vr_dev->destroy_tlas(m_tlas_handle);
        });
    }


    void renderer::create_rt_pipeline() {

        // Don't lie [using] but it make the binding more readable
        using VKDI = vr::descriptor_item;
        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // [POI]
        // Now we create a descriptor layout for the ray tracing pipeline last parameter is a pointer to the items vector,
        // so we can use it later to create the descriptor set for now we have only one item, so we just pass the address
        // of the first element if we want to update the descriptor set later with another item, we can just reassign the 
        // vr::descriptor_item::pItems with new items and update the descriptor set
        m_resource_bindings = {
            VKDI(0, VKDT::eAccelerationStructureKHR, VKSF::eRaygenKHR | VKSF::eClosestHitKHR, 1, &m_tlas_handle.buffer.dev_address),
            VKDI(1, VKDT::eUniformBuffer, VKSF::eRaygenKHR | VKSF::eClosestHitKHR | VKSF::eCompute, 1, &m_uniform_buffer),
            VKDI(2, VKDT::eStorageImage, VKSF::eRaygenKHR | VKSF::eCompute, 10, &m_output_image->get_accessible_image_ref(), 1),
            VKDI(3, VKDT::eStorageBuffer, VKSF::eClosestHitKHR, 1, &m_material_buffer),
            VKDI(4, VKDT::eStorageBuffer, VKSF::eClosestHitKHR, 1, &m_vertex_buffer),
            VKDI(5, VKDT::eStorageBuffer, VKSF::eClosestHitKHR, 1, &m_index_buffer),

            // bindless textures support
            // Bindless texture array. No dynamic_array_size -> fixed-size array, every element is written in create_default_texture() and per-element on load
            VKDI(6, VKDT::eCombinedImageSampler, VKSF::eClosestHitKHR, BINDLESS_TEXTURE_MAX, m_texture_descriptors.data()),
            VKDI(7, VKDT::eStorageBuffer, VKSF::eClosestHitKHR, 1, &m_geometry_buffer),

            // ping-pong accum + g-buffer
            VKDI(8,  VKDT::eStorageImage, VKSF::eCompute, 10, &m_accum_image[0]->get_accessible_image_ref(), 1),
            VKDI(9,  VKDT::eStorageImage, VKSF::eCompute, 10, &m_accum_image[1]->get_accessible_image_ref(), 1),
            VKDI(10, VKDT::eStorageImage, VKSF::eClosestHitKHR | VKSF::eCompute, 10, &m_gbuffer_pos[0]->get_accessible_image_ref(), 1),
            VKDI(11, VKDT::eStorageImage, VKSF::eClosestHitKHR | VKSF::eCompute, 10, &m_gbuffer_pos[1]->get_accessible_image_ref(), 1),
            VKDI(12, VKDT::eStorageImage, VKSF::eClosestHitKHR | VKSF::eCompute, 10, &m_gbuffer_nrm[0]->get_accessible_image_ref(), 1),
            VKDI(13, VKDT::eStorageImage, VKSF::eClosestHitKHR | VKSF::eCompute, 10, &m_gbuffer_nrm[1]->get_accessible_image_ref(), 1),
            VKDI(14, VKDT::eStorageImage, VKSF::eRaygenKHR | VKSF::eCompute, 10, &m_current_raw->get_accessible_image_ref(), 1),
        };

        // create a descriptor set layout, for the ray tracing pipeline
        m_resource_descriptor_layout = m_vr_dev->create_descriptor_set_layout(m_resource_bindings);

        m_pipeline_layout = m_vr_dev->create_pipeline_layout(m_resource_descriptor_layout);

        // create shaders for the ray tracing pipeline
        // Spir-V bytecode is required

        const auto shader_dir = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "shader";

        // load the ray gen shader
        auto ray_gen_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh_materials.rgen.glsl");
        ASSERT(!ray_gen_spv.empty(), "", "Failed to load shader")
        auto ray_gen_shader_module = m_vr_dev->create_shader_from_spv(ray_gen_spv);

        // load the miss shader
        auto ray_miss_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh_materials.rmiss.glsl");
        ASSERT(!ray_miss_spv.empty(), "", "Failed to load shader")
        auto ray_miss_shader_module = m_vr_dev->create_shader_from_spv(ray_miss_spv);

        // load the closest hit shader
        auto closest_hit_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh_materials.rchit.glsl");
        ASSERT(!closest_hit_spv.empty(), "", "Failed to load shader")
        auto closest_hit_shader_module = m_vr_dev->create_shader_from_spv(closest_hit_spv);

        // AO hit group - writes 1.0 (occluded)
        auto ao_hit_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh_ao.rchit.glsl");
        ASSERT(!ao_hit_spv.empty(), "", "Failed to load shader")
        auto ao_hit_shader_module = m_vr_dev->create_shader_from_spv(ao_hit_spv);

        // AO miss - writes 0.0 (un-occluded)
        auto ao_miss_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh_ao.rmiss.glsl");
        ASSERT(!ao_miss_spv.empty(), "", "Failed to load shader")
        auto ao_miss_shader_module = m_vr_dev->create_shader_from_spv(ao_miss_spv);

        // [POI]
        // Pipeline settings for the ray tracing pipeline
        // we can set the max recursion depth, max payload size and max hit attribute size
        // max payload size is the size of the data we that every ray can carry, in this case it is a vec3
        // Look at the shader code to see how the payload is used
        // max hit attribute size is the size of the that gets passed to the hit shaders if there is a hit
        // we get barycentric coordinates of the hit point in this case which is a vec2
        vr::pipeline_settings pipeline_settings = {};
        pipeline_settings.pipeline_layout = m_pipeline_layout;

        // Depth budget: rgen -> bounce 0 chit -> bounce 1 chit -> ... -> bounce N-1 chit
        // -> shadow ray from the deepest chit. For 3 indirect bounces plus a nested shadow trace, that's rgen + 4 chits = depth 6
        pipeline_settings.max_recursion_depth = 6;
        pipeline_settings.max_payload_size = sizeof(glm::vec4);
        pipeline_settings.max_hit_attribute_size = sizeof(glm::vec2);

        // Collection of shaders for the pipeline
        vr::ray_tracing_shader_collection shader_collection = {};

        shader_collection.ray_gen_shaders.push_back(ray_gen_shader_module);

        // Miss shaders: index 0 = primary, index 1 = AO
        shader_collection.miss_shaders.push_back(ray_miss_shader_module);   // missIndex 0
        shader_collection.miss_shaders.push_back(ao_miss_shader_module);    // missIndex 1

        // Hit groups: index 0 = primary, index 1 = AO
        {
            vr::hit_group primary_hit = {};
            primary_hit.closest_hit_shader = closest_hit_shader_module;
            shader_collection.hit_groups.push_back(primary_hit);            // sbtRecordOffset 0
        }
        {
            vr::hit_group ao_hit = {};
            ao_hit.closest_hit_shader = ao_hit_shader_module;
            shader_collection.hit_groups.push_back(ao_hit);                 // sbtRecordOffset 1
        }

        // create the ray tracing pipeline, a vk::Pipeline object
        auto [pipeline, sbtInfo] = m_vr_dev->create_ray_tracing_pipeline(shader_collection, pipeline_settings);
        m_rt_pipeline = pipeline;

        // [POI]
        // Build the shader binding table, it is a buffer that contains the shaders for the pipeline and we can update hit record data if we want
        m_sbt_buffer = m_vr_dev->create_sbt(m_rt_pipeline, sbtInfo);

        // ---- temporal resolve compute pipeline ---------------------------------------
        {
            auto compute_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh_materials_temporal.comp.glsl");
            ASSERT(!compute_spv.empty(), "", "Failed to load temporal resolve shader")
            auto compute_module = m_vr_dev->create_shader_from_spv(compute_spv);

            vk::ComputePipelineCreateInfo cpci{};
            cpci.layout = m_pipeline_layout;
            cpci.stage = vk::PipelineShaderStageCreateInfo()
                .setStage(vk::ShaderStageFlagBits::eCompute)
                .setModule(compute_module.module)
                .setPName("main");

            auto result = m_device.createComputePipeline(nullptr, cpci);
            VALIDATE(result.result == vk::Result::eSuccess, , "", "Failed to create temporal compute pipeline")
            m_temporal_pipeline = result.value;
            m_device.destroyShaderModule(compute_module.module);
        }

        // create a descriptor buffer for the ray tracing pipeline
        m_resource_desc_buffer = m_vr_dev->create_descriptor_buffer(m_resource_descriptor_layout, m_resource_bindings,
            vr::descriptor_buffer_type::combined);                          // sampler | resource

        m_live_buffer_count++;                                              // m_resource_desc_buffer.buffer
        m_live_pipeline_count++;                                            // m_rt_pipeline
        m_live_descriptor_set_count++;                                      // one set inside m_resource_desc_buffer

        // cleanup
        m_device.destroyShaderModule(ray_gen_shader_module.module);
        m_device.destroyShaderModule(ray_miss_shader_module.module);
        m_device.destroyShaderModule(closest_hit_shader_module.module);
        m_device.destroyShaderModule(ao_hit_shader_module.module);
        m_device.destroyShaderModule(ao_miss_shader_module.module);
        m_deletion_queue.push_func([&]() {

            m_vr_dev->destroy_sbt_buffer(m_sbt_buffer);
            m_device.destroyPipeline(m_rt_pipeline);
            m_device.destroyPipelineLayout(m_pipeline_layout);
            m_device.destroyDescriptorSetLayout(m_resource_descriptor_layout);
            m_vr_dev->destroy_buffer(m_resource_desc_buffer.buffer);
            if (m_temporal_pipeline)
                m_device.destroyPipeline(m_temporal_pipeline);
        });
    }


    void renderer::create_base_resources() {

        vk::CommandPoolCreateInfo command_pool_CI = vk::CommandPoolCreateInfo()
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

        m_immediate_submit_command_pool = m_device.createCommandPool(command_pool_CI);

        vk::CommandBufferAllocateInfo cmd_alloc_I = vk::CommandBufferAllocateInfo()
            .setCommandPool(m_immediate_submit_command_pool)
            .setLevel(vk::CommandBufferLevel::ePrimary)
            .setCommandBufferCount(1);
        std::vector<vk::CommandBuffer> command_buffer_result = m_device.allocateCommandBuffers(cmd_alloc_I);
        VALIDATE(command_buffer_result.size() > 0, , "", "Failed to allocate command buffer")
        m_immediate_submit_command_buffer = command_buffer_result[0];

        vk::FenceCreateInfo fence_CI = vk::FenceCreateInfo().setFlags(vk::FenceCreateFlagBits::eSignaled);
        m_immediate_submit_fence = m_device.createFence(fence_CI);

        m_output_image = GLT::create_ref<image>();
        m_output_image->resize({m_render_size.x, m_render_size.y, 1});

        m_current_raw = GLT::create_ref<image>();
        m_current_raw->resize({m_render_size.x, m_render_size.y, 1}, GLT::render::image_format::RGBA16F);

        // Ping-pong accum + g-buffer
        for (u32 index = 0; index < 2; ++index) {

            m_accum_image[index] = GLT::create_ref<image>();
            m_accum_image[index]->resize({m_render_size.x, m_render_size.y, 1}, GLT::render::image_format::RGBA32F);

            m_gbuffer_pos[index] = GLT::create_ref<image>();
            m_gbuffer_pos[index]->resize({m_render_size.x, m_render_size.y, 1}, GLT::render::image_format::RGBA32F);

            m_gbuffer_nrm[index] = GLT::create_ref<image>();
            m_gbuffer_nrm[index]->resize({m_render_size.x, m_render_size.y, 1}, GLT::render::image_format::RGBA16F);
        }

        // Init both accum and g-buffer into eGeneral. Anything read back by the shader
        // (imageLoad) requires eGeneral layout.
        immediate_submit([&](vk::CommandBuffer cmd) {
            const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            auto prep = [&](GLT::ref<image>& img) {
                m_vr_dev->transition_image_layout(cmd,
                    img->get_allocated_image_ref().image,
                    vk::ImageLayout::eUndefined,
                    vk::ImageLayout::eGeneral,
                    range,
                    vk::PipelineStageFlagBits::eTopOfPipe,
                    vk::PipelineStageFlagBits::eAllCommands);
                img->get_accessible_image_ref().layout = vk::ImageLayout::eGeneral;
            };

            prep(m_accum_image[0]);
            prep(m_accum_image[1]);
            prep(m_gbuffer_pos[0]);
            prep(m_gbuffer_pos[1]);
            prep(m_gbuffer_nrm[0]);
            prep(m_gbuffer_nrm[1]);
            prep(m_current_raw);
        });

        m_temporal_valid = false;   // first frame is a reset frame

        // samplers + white 1x1 fallback texture (slot 0) -------------------------
        // Must run before the default material is written, because it initialises m_texture_descriptors[] which the descriptor buffer reads
        create_default_texture();

        // default material (gpu_material slot 0) ----------------------------------------------------------------------
        reserve_material_space(1);
        gpu_material default_mat{};
        default_mat.base_color = { 0.8f, 0.8f, 0.8f, 1.0f };
        default_mat.roughness  = 0.7f;
        default_mat.textures.fill(0);       // white fallback index
        auto* dst = static_cast<gpu_material*>(m_vr_dev->map_buffer(m_material_buffer));
        dst[0] = default_mat;
        m_vr_dev->unmap_buffer(m_material_buffer);
        m_material_used = 1;

        // camera UBO --------------------------------------------------------------------------------------------------
        m_uniform_buffer = m_vr_dev->create_buffer(
            sizeof(camera_ubo),
            vk::BufferUsageFlagBits::eUniformBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count += 1;

        m_deletion_queue.push_func([&]() {

            if (m_uniform_buffer.buffer)
                m_vr_dev->destroy_buffer(m_uniform_buffer);

            if (m_immediate_submit_fence)
                m_device.destroyFence(m_immediate_submit_fence);

            if (m_immediate_submit_command_pool)
                m_device.destroyCommandPool(m_immediate_submit_command_pool);

            // Tear down GPU textures. Slot 0 is the white fallback and is destroyed too.
            for (auto& slot : m_texture_slots) {
                if (!slot.alive)
                    continue;

                if (slot.view)
                    m_device.destroyImageView(slot.view);

                vr::allocated_image img{};
                img.image = slot.image;
                img.allocation = slot.allocation;
                if (img.image)
                    m_vr_dev->destroy_image(img);
            }
            m_texture_slots.clear();

            if (m_default_sampler_linear)
                m_device.destroySampler(m_default_sampler_linear);

            if (m_default_sampler_nearest)
                m_device.destroySampler(m_default_sampler_nearest);
        });
    }


    void renderer::update_descriptor_set() {

        // Called from two places:
        //   1. reserve_mesh_space() when the shared buffers move - m_resource_desc_buffer already exists by then, we're just refreshing the resource handles.
        //   2. During init via reserve_mesh_space(), *before* create_rt_pipeline() has built m_resource_desc_buffer. 
        //      Skip - create_rt_pipeline() will pick up the current m_resource_bindings state when it builds the descriptor buffer.
        if (!m_resource_desc_buffer.buffer.buffer)
            return;

        // // Set the camera position
        // // movement, rotation and input is handled by the Application Base class and we can modify the camera values as we like
        // m_active_camera->m_position = glm::vec3(0.0f, 0.0f, 2.5f);

        // [POI] We already provided each descriptor item with the pointer to a resource back when we created the descriptor set layout
        // so we can just update the resource values here
        // if we want to update the descriptor set with a new item, we can just reassign the vr::descriptor_item::p*** with new items and update the descriptor set
        m_vr_dev->update_descriptor_buffer(m_resource_desc_buffer, m_resource_bindings, vr::descriptor_buffer_type::resource);
    }

}


#include <util/pch.h>

#include <imgui.h>

#include <event/event_bus.h>
#include <event/application_event.h>
#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_window_plugin.h>
#include <plugin_system/i_renderer_plugin.h>
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

    // Matches the layout declared in mesh_materials.rchit.glsl (std430).
    struct gpu_material {
        glm::vec4                   base_color{ 1.0f };
        f32                         roughness{ 0.5f };
        f32                         metallic{ 0.0f };
        u32                         vertex_base{ 0 };     // mesh vertex offset (in vertices)
        u32                         index_base{ 0 };      // submesh index offset (in indices)
    };
    static_assert(sizeof(gpu_material) == 32);


    struct camera_ubo {
        glm::mat4                   view_inv{ 1.0f };
        glm::mat4                   proj_inv{ 1.0f };
        glm::vec4                   sun_direction{ 0.5f, 1.0f, 0.3f, 0.0f };  // xyz = direction TO the sun (normalized)
        glm::vec4                   sun_color{ 1.0f, 0.95f, 0.85f, 3.0f }; // xyz = color, w = intensity
    };
    static_assert(sizeof(camera_ubo) == 160, "camera_ubo layout mismatch");

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

        // TODO: remove
        m_active_camera = create_ref<GLT::world::camera>();
        m_active_camera->set_position({0.0f, 0.0f, 80.f});

        init_vulkan();
        create_base_resources();
        create_acceleration_structures();
        create_rt_pipeline();
        update_descriptor_set();
        imgui_init();

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

        // ---- GPU timing: read the previous use of this command buffer slot -----------------
        // The fence above guarantees the timestamps we wrote last time this slot was submitted have completed, so the result is available without waiting.
        if (m_timestamp_pool) {

            u64 timestamps[2] = {0, 0};
            const vk::Result r = m_device.getQueryPoolResults(m_timestamp_pool,
                m_current_frame * 2, static_cast<u32>(2),
                sizeof(timestamps), timestamps, sizeof(u64),
                vk::QueryResultFlagBits::e64);

            if (r == vk::Result::eSuccess && timestamps[1] > timestamps[0]) {

                const f32 elapsed_ms = static_cast<f32>(timestamps[1] - timestamps[0]) * m_timestamp_period_ns * 1e-6f;
                m_gpu_frame_time_ms[m_current_frame] = elapsed_ms;
                m_last_gpu_time_ms                   = elapsed_ms;
            }
        }

        rebuild_tlas_if_dirty();

        // ---- reset per-frame counters ------------------------------------------------------
        m_frame_draw_calls    = 0;
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

        // Start the GPU timer for this frame.
        if (m_timestamp_pool) {

            current_cmd.resetQueryPool(m_timestamp_pool, m_current_frame * 2, 2);
            current_cmd.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe,
                m_timestamp_pool, m_current_frame * 2);
        }

        // Update camera + sun parameters
        {
            m_active_camera->set_aspect_ratio((f32)m_render_size.x / (f32)m_render_size.y);
            glm::mat4 proj = m_active_camera->get_projection_matrix();
            glm::mat4 view = m_active_camera->get_view_matrix();

            camera_ubo ubo{};
            ubo.view_inv = glm::inverse(view);
            ubo.proj_inv = glm::inverse(proj);
            ubo.sun_direction = glm::vec4(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)), 0.0f);
            ubo.sun_color = glm::vec4(1.0f, 0.95f, 0.85f, 3.0f);  // warm sun, intensity 3

            void* data = m_vr_dev->map_buffer(m_uniform_buffer);
            std::memcpy(data, &ubo, sizeof(ubo));
            m_vr_dev->unmap_buffer(m_uniform_buffer);
        }
        
        // Bind descriptor buffer
        m_vr_dev->bind_descriptor_buffer({m_resource_desc_buffer}, current_cmd);
        m_vr_dev->bind_descriptor_set(m_pipeline_layout, 0, 0, 0, current_cmd);

        transition_image_layout(current_cmd, image_type::swapchain, vk::ImageLayout::eTransferDstOptimal);
        clear_output_image(current_cmd, m_clear_color);
        transition_image_layout(current_cmd, image_type::render, vk::ImageLayout::eGeneral);                     // Transition output to GENERAL

        // Ray tracing
        current_cmd.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, m_rt_pipeline);
        m_vr_dev->dispatch_rays(m_rt_pipeline, m_sbt_buffer, m_render_size.x, m_render_size.y, 1, current_cmd);

        m_frame_draw_calls    += 1;     // one RT dispatch
        m_frame_render_passes += 1;     // the RT dispatch is the scene's only "pass"

        // // Blit from output image to swapchain image
        // transition_image_layout(current_cmd, image_type::swapchain, vk::ImageLayout::eTransferDstOptimal);       // to TRANSFER_DST_OPTIMAL
        // transition_image_layout(current_cmd, image_type::render, vk::ImageLayout::eTransferSrcOptimal);          // to TRANSFER_SRC_OPTIMAL
        // current_cmd.blitImage(
        //     m_output_image->get_allocated_image_ref().image,            vk::ImageLayout::eTransferSrcOptimal,
        //     m_swapchain.swapchain_images[m_current_swapchain_image],    vk::ImageLayout::eTransferDstOptimal,
        //     vk::ImageBlit(vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
        //     {vk::Offset3D(0, 0, 0), vk::Offset3D(m_render_size.x, m_render_size.y, 1)},
        //     vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
        //     {vk::Offset3D(0, 0, 0), vk::Offset3D(m_render_size.x, m_render_size.y, 1)}),
        //     vk::Filter::eLinear);

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

        if (size.x <= 0 || size.y <= 0)         return;     // guard first-frame {0,0} from editor_layer::update()
        if (m_render_size == size)              return;

        m_render_size = size;
        m_output_image->resize({size.x, size.y, 1});        // keep the image in sync with what's actually rendered
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

        
    void renderer::set_active_camera(ref<GLT::world::camera> active_camera) { m_active_camera = active_camera; }


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

    // uploaded mesh data ----------------------------------------------------------------------------------------------

    bool renderer::load_mesh(const std::filesystem::path& path) {

        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "No asset registry");

        auto handle = registry->load(path);
        VALIDATE(handle.has_value(), return false, "", "Failed to load mesh [{}]", path.generic_string());

        return load_mesh(*handle);
    }


    bool renderer::load_mesh(const GLT::asset::handle handle) {

        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "No asset registry");

        for (const auto& slot : m_mesh_slots)
            if (slot.asset == handle)
                return true;                        // mesh already uploaded

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(handle);
        VALIDATE(mesh, return false, "", "Asset [{}] is not a mesh_asset", registry->info(handle).name);
       
        // reserve buffer space. May grow the shared buffers
        u32 live_submeshes = 0;
        for (const auto& sm : mesh->submeshes)
            if (sm.index_count >= 3)
                live_submeshes++;

        // DEBUG-ONLY: -------------------------------------------------------------------------------------------------
        #define SANITY_CHECK_ON_THE_DECODED_MESH 0
        #if SANITY_CHECK_ON_THE_DECODED_MESH
            {
                LOG(info, "count supplied to reserve_mesh_space(): [{}], count in mesh asset [{}]", live_submeshes, mesh->submeshes.size())

                const size_t n_idx = mesh->indices.size();
                const size_t n_vtx = mesh->vertices.size();

                u32 global_max_idx = 0;
                for (u32 i : mesh->indices)
                    global_max_idx = std::max(global_max_idx, i);

                LOG(info, "mesh: {} verts, {} indices, max index in array = {}", n_vtx, n_idx, global_max_idx);

                if (global_max_idx >= n_vtx)
                    LOG(fatal, "  !! mesh index {} >= vertex count {} — asset is corrupt", global_max_idx, n_vtx);

                for (size_t si = 0; si < mesh->submeshes.size(); ++si) {
                    const auto& sm = mesh->submeshes[si];

                    const u64 end = u64(sm.first_index) + u64(sm.index_count);
                    if (end > n_idx) {
                        LOG(fatal, "  submesh[{}]: first={} count={} -> end={} OVERFLOWS index array size {}",
                            si, sm.first_index, sm.index_count, end, n_idx);
                        continue;
                    }

                    u32 sub_max_idx = 0;
                    for (u32 k = 0; k < sm.index_count; ++k)
                        sub_max_idx = std::max(sub_max_idx, mesh->indices[sm.first_index + k]);

                    LOG(info, "  submesh[{}]: first={} count={} max_vertex_idx={}{}",
                        si, sm.first_index, sm.index_count, sub_max_idx,
                        (sub_max_idx >= n_vtx ? "  <-- OOB VERTEX" : ""));
                }
            }
        #endif

        const bool grew = reserve_mesh_space(
            static_cast<u32>(mesh->vertices.size()),
            static_cast<u32>(mesh->indices.size()),
            live_submeshes);
        
        // If buffers moved, every existing BLAS references a dead address — rebuild them all once, right here, not per-mesh
        // This is the only path that touches other meshes
        if (grew)
            rebuild_all_blases();

        u32 slot_idx;
        if (!m_free_slots.empty()) {                                // claim a slot
        
            slot_idx = m_free_slots.back();
            m_free_slots.pop_back();
        
        } else {
        
            slot_idx = static_cast<u32>(m_mesh_slots.size());
            m_mesh_slots.emplace_back();
        }

        mesh_slot& slot = m_mesh_slots[slot_idx];
        slot.asset = handle;
        slot.vertex_offset = m_vertex_used;
        slot.vertex_count = static_cast<u32>(mesh->vertices.size());
        slot.index_offset = m_index_used;
        slot.index_count = static_cast<u32>(mesh->indices.size());
        slot.material_offset = m_material_used;
        slot.material_count = live_submeshes;
        slot.alive = true;

        upload_mesh_slice(slot, *mesh);                             // Upload just this mesh's slice
        m_vertex_used += slot.vertex_count;
        m_index_used += slot.index_count;
        m_material_used += slot.material_count;

        build_blas_for_slot(slot);                                  // Build exactly one BLAS for this mesh
        m_tlas_dirty = true;                                        // TLAS contents changed; the handle did not

        return true;
    }


    void renderer::unload_mesh(GLT::asset::handle handle) {

        u32 slot_idx = std::numeric_limits<u32>::max();
        for (u32 i = 0; i < m_mesh_slots.size(); ++i)
            if (m_mesh_slots[i].alive && m_mesh_slots[i].asset == handle) {
                
                slot_idx = i;
                break;
            }

        VALIDATE(slot_idx != std::numeric_limits<u32>::max(), return, "", "handle not loaded");

        mesh_slot& slot = m_mesh_slots[slot_idx];
        slot.alive = false;

        // BLAS destroy must not race in-flight frames. Easiest correct thing: wait for the GPU, then destroy. Load/unload is not a hot path.
        m_device.waitIdle();
        m_vr_dev->destroy_blas(slot.blas);
        slot.blas = {};

        // Leave the vertex/index/material ranges as holes for now. Adding a per-buffer free list is a follow-up if this ever matters.
        m_free_slots.push_back(slot_idx);
        m_tlas_dirty = true;

        // Drop the asset registry reference so the CPU-side mesh can be evicted too.
        if (auto reg = GLT::asset::registry::get_ref())
            reg->unload(handle);
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


        // vk::SamplerCreateInfo sampler_info{};
        // sampler_info.magFilter = vk::Filter::eNearest;
        // sampler_info.minFilter = vk::Filter::eNearest;
        // sampler_info.mipmapMode = vk::SamplerMipmapMode::eNearest;
        // sampler_info.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        // sampler_info.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        // sampler_info.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        // sampler_info.anisotropyEnable = VK_FALSE;
        // sampler_info.maxAnisotropy = 1.0f;
        // sampler_info.borderColor = vk::BorderColor::eFloatOpaqueBlack;
        // sampler_info.unnormalizedCoordinates = VK_FALSE;
        // sampler_info.compareEnable = VK_FALSE;
        // sampler_info.compareOp = vk::CompareOp::eAlways;
        // sampler_info.mipLodBias = 0.0f;
        // sampler_info.minLod = 0.0f;
        // sampler_info.maxLod = 0.0f;
        // m_default_sampler_nearest = m_device.createSampler(sampler_info);

        // sampler_info.magFilter = vk::Filter::eLinear;
        // sampler_info.minFilter = vk::Filter::eLinear;
        // m_default_sampler_linear = m_device.createSampler(sampler_info);

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
        // create_rt_pipeline(), so it must never move. Only its *contents* are rebuilt as the scene changes (rebuild_tlas_if_dirty).
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

        // Load the initial meshes through the asset registry ----------------------------------------------------------
        // Each call: claims a slot, uploads its slice, builds one BLAS, marks the TLAS dirty. Existing meshes are never touched.
        // This is the same code path that runtime callers hit.
        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "Failed to get asset registry");

        const auto content_dir = std::filesystem::path(PROJECT_CONTENT_DIR);
        const std::filesystem::path mesh_paths[] = {
            content_dir / "mesh" / "stealth_ship.glt_mesh",
        };

        for (const auto& path : mesh_paths) {

            ASSERT(load_mesh(path), "", "Failed to load initial mesh [{}]", path.generic_string());
        }

        // Populate the TLAS with whatever we just loaded --------------------------------------------------------------
        // The scene is now "ready" once create() returns, instead of waiting for the first begin_frame().
        rebuild_tlas_if_dirty();

        // Deferred cleanup --------------------------------------------------------------------------------------------
        // BLASes now live on the slots, so walk those. Buffers and the TLAS are renderer-owned and go straight into the deletion queue.
        m_deletion_queue.push_func([&]() {

            for (auto& slot : m_mesh_slots) {
                if (slot.alive && slot.blas.buffer.buffer)
                    m_vr_dev->destroy_blas(slot.blas);
            }
            m_mesh_slots.clear();

            if (m_vertex_buffer.buffer)        m_vr_dev->destroy_buffer(m_vertex_buffer);
            if (m_index_buffer.buffer)         m_vr_dev->destroy_buffer(m_index_buffer);
            if (m_material_buffer.buffer)      m_vr_dev->destroy_buffer(m_material_buffer);
            if (m_tlas_instance_buffer.buffer) m_vr_dev->destroy_buffer(m_tlas_instance_buffer);

            m_vr_dev->destroy_tlas(m_tlas_handle);
        });
    }


    void renderer::create_rt_pipeline() {

        // [POI]
        // Now we create a descriptor layout for the ray tracing pipeline
        // last parameter is a pointer to the items vector, so we can use it later to create the descriptor set
        // for now we have only one item, so we just pass the address of the first element
        // if we want to update the descriptor set later with another item,
        // we can just reassign the vr::descriptor_item::pItems with new items and update the descriptor set
        m_resource_bindings = {
            vr::descriptor_item(0, vk::DescriptorType::eAccelerationStructureKHR,
                vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR, 1, &m_tlas_handle.buffer.dev_address), // <-- extended
            vr::descriptor_item(1, vk::DescriptorType::eUniformBuffer,
                vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR, 1, &m_uniform_buffer),
            vr::descriptor_item(2, vk::DescriptorType::eStorageImage,
                vk::ShaderStageFlagBits::eRaygenKHR, 10, &m_output_image->get_accessible_image_ref(), 1),
            vr::descriptor_item(3, vk::DescriptorType::eStorageBuffer,
                vk::ShaderStageFlagBits::eClosestHitKHR, 1, &m_material_buffer),
            vr::descriptor_item(4, vk::DescriptorType::eStorageBuffer,
                vk::ShaderStageFlagBits::eClosestHitKHR, 1, &m_vertex_buffer),
            vr::descriptor_item(5, vk::DescriptorType::eStorageBuffer,
                vk::ShaderStageFlagBits::eClosestHitKHR, 1, &m_index_buffer),
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

        // AO hit group — writes 1.0 (occluded)
        auto ao_hit_spv = m_shader_compiler.compile_glsl_to_spirv(shader_dir / "mesh_ao.rchit.glsl");
        ASSERT(!ao_hit_spv.empty(), "", "Failed to load shader")
        auto ao_hit_shader_module = m_vr_dev->create_shader_from_spv(ao_hit_spv);

        // AO miss — writes 0.0 (unoccluded)
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
        pipeline_settings.max_recursion_depth = 2;                  // primary + AO
        pipeline_settings.max_payload_size = sizeof(glm::vec3);
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

        // create a descriptor buffer for the ray tracing pipeline
        m_resource_desc_buffer = m_vr_dev->create_descriptor_buffer(m_resource_descriptor_layout, m_resource_bindings, vr::descriptor_buffer_type::resource);

        m_live_buffer_count += 1;           // m_resource_desc_buffer.buffer
        m_live_pipeline_count += 1;         // m_rt_pipeline
        m_live_descriptor_set_count += 1;   // one set inside m_resource_desc_buffer

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
        });
    }


    void renderer::update_descriptor_set() {

        // Called from two places:
        //   1. reserve_mesh_space() when the shared buffers move — m_resource_desc_buffer already exists by then, we're just refreshing the resource handles.
        //   2. During init via reserve_mesh_space(), *before* create_rt_pipeline() has built m_resource_desc_buffer. 
        //      Skip — create_rt_pipeline() will pick up the current m_resource_bindings state when it builds the descriptor buffer.
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


    void renderer::clear_output_image(vk::CommandBuffer cmd, const glm::vec4& color) {

        // Clear the image
        vk::ClearColorValue clear_color;
        clear_color.setFloat32({color.r, color.g, color.b, color.a});
        cmd.clearColorImage(
            m_swapchain.swapchain_images[m_current_swapchain_image],
            vk::ImageLayout::eTransferDstOptimal,
            clear_color,
            vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
        );
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
        // m_output_image->resize(glm::uvec3{m_swapchain.swapchain_extent.width, m_swapchain.swapchain_extent.height, 1});

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

        vk::FenceCreateInfo fence_CI = vk::FenceCreateInfo()
            .setFlags(vk::FenceCreateFlagBits::eSignaled);
		m_immediate_submit_fence = m_device.createFence(fence_CI);

        // m_deletion_queue.push_pointer(m_immediate_submit_command_pool);
		// m_deletion_queue.push_pointer(m_immediate_submit_fence);

        m_output_image = GLT::create_ref<image>();

        // create a uniform buffer
        u32 uniform_buffer_size = sizeof(f32) * 4 * 4 * 2; // two 4x4 matrix
        uniform_buffer_size += sizeof(f32) * 4;            // pass time, and 3 floats for padding or whatever else in the future

        // we will be writing to this buffer on the CPU
        m_uniform_buffer = m_vr_dev->create_buffer(
            sizeof(camera_ubo),
            vk::BufferUsageFlagBits::eUniformBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count += 1;

        m_deletion_queue.push_func([&]() {

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
                    vk::ImageAspectFlagBits::eColor,                // Color aspect
                    0,                                              // Base mip level
                    1,                                              // Level count
                    0,                                              // Base array layer
                    1                                               // Layer count
                );

                m_vr_dev->transition_image_layout(
                    command_buffer,
                    m_swapchain.swapchain_images[m_current_swapchain_image],
                    m_swapchain_images_layout[m_current_swapchain_image],
                    new_layout,
                    swapchain_range,                                // Image subresource range
                    vk::PipelineStageFlagBits::eAllCommands,        // Source stage
                    vk::PipelineStageFlagBits::eAllCommands         // Destination stage
                );
                m_swapchain_images_layout[m_current_swapchain_image] = new_layout;

            } break;

            case image_type::render: {

                // Define the image subresource range for render images
                vk::ImageSubresourceRange render_range(
                    vk::ImageAspectFlagBits::eColor,                // Color aspect
                    0,                                              // Base mip level
                    1,                                              // Level count
                    0,                                              // Base array layer
                    1                                               // Layer count
                );

                m_vr_dev->transition_image_layout(
                    command_buffer,
                    m_output_image->get_allocated_image_ref().image,
                    m_output_image->get_accessible_image_ref().layout,
                    new_layout,
                    render_range,                                   // Image subresource range
                    vk::PipelineStageFlagBits::eAllCommands ,       // Source stage
                    vk::PipelineStageFlagBits::eAllCommands         // Destination stage
                );
                m_output_image->get_accessible_image_ref().layout = new_layout;

            } break;
        }
    }

    // ----- IMGUI -----------------------------------------------------------------------------------------------------

    void renderer::imgui_init() {

        ImGui::SetCurrentContext(imgui_config::get_context_imgui());
        // ImGuiIO& io = ImGui::GetIO();
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
        // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
        // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
        // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows

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
        LOG(trace, "ImGui shutdown");
    }


    void renderer::create_imgui_resources() {

        utils::create_imgui_resources(m_imgui_descriptor_pool, m_device, m_swapchain, m_imgui_render_pass,
            m_instance, m_physical_device, m_queues, m_imgui_framebuffers, m_imgui_initialized);
    }


    void renderer::destroy_imgui_resources() {

        utils::destroy_imgui_resources(m_device, m_imgui_framebuffers, m_imgui_render_pass, 
            m_imgui_descriptor_pool, m_imgui_initialized);
    }


    void renderer::begin_imgui_frame(vk::CommandBuffer& current_cmd) {

        VALIDATE_INIT

        // Transition the swapchain image to COLOR_ATTACHMENT_OPTIMAL for ImGui
        transition_image_layout(current_cmd, image_type::swapchain, vk::ImageLayout::eColorAttachmentOptimal);

        ImGui::SetCurrentContext(imgui_config::get_context_imgui());
        ImGui_ImplVulkan_NewFrame();                                                    // Start ImGui frame (no command buffer needed)
        mp_window->begin_imgui_frame();
        ImGui::NewFrame();
    }


    void renderer::end_imgui_frame(vk::CommandBuffer& current_cmd) {

        VALIDATE_INIT

        ImGui::EndFrame();
        ImGui::Render();                                                                // Finalize ImGui draw data

        // ---- stats ------------------------------------------------------------------------
        m_frame_draw_calls    += count_imgui_draw_calls(ImGui::GetDrawData());
        m_frame_render_passes += 1;     // ImGui uses one render pass

        vk::RenderPassBeginInfo rp_info{};                                              // Begin render pass (clears background to dark blue)
        rp_info.renderPass = m_imgui_render_pass;
        rp_info.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
        rp_info.renderArea.offset = vk::Offset2D{};
        rp_info.renderArea.extent = m_swapchain.swapchain_extent;

        std::array<vk::ClearValue, 1> clear_values{};                                   // Clear colour (dark blue)
        clear_values[0].color = {m_clear_color.x, m_clear_color.y, m_clear_color.z, m_clear_color.w};
        rp_info.clearValueCount = static_cast<u32>(clear_values.size());
        rp_info.pClearValues = clear_values.data();

        current_cmd.beginRenderPass(rp_info, vk::SubpassContents::eInline);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), current_cmd);
        current_cmd.endRenderPass();

        // After the render pass, the image layout is PRESENT_SRC_KHR (set in render pass)
        m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;
        
        // Update and Render additional Platform Windows
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    // mesh handling ---------------------------------------------------------------------------------------------------

    bool renderer::reserve_mesh_space(u32 v, u32 i, u32 m) {

        const u32 need_v = m_vertex_used + v;
        const u32 need_i = m_index_used + i;
        const u32 need_m = m_material_used + m;

        bool grew = false;

        if (need_v > m_vertex_capacity) {
            m_vertex_capacity = std::max(need_v, m_vertex_capacity + std::max(m_vertex_capacity / 2, VERTEX_HEADROOM_MIN));
            grew = true;
        }
        if (need_i > m_index_capacity) {
            m_index_capacity  = std::max(need_i, m_index_capacity + std::max(m_index_capacity / 2, INDEX_HEADROOM_MIN));
            grew = true;
        }
        if (need_m > m_material_capacity) {
            m_material_capacity = std::max(need_m, m_material_capacity + std::max(m_material_capacity / 2, MATERIAL_HEADROOM_MIN));
            grew = true;
        }
        if (!grew) return false;

        // Grow: create new buffers, copy old, then swap. Because device addresses change, every BLAS that reads them is invalid — caller must rebuild them.
        const auto as_input = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer;

        auto new_vb = m_vr_dev->create_buffer(m_vertex_capacity * sizeof(GLT::asset::mesh::vertex), as_input,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        auto new_ib = m_vr_dev->create_buffer(m_index_capacity * sizeof(u32), as_input,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        auto new_mb = m_vr_dev->create_buffer(m_material_capacity * sizeof(gpu_material), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        std::memset(m_vr_dev->map_buffer(new_vb), 0, m_vertex_capacity * sizeof(GLT::asset::mesh::vertex));
        std::memset(m_vr_dev->map_buffer(new_ib), 0, m_index_capacity  * sizeof(u32));
        std::memset(m_vr_dev->map_buffer(new_mb), 0, m_material_capacity * sizeof(gpu_material));
        m_vr_dev->unmap_buffer(new_vb);
        m_vr_dev->unmap_buffer(new_ib);
        m_vr_dev->unmap_buffer(new_mb);

        // Copy the used region from old to new. Only touch a buffer if it has data —
        // an unpopulated slot (e.g. right after the initial pre-reservation) was never mapped, so calling unmap on it would trip VMA's assertion.
        if (m_vertex_used) {
            const void* src = m_vr_dev->map_buffer(m_vertex_buffer);
            void*       dst = m_vr_dev->map_buffer(new_vb);
            std::memcpy(dst, src, m_vertex_used * sizeof(GLT::asset::mesh::vertex));
            m_vr_dev->unmap_buffer(m_vertex_buffer);
            m_vr_dev->unmap_buffer(new_vb);
        }

        if (m_index_used) {
            const void* src = m_vr_dev->map_buffer(m_index_buffer);
            void*       dst = m_vr_dev->map_buffer(new_ib);
            std::memcpy(dst, src, m_index_used * sizeof(u32));
            m_vr_dev->unmap_buffer(m_index_buffer);
            m_vr_dev->unmap_buffer(new_ib);
        }

        if (m_material_used) {
            const void* src = m_vr_dev->map_buffer(m_material_buffer);
            void*       dst = m_vr_dev->map_buffer(new_mb);
            std::memcpy(dst, src, m_material_used * sizeof(gpu_material));
            m_vr_dev->unmap_buffer(m_material_buffer);
            m_vr_dev->unmap_buffer(new_mb);
        }

        std::swap(m_vertex_buffer, new_vb);
        std::swap(m_index_buffer, new_ib);
        std::swap(m_material_buffer, new_mb);

        m_device.waitIdle();

        // After the swap, new_* hold the *old* buffers (possibly default-constructed).
        if (new_vb.buffer) m_vr_dev->destroy_buffer(new_vb);
        if (new_ib.buffer) m_vr_dev->destroy_buffer(new_ib);
        if (new_mb.buffer) m_vr_dev->destroy_buffer(new_mb);

        update_descriptor_set();
        return true;
    }


    void renderer::rebuild_tlas_if_dirty() {

        if (!m_tlas_dirty)
            return;
        m_tlas_dirty = false;

        m_tlas_instances.clear();
        m_tlas_instances.reserve(m_mesh_slots.size());

        for (const auto& slot : m_mesh_slots) {

            if (!slot.alive)
                continue;

            // layout (whatever you want, e.g. layout N by 2.5 units along X)
            const f32 x = (static_cast<f32>(m_tlas_instances.size()) - 0.5f * (static_cast<f32>(m_mesh_slots.size()) - 1.0f)) * 2.5f;
            VkTransformMatrixKHR xform = { 1,0,0,x,  0,1,0,0,  0,0,1,0 };

            m_tlas_instances.push_back(
                vk::AccelerationStructureInstanceKHR()
                    .setTransform(xform)
                    .setInstanceCustomIndex(slot.material_offset)
                    .setAccelerationStructureReference(slot.blas.buffer.dev_address)
                    .setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
                    .setMask(0xFF)
                    .setInstanceShaderBindingTableRecordOffset(0));
        }

        if (!m_tlas_instances.empty()) {
            m_vr_dev->update_buffer(m_tlas_instance_buffer, m_tlas_instances.data(),
                sizeof(vk::AccelerationStructureInstanceKHR) * m_tlas_instances.size());
        }

        auto scratch = m_vr_dev->create_scratch_buffer_from_build_info(m_tlas_build_info);
        auto cmd = m_device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(m_graphics_pool, vk::CommandBufferLevel::ePrimary, 1))[0];
        cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        m_vr_dev->build_tlas(m_tlas_build_info, m_tlas_instance_buffer, static_cast<u32>(m_tlas_instances.size()), cmd);
        cmd.end();

        auto si = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&cmd);
        m_queues.graphics_queue.submit(si, nullptr);
        m_device.waitIdle();

        m_vr_dev->destroy_buffer(scratch);
        m_device.freeCommandBuffers(m_graphics_pool, cmd);
    }


    void renderer::build_blas_for_slot(mesh_slot& slot) {

        auto* mesh = GLT::asset::registry::get_ref()->data_as<GLT::asset::mesh::mesh_asset>(slot.asset);
        ASSERT(mesh, "", "slot has no mesh");

        vr::blas_create_info bci{};
        bci.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        const vk::DeviceAddress base_vertex_addr = m_vertex_buffer.dev_address + slot.vertex_offset * sizeof(GLT::asset::mesh::vertex);
        const vk::DeviceAddress base_index_addr = m_index_buffer.dev_address  + slot.index_offset  * sizeof(u32);

        for (const auto& sm : mesh->submeshes) {
            if (sm.index_count < 3)
                continue;

            vr::geometry_data gd{};
            gd.vertex_format = vk::Format::eR32G32B32Sfloat;
            gd.stride = sizeof(GLT::asset::mesh::vertex);
            gd.index_format = vk::IndexType::eUint32;
            gd.primitive_count = sm.index_count / 3;
            gd.data_addresses.vertex_dev_address = base_vertex_addr;
            gd.data_addresses.index_dev_address  = base_index_addr + sm.first_index * sizeof(u32);
            bci.geometries.push_back(gd);
        }

        auto [handle, build_info] = m_vr_dev->create_blas(bci);
        slot.blas = handle;

        std::vector<vr::blas_build_info> infos = { build_info };
        auto scratch = m_vr_dev->create_scratch_buffer_from_build_infos(infos);
        auto cmd = m_device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(m_graphics_pool, vk::CommandBufferLevel::ePrimary, 1))[0];
        
        cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        m_vr_dev->build_blas(infos, cmd);
        m_vr_dev->add_acceleration_build_barrier(cmd);
        cmd.end();

        auto si = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&cmd);
        m_queues.graphics_queue.submit(si, nullptr);
        m_device.waitIdle();

        m_vr_dev->destroy_buffer(scratch);
        m_device.freeCommandBuffers(m_graphics_pool, cmd);
    }


    void renderer::upload_mesh_slice(mesh_slot& slot, GLT::asset::mesh::mesh_asset& mesh) {

        // Vertices
        {
            auto* dst = static_cast<GLT::asset::mesh::vertex*>(m_vr_dev->map_buffer(m_vertex_buffer));
            std::memcpy(dst + slot.vertex_offset, mesh.vertices.data(), mesh.vertices.size() * sizeof(GLT::asset::mesh::vertex));
            m_vr_dev->unmap_buffer(m_vertex_buffer);
        }

        // Indices
        {
            auto* dst = static_cast<u32*>(m_vr_dev->map_buffer(m_index_buffer));
            std::memcpy(dst + slot.index_offset, mesh.indices.data(), mesh.indices.size() * sizeof(u32));
            m_vr_dev->unmap_buffer(m_index_buffer);
        }

        // Materials — one entry per submesh.
        {
            auto* dst = static_cast<gpu_material*>(m_vr_dev->map_buffer(m_material_buffer));

            u32 mat_off = slot.material_offset;
            for (const auto& sm : mesh.submeshes) {
                if (sm.index_count < 3)
                    continue;                 // must match build_blas_for_slot

                const f32 t = static_cast<f32>(mat_off) / static_cast<f32>(std::max(1u, m_material_used + slot.material_count));

                gpu_material mat{};
                mat.base_color = glm::vec4(
                    0.5f + 0.5f * std::cos(6.2831853f * (t + 0.00f)),
                    0.5f + 0.5f * std::cos(6.2831853f * (t + 0.33f)),
                    0.5f + 0.5f * std::cos(6.2831853f * (t + 0.67f)),
                    1.0f);
                mat.roughness   = 0.6f;
                mat.metallic    = 0.0f;
                mat.vertex_base = slot.vertex_offset;
                mat.index_base  = slot.index_offset + sm.first_index;

                dst[mat_off++] = mat;
            }
            m_vr_dev->unmap_buffer(m_material_buffer);
        }
    }


    void renderer::rebuild_all_blases() {

        // Called after the shared vertex/index/material buffers moved. Every BLAS
        // baked a device address into its build; they are all stale now.
        m_device.waitIdle();

        for (auto& slot : m_mesh_slots) {

            if (!slot.alive)
                continue;

            if (slot.blas.buffer.buffer)          // guard against a default-constructed handle
                m_vr_dev->destroy_blas(slot.blas);
                
            slot.blas = {};
            build_blas_for_slot(slot);
        }
    }

}


#include "util/pch.h"
#include "util/util_pch.h"
#include "scene/scene_renderer.h"

#include <platform/i_window.h>

#include "plugin/plugin.h"
#include "plugin/image.h"
#include "shared/shared_resource_manager.h"
#include "scene/scene_renderer_debug_draw.inl"
#include "scene/scene_renderer_imgui.inl"

#include "modes.h"

#include <debug/trace.h>
#include <glm/packing.hpp>
#if defined(GLT_TRACY_ENABLED)
    #include <tracy/TracyVulkan.hpp>
#endif



namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // The call ALWAYS runs; only the result check is debug-only. (The release variant used to expand to nothing, which
    // silently removed the frame-fence wait: CPU raced the GPU, reset in-flight command buffers -> black, frozen window)
    #if defined(DEBUG)
        #define VK_SR_CHECK(expr)  ASSERT_S((expr) == vk::Result::eSuccess)
    #else
        #define VK_SR_CHECK(expr)  ((void)(expr))
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static FORCE_INLINE void extract_frustum_planes(const glm::mat4& view_projection, glm::vec4 (&planes)[6]);

    // 64-bit FNV-1a style hash over a block of bytes, 8 at a time (shadow cache keys)
    static u64 shadow_hash_bytes(const void* data, size_t bytes, u64 hash);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static FORCE_INLINE void extract_frustum_planes(const glm::mat4& view_projection, glm::vec4 (&planes)[6]) {

        const glm::mat4 t = glm::transpose(view_projection);
        planes[0] = t[3] + t[0];
        planes[1] = t[3] - t[0];
        planes[2] = t[3] + t[1];
        planes[3] = t[3] - t[1];
        planes[4] = t[3] + t[2];
        planes[5] = t[3] - t[2];

        for (auto& p : planes) {
            const f32 len = glm::length(glm::vec3(p));
            if (len > 0.f) p /= len;
        }
    }


    static u64 shadow_hash_bytes(const void* data, const size_t bytes, u64 hash) {

        const auto* cursor = static_cast<const unsigned char*>(data);
        size_t index = 0;
        for (; index + 8 <= bytes; index += 8) {
            u64 word;
            std::memcpy(&word, cursor + index, 8);
            hash = (hash ^ word) * 0x100000001b3ull;
            hash ^= hash >> 29;
        }
        for (; index < bytes; ++index)
            hash = (hash ^ cursor[index]) * 0x100000001b3ull;
        return hash;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    scene_renderer::scene_renderer(renderer& plugin, const GLT::render::scene_renderer_desc& desc)
        : m_plugin(plugin), m_shared(plugin.get_shared()), m_desc(desc) {


        auto device = m_plugin.get_vk_device();

        if (desc.clear_color.has_value())
            m_clear_color = *desc.clear_color;

        if (std::holds_alternative<GLT::render::surface_target_desc>(desc.target)) {

            m_offscreen = false;
            const auto& surface = std::get<GLT::render::surface_target_desc>(desc.target);
            m_window = surface.window;
            init_surface_target(surface);

        } else {

            m_offscreen = true;
            init_offscreen_target(std::get<GLT::render::offscreen_target_desc>(desc.target));
        }

        vk::CommandPoolCreateInfo cpi{};
        cpi.queueFamilyIndex = m_plugin.get_queues_ref().graphics_index;
        cpi.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        m_graphics_pool = device.createCommandPool(cpi);

        init_frames();
        m_graph = GLT::create_unique_ref<graph::render_graph>(m_plugin.get_dev());
        m_graph->set_retire_callback([shared = m_shared](std::function<void()>&& fn) { shared->retire(std::move(fn)); });
        create_timestamp_queries();
        create_gpu_profiler();
        m_graph->set_gpu_profiler(m_gpu_profiler);
        init_gpu_state();

        if (!m_offscreen && m_desc.enable_imgui)
            imgui_init();

        LOG(trace, "scene_renderer created ({}x{}, offscreen={}, window=[{},{}])",
            m_extent.x, m_extent.y, m_offscreen, m_window.index, m_window.generation);
    }


    scene_renderer::~scene_renderer() { destroy_internal(); }

    // CLASS PUBLIC ====================================================================================================

    std::expected<GLT::unique_ref<scene_renderer>, GLT::render::scene_renderer_error> scene_renderer::create(renderer& plugin,
        const GLT::render::scene_renderer_desc& desc) {
        GLT_PROFILE_SCOPE("scene_renderer::create");

        // Validate the surface target. Any window the platform plugin confirms as live is acceptable;
        // the renderer will happily create a swapchain for it (there is no longer a "only the plugin's
        // own window" restriction — that was a v1 limitation).
        if (std::holds_alternative<GLT::render::surface_target_desc>(desc.target)) {

            const auto& surface = std::get<GLT::render::surface_target_desc>(desc.target);

            if (!surface.window.is_valid()) {
                return std::unexpected(GLT::render::scene_renderer_error{
                    GLT::render::scene_renderer_error::code::target_invalid,
                    "surface target did not specify a window handle"
                });
            }

            auto* window = plugin.get_window();
            if (!window || !window->is_valid(surface.window)) {
                return std::unexpected(GLT::render::scene_renderer_error{
                    GLT::render::scene_renderer_error::code::target_invalid,
                    "surface target references a window that does not exist"
                });
            }
        }

        try {

            return GLT::unique_ref<scene_renderer>(new scene_renderer(plugin, desc));

        } catch (const std::exception& e) {
            return std::unexpected(GLT::render::scene_renderer_error{
                GLT::render::scene_renderer_error::code::backend,
                std::format("scene renderer create failed: {}", e.what())
            });
        }
    }


    void scene_renderer::set_active_camera(const GLT::world::camera_snapshot& camera) { m_active_camera = camera; }


    void scene_renderer::update(f32 delta_time) {
        GLT_PROFILE_SCOPE("scene_renderer::update");

        if (!m_active && m_offscreen)       // Offscreen scene renderers don't own ImGui and don't need to run when inactive
            return;

        GLT_PROFILE_SCOPE_C("Scene renderer update", GLT::debug::trace::COLOR_RENDER);

        m_delta_time = delta_time;
        {
            // Blocks until the GPU finished the frame that last used this slot (MAX_CONCURRENT_FRAMES ago): GPU-bound time
            GLT_PROFILE_WAIT("Wait for GPU (frame fence)");
            VK_SR_CHECK(m_plugin.get_vk_device().waitForFences(m_frames[m_current_frame].fence, VK_TRUE, UINT64_MAX));
        }
        resolve_gpu_timestamps(m_current_frame);    // this slot's previous frame is complete -> its queries are available

        if (!m_offscreen)
            sync_swapchain_to_window();             // before the ImGui frame opens and before camera/cluster data use m_extent

        if (m_imgui_initialized)            // Surface target: ImGui frame MUST open every frame
            begin_imgui_frame();

        if (!m_active)                      // Surface target, inactive: keep ImGui, skip 3D CPU work
            return;

        apply_visual_settings();
        ensure_scene_meshes();
        build_instance_buffer();
        update_shadow_casters();
        update_lights();
        update_shadow();
        update_camera_ubo();
        prepare_translucent_draws();
        update_globals();
        write_scene_material_descriptors();
        process_debug_primitives();
    }


    std::expected<void, GLT::render::render_error> scene_renderer::render(u64 /*frame_number*/) {
        GLT_PROFILE_SCOPE("scene_renderer::render");

        if (!m_active && m_offscreen)       // Offscreen inactive: nothing to do, leave fence + frame slot alone
            return {};

        GLT_PROFILE_SCOPE_C("Scene renderer render", GLT::debug::trace::COLOR_RENDER);
        auto device = m_plugin.get_vk_device();
        const auto& queues = m_plugin.get_queues_ref();
        auto& frame = m_frames[m_current_frame];

        if (!m_offscreen) {

            // Resizes are handled in update() (sync_swapchain_to_window). This is only a safety net for a size change between
            // update() and render() (e.g. events pumped by a layer in between) and for the minimized case
            if (auto* window = m_plugin.get_window(); window && window->is_valid(m_window)) {

                const auto fb = window->get_framebuffer_size(m_window);
                if (fb.x <= 0 || fb.y <= 0) {                   // minimized: nothing to render, keep the fence signalled
                    abort_imgui_frame();
                    return {};
                }

                if (static_cast<u32>(fb.x) != m_swapchain_builder.width || static_cast<u32>(fb.y) != m_swapchain_builder.height) {
                    abort_imgui_frame();
                    return std::unexpected(GLT::render::render_error{
                        GLT::render::render_error::code::target_out_of_date,
                        "window size changed"
                    });
                }
            }

            try {

                GLT_PROFILE_WAIT("Acquire swapchain image");
                auto r = device.acquireNextImageKHR(m_swapchain.swapchain_handle, UINT64_MAX, frame.image_available, nullptr);
                m_current_swapchain_image = r.value;

            } catch (const vk::OutOfDateKHRError&) {

                m_swapchain_out_of_date = true;
                abort_imgui_frame();
                return std::unexpected(GLT::render::render_error{
                    GLT::render::render_error::code::target_out_of_date,
                    "swapchain out of date"
                });
            }
        }

        // Reset only once we know we will submit: an early return above must leave the fence signalled, otherwise the next
        // update() blocks forever in waitForFences
        {
            GLT_PROFILE_SCOPE("Reset fence + begin command buffer");
            device.resetFences(frame.fence);
            frame.cmd.reset();
            frame.cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        }

        // LOG(trace, "render: window=[{},{}] active={} instances={} lights={}",
        //     m_window.index, m_window.generation, m_active, m_instance_count, m_light_count)

        m_graph->begin_frame();
        update_cluster_dimensions();
        update_light_meta_ubo();
        import_frame_textures();
        ensure_environment_textures();
        update_environment();
        update_light_cull_descriptors();
        update_mesh_descriptors();
        ensure_draw_buffers();                  // grow indirect buffers to this frame's draw count (before descriptors)
        ensure_virtual_geometry_buffers();
        update_cull_descriptors();
        update_virtual_geometry_descriptors();

        m_imgui_clears_backbuffer = false;
        if (m_active) {

            add_environment_passes();           // sky cubemap, specular prefilter, SH (only when the sun / sky changed)
            add_reset_draw_count_pass();
            add_virtual_geometry_cull_pass();   // cluster selection for meshes on the virtual geometry path (no-op without any)
            add_cull_pass();

            // Shadow caching: update_shadow() / update_local_shadows() already planned which cascades of the sun and
            // which atlas tiles of the local lights are out of date. Here: was one of the atlas images replaced?
            bool render_sun = false;
            bool clear_sun_atlas = false;
            bool render_local = false;
            bool clear_atlas = false;
            {
                GLT_PROFILE_SCOPE("Shadow cache check");

                if (m_shadow.active) {
                    const vk::Image image = m_graph->texture_image(m_shadow_map);
                    clear_sun_atlas = image != m_dir_shadow_image;  // new image: only what this frame draws is in it
                    m_dir_shadow_image = image;
                    if (clear_sun_atlas)
                        m_sun_shadows.atlas_lost();
                    render_sun = clear_sun_atlas || !m_sun_shadow_tiles.empty();
                }
                if (m_local_shadow_view_count > 0) {
                    const vk::Image image = m_graph->texture_image(m_shadow_atlas);
                    clear_atlas = image != m_local_shadow_image;    // new image: only what this frame draws is in it
                    m_local_shadow_image = image;
                    if (clear_atlas)
                        m_local_shadows.atlas_lost();
                    render_local = clear_atlas || !m_local_shadow_tiles.empty();
                }
            }

            if (render_sun)
                add_sun_shadow_passes(clear_sun_atlas);
            if (render_local)
                add_local_shadow_passes(clear_atlas);
            m_sun_shadow_plan_pending = false;      // this frame's cascades and tiles are in the graph
            m_local_shadow_plan_pending = false;
            add_depth_prepass_pass();
            add_clear_light_grid_pass();
            add_light_culling_pass();
            add_scene_pass();
            if (!debug_view_active())
                add_sky_background_pass();      // sky + sun where no opaque geometry was drawn (debug views: flat background)
            add_translucent_pass();             // sorted, blended into scene_hdr after all opaque geometry
            if (m_active_mode == modes::Light_cull.id)
                add_light_cull_debug_pass();    // debug render mode: lights-per-cluster heatmap instead of the shaded image
            else
                add_tonemap_pass();             // HDR -> scene_color (display); debug draw goes on top, unaffected by exposure
            if (m_active_mode == modes::Tri_density.id)
                add_tri_density_passes();       // triangles-per-pixel heatmap blended over the flat grey scene
            if (virtual_geometry_debug_view() != 0u)
                add_virtual_geometry_debug_pass();  // clusters / levels / triangles / error instead of the shaded image
            add_debug_draw_pass();

            if (!m_offscreen)
                add_blit_to_backbuffer_pass();
            else
                add_output_to_shader_read_pass();
        } else {

            // Inactive surface (UI only, e.g. the editor's main window): the ImGui render pass clears the backbuffer itself
            // (loadOp CLEAR) instead of a separate full-screen clear pass + barrier
            m_imgui_clears_backbuffer = !m_offscreen && m_imgui_initialized && m_imgui_clear_render_pass;
            if (!m_offscreen && !m_imgui_clears_backbuffer)
                add_clear_backbuffer_pass();
        }

        if (m_imgui_initialized)
            add_imgui_pass();
        if (m_imgui_initialized)
            end_imgui_frame();                  // ImGui::Render() — pass consumes draw data at exec

        m_graph->compile();
        m_graph->set_timestamp_queries(m_timestamp_pool, m_current_frame * MAX_TIMESTAMPS_PER_FRAME, MAX_TIMESTAMPS_PER_FRAME);
        m_graph->execute(frame.cmd);
        m_timed_pass_names[m_current_frame] = m_graph->timed_pass_names();
        m_frame_views[m_current_frame] = { m_active_camera.position, m_active_mode, m_extent };
        m_timestamps_written[m_current_frame] = m_graph->timestamps_written();
        m_graph->end_frame();

        // if (m_dup_counter >= 300) {
        //     LOG(info, "{}", m_graph->dump())
        //     m_dup_counter = 0;
        // }
        // m_dup_counter++;

        if (m_offscreen)
            m_output_image->get_accessible_image_ref().layout = m_graph->texture_layout(m_scene_color);
        else
            m_swapchain_images_layout[m_current_swapchain_image] = vk::ImageLayout::ePresentSrcKHR;

        {
            GLT_PROFILE_SCOPE("End command buffer");
            frame.cmd.end();
        }

        // Submit
        vk::CommandBufferSubmitInfo cbsi{};
        cbsi.commandBuffer = frame.cmd;

        vk::SemaphoreSubmitInfo wait_si{}, signal_si{};
        if (!m_offscreen) {
            wait_si.semaphore = frame.image_available;
            wait_si.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            signal_si.semaphore = m_render_finished[m_current_swapchain_image];
            signal_si.stageMask = vk::PipelineStageFlagBits2::eAllCommands;
        }

        // Every submission signals the shared retire timeline so deferred destruction knows when this frame is done
        vk::SemaphoreSubmitInfo retire_si{};
        retire_si.semaphore = m_shared->retire_timeline();
        retire_si.value = m_shared->next_retire_value();
        retire_si.stageMask = vk::PipelineStageFlagBits2::eAllCommands;

        std::array<vk::SemaphoreSubmitInfo, 2> signal_infos{};
        u32 signal_count = 0;
        if (!m_offscreen)
            signal_infos[signal_count++] = signal_si;
        signal_infos[signal_count++] = retire_si;

        vk::SubmitInfo2 si{};
        si.commandBufferInfoCount = 1;
        si.pCommandBufferInfos = &cbsi;
        if (!m_offscreen) {
            si.waitSemaphoreInfoCount = 1;
            si.pWaitSemaphoreInfos = &wait_si;
        }
        si.signalSemaphoreInfoCount = signal_count;
        si.pSignalSemaphoreInfos = signal_infos.data();

        try {

            GLT_PROFILE_SCOPE("Queue submit");
            queues.graphics_queue.submit2(si, frame.fence);

        } catch (const std::exception& e) {
            render_imgui_platform_windows();            // ImGui requires UpdatePlatformWindows() every frame
            return std::unexpected(GLT::render::render_error{
                GLT::render::render_error::code::backend, 
                std::format("submit failed: {}", e.what())
            });
        }

        if (!m_offscreen) {
            vk::PresentInfoKHR pi{};
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &m_render_finished[m_current_swapchain_image];
            pi.swapchainCount = 1;
            pi.pSwapchains = &m_swapchain.swapchain_handle;
            pi.pImageIndices = &m_current_swapchain_image;
            try {

                // FIFO (vsync) blocks here (or in the next acquire) once the swapchain queue is full
                GLT_PROFILE_WAIT("Present (vsync)");
                (void)queues.present_queue.presentKHR(pi);

            } catch (const vk::OutOfDateKHRError&) {
                m_swapchain_out_of_date = true;
                m_frame_counter++;                                              // the frame WAS submitted: advance the slot
                m_current_frame = (m_current_frame + 1) % MAX_CONCURRENT_FRAMES;
                render_imgui_platform_windows();
                return std::unexpected(GLT::render::render_error{
                    GLT::render::render_error::code::target_out_of_date,
                    "present out of date"
                });
            }
        }

        m_frame_counter++;
        m_current_frame = (m_current_frame + 1) % MAX_CONCURRENT_FRAMES;

        render_imgui_platform_windows();                // detached ImGui windows, after this window's own present
        return {};
    }


    // Wayland never returns VK_ERROR_OUT_OF_DATE_KHR on resize (the swapchain extent defines the surface size there) and
    // some X11 drivers only report SUBOPTIMAL, so the acquire/present errors alone leave the swapchain at its old size.
    // Polled once per frame in update(), before the ImGui frame opens, so a resize never has to abort a frame.
    // resize() compares against the size we last *requested*, so a driver that clamps the extent can't cause a rebuild loop
    void scene_renderer::sync_swapchain_to_window() {
        GLT_PROFILE_SCOPE("scene_renderer::sync_swapchain_to_window");

        auto* window = m_plugin.get_window();
        if (!window || !window->is_valid(m_window))
            return;

        const auto fb = window->get_framebuffer_size(m_window);
        if (fb.x <= 0 || fb.y <= 0)
            return;                                 // minimized: render() skips the frame

        resize({ static_cast<u32>(fb.x), static_cast<u32>(fb.y) });     // no-op when nothing changed
    }


    void scene_renderer::resize(glm::uvec2 extent) {
        GLT_PROFILE_SCOPE("scene_renderer::resize");

        if (extent.x == 0 || extent.y == 0)
            return;

        // Surface: compare against the requested size (see render()); the swapchain extent may be clamped by the driver
        if (m_offscreen ? (extent == m_extent)
                        : (!m_swapchain_out_of_date && extent.x == m_swapchain_builder.width && extent.y == m_swapchain_builder.height))
            return;
        m_swapchain_out_of_date = false;

        m_plugin.get_vk_device().waitIdle();

        if (m_offscreen) {
            // Build the new target BEFORE dropping the old one and give it valid content in SHADER_READ_ONLY layout.
            // The UI samples the output image this same frame, before this renderer draws into it again (the main window
            // renders first); a freshly created image is uninitialised + UNDEFINED, which showed up as garbled,
            // colour-shifted blocks on GPUs with framebuffer compression while dragging a resize
            auto next = GLT::create_ref<GLT::renderer::vk_rasterizer::image>();
            next->resize({ extent.x, extent.y, 1 });
            if (m_output_image)
                seed_resized_output(*m_output_image, *next);
            m_output_image = next;                  // old image (and its ImGui descriptor) released here; GPU is idle
            m_extent = extent;
            return;
        }

        // Build the replacement with the old swapchain as oldSwapchain, then destroy the old one. The presentation engine
        // can hand over images instead of starting from nothing (smoother live resize; some WSI implementations need it)
        const util::swapchain_resources old_swapchain = m_swapchain;
        m_swapchain_builder.width = extent.x;
        m_swapchain_builder.height = extent.y;
        m_swapchain = m_swapchain_builder.build_swapchain(old_swapchain.swapchain_handle);
        m_swapchain_builder.destroy_swapchain(m_plugin.get_vk_device(), old_swapchain);
        m_image_count = static_cast<u32>(m_swapchain.swapchain_images.size());
        m_swapchain_images_layout.assign(m_image_count, vk::ImageLayout::eUndefined);
        m_extent = { m_swapchain.swapchain_extent.width, m_swapchain.swapchain_extent.height };
        create_present_semaphores();            // image count can change on rebuild

        recreate_imgui_framebuffers();
    }


    void* scene_renderer::get_output_image() const {

        if (m_offscreen && m_output_image)
            return m_output_image->get_descriptor_set();
        return nullptr;
    }

    std::span<const GLT::render::render_mode_info> scene_renderer::supported_modes() const { return modes::mode_table; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void scene_renderer::init_surface_target(const GLT::render::surface_target_desc& target) {
        GLT_PROFILE_SCOPE("scene_renderer::init_surface_target");

        auto device = m_plugin.get_vk_device();
        const auto& queues = m_plugin.get_queues_ref();

        auto* window = m_plugin.get_window();
        ASSERT(window, "", "init_surface_target: no window plugin")
        ASSERT(window->is_valid(target.window), "", "init_surface_target: invalid window handle")

        // Renderer owns the surface; we just take a ref for the lifetime of this scene renderer.
        const vk::SurfaceKHR surface = m_plugin.get_or_create_surface(target.window);
        if (!surface)
            throw std::runtime_error(" failed to acquire a Vulkan surface for the target window");

        m_swapchain_builder = util::swapchain_builder(device, m_plugin.get_physical_device(), surface,
            queues.graphics_index, queues.present_index);

        const auto fb = window->get_framebuffer_size(target.window);
        m_swapchain_builder.width  = static_cast<u32>(std::max(fb.x, 1));
        m_swapchain_builder.height = static_cast<u32>(std::max(fb.y, 1));
        m_swapchain_builder.back_buffer_count = MAX_CONCURRENT_FRAMES;
        m_swapchain_builder.image_usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        m_swapchain_builder.desired_format = vk::Format::eB8G8R8A8Unorm;
        m_swapchain_builder.present_mode = window->get_vsync(target.window)
            ? vk::PresentModeKHR::eFifo
            : vk::PresentModeKHR::eMailbox;

        m_swapchain = m_swapchain_builder.build_swapchain();
        m_image_count = static_cast<u32>(m_swapchain.swapchain_images.size());
        m_swapchain_images_layout.assign(m_image_count, vk::ImageLayout::eUndefined);
        m_extent = { m_swapchain.swapchain_extent.width, m_swapchain.swapchain_extent.height };
        create_present_semaphores();
    }


    void scene_renderer::create_present_semaphores() {
        GLT_PROFILE_SCOPE("scene_renderer::create_present_semaphores");

        destroy_present_semaphores();
        auto device = m_plugin.get_vk_device();
        m_render_finished.resize(m_image_count);
        for (auto& semaphore : m_render_finished)
            semaphore = device.createSemaphore(vk::SemaphoreCreateInfo{});
    }


    void scene_renderer::destroy_present_semaphores() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_present_semaphores");

        auto device = m_plugin.get_vk_device();
        for (auto& semaphore : m_render_finished)
            if (semaphore)
                device.destroySemaphore(semaphore);
        m_render_finished.clear();
    }


    void scene_renderer::create_timestamp_queries() {
        GLT_PROFILE_SCOPE("scene_renderer::create_timestamp_queries");

        const auto& queues = m_plugin.get_queues_ref();
        const auto families = m_plugin.get_physical_device().getQueueFamilyProperties();
        const u32 valid_bits = queues.graphics_index < families.size() ? families[queues.graphics_index].timestampValidBits : 0;
        const f32 period = m_plugin.get_dev()->get_properties().limits.timestampPeriod;

        if (valid_bits == 0 || period <= 0.f) {
            LOG(warn, "GPU timestamps not supported on the graphics queue, GPU timings disabled")
            return;
        }

        m_timestamp_period_ns = period;
        m_timestamp_mask = valid_bits >= 64 ? ~0ull : ((1ull << valid_bits) - 1ull);

        vk::QueryPoolCreateInfo qci{};
        qci.queryType = vk::QueryType::eTimestamp;
        qci.queryCount = MAX_CONCURRENT_FRAMES * MAX_TIMESTAMPS_PER_FRAME;
        m_timestamp_pool = m_plugin.get_vk_device().createQueryPool(qci);
        // No host reset needed: render_graph::execute() resets each frame slot's range on the GPU before writing it, and
        // only ranges that were written are ever read back
    }


    void scene_renderer::destroy_timestamp_queries() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_timestamp_queries");

        if (m_timestamp_pool) {
            m_plugin.get_vk_device().destroyQueryPool(m_timestamp_pool);
            m_timestamp_pool = nullptr;
        }
    }


    void scene_renderer::resolve_gpu_timestamps(u32 frame_slot) {
        GLT_PROFILE_SCOPE("scene_renderer::resolve_gpu_timestamps");

        gpu_frame_report report{};
        read_back_draw_stats(frame_slot, report);   // visible draws / triangles from GPU culling

        const u32 written = std::exchange(m_timestamps_written[frame_slot], 0u);
        std::array<u64, MAX_TIMESTAMPS_PER_FRAME> ticks{};
        const bool have_timestamps = m_timestamp_pool && written >= 2 &&
            m_plugin.get_vk_device().getQueryPoolResults(m_timestamp_pool, frame_slot * MAX_TIMESTAMPS_PER_FRAME, written,
                written * sizeof(u64), ticks.data(), sizeof(u64), vk::QueryResultFlagBits::e64) == vk::Result::eSuccess;

        // Per-renderer result for i_scene_renderer::last_frame_stats() (the shared report below sums all renderers)
        auto publish = [&](const f32 geometry_ms) {
            const frame_view& view = m_frame_views[frame_slot];
            m_last_frame_stats.serial++;
            m_last_frame_stats.gpu_time_ms = report.gpu_time_ms;
            m_last_frame_stats.geometry_time_ms = geometry_ms;
            m_last_frame_stats.draw_calls = report.draw_calls;
            m_last_frame_stats.triangles = report.triangles;
            m_last_frame_stats.cluster_draws = report.virtual_clusters;
            m_last_frame_stats.cluster_triangles = report.virtual_triangles;
            m_last_frame_stats.camera_position = view.camera_position;
            m_last_frame_stats.render_mode = view.render_mode;
            m_last_frame_stats.extent = view.extent;
        };

        if (!have_timestamps) {
            if (report.draw_calls > 0) {
                publish(0.0f);
                m_shared->report_gpu_frame(report);
            }
            return;
        }

        const auto& names = m_timed_pass_names[frame_slot];
        const f64 to_ms = static_cast<f64>(m_timestamp_period_ns) * 1e-6;
        auto delta_ms = [&](u32 from, u32 to) -> f32 {
            const u64 a = ticks[from] & m_timestamp_mask;
            const u64 b = ticks[to] & m_timestamp_mask;
            return b >= a ? static_cast<f32>(static_cast<f64>(b - a) * to_ms) : 0.f;
        };

        // Rows are per target ("viewport ..." / "window ..."): several renderers used to be summed under one pass name
        const std::string label = m_offscreen ? "viewport " : "window ";
        report.gpu_time_ms = delta_ms(0, written - 1);
        report.render_passes = written - 1;
        report.pass_times_ms.reserve(written + 2);
        report.pass_times_ms.emplace_back(label + "[frame total]", report.gpu_time_ms);
        report.pass_times_ms.emplace_back(label + "[width]", static_cast<f32>(m_extent.x));
        report.pass_times_ms.emplace_back(label + "[height]", static_cast<f32>(m_extent.y));
        {   // local light shadows (counts, not times): lights with shadow maps, their views, views drawn in the latest frame
            const local_shadow_cache::frame_stats& shadows = m_local_shadows.stats();
            report.pass_times_ms.emplace_back(label + "[shadowed local lights]", static_cast<f32>(shadows.shadowed));
            report.pass_times_ms.emplace_back(label + "[shadow views]", static_cast<f32>(shadows.views));
            report.pass_times_ms.emplace_back(label + "[shadow views drawn]", static_cast<f32>(shadows.rendered));
            report.pass_times_ms.emplace_back(label + "[shadow views waiting]", static_cast<f32>(shadows.waiting));
            report.pass_times_ms.emplace_back(label + "[shadow atlas used %]", shadows.atlas_fill * 100.0f);
            const sun_shadow_cascades::frame_stats& sun = m_sun_shadows.stats();
            report.pass_times_ms.emplace_back(label + "[sun shadow distance m]", m_shadow.active ? m_sun_shadow_distance : 0.f);
            report.pass_times_ms.emplace_back(label + "[sun shadow cascades]", static_cast<f32>(sun.cascades));
            report.pass_times_ms.emplace_back(label + "[sun shadow cascades drawn]", static_cast<f32>(sun.drawn));
            report.pass_times_ms.emplace_back(label + "[sun shadow cascades waiting]", static_cast<f32>(sun.waiting));
        }
        if (m_vg_cull_pipeline) {               // virtual geometry: clusters / triangles drawn (counts, not times)
            report.pass_times_ms.emplace_back(label + "[virtual geometry clusters]", static_cast<f32>(report.virtual_clusters));
            report.pass_times_ms.emplace_back(label + "[virtual geometry triangles]", static_cast<f32>(report.virtual_triangles));
        }
        // Passes that cull or draw scene geometry: what a mesh's triangle count and draw calls actually change
        static constexpr std::string_view GEOMETRY_PASSES[] = {
            "reset_draw_count", "virtual_geometry_cull", "cull", "depth_prepass", "scene", "translucent",
            "reset_shadow_draw_count", "shadow_cull", "shadow_pass", "reset_local_shadow_counts", "local_shadow_cull", "local_shadow_pass",
            "virtual_geometry_debug",
        };
        f32 geometry_ms = 0.0f;
        for (u32 i = 1; i < written && (i - 1) < names.size(); ++i) {
            const f32 pass_ms = delta_ms(i - 1, i);
            report.pass_times_ms.emplace_back(label + names[i - 1], pass_ms);
            if (std::find(std::begin(GEOMETRY_PASSES), std::end(GEOMETRY_PASSES), std::string_view(names[i - 1])) != std::end(GEOMETRY_PASSES))
                geometry_ms += pass_ms;
        }

        publish(geometry_ms);
        m_shared->report_gpu_frame(report);
    }


    void scene_renderer::init_offscreen_target(const GLT::render::offscreen_target_desc& target) {
        GLT_PROFILE_SCOPE("scene_renderer::init_offscreen_target");

        if (target.extent.x == 0 || target.extent.y == 0)
            throw std::runtime_error("offscreen target extent is zero");

        m_output_image = GLT::create_ref<GLT::renderer::vk_rasterizer::image>();
        m_output_image->resize({ target.extent.x, target.extent.y, 1 });
        m_extent = target.extent;
    }


    void scene_renderer::init_frames() {
        GLT_PROFILE_SCOPE("scene_renderer::init_frames");

        auto device = m_plugin.get_vk_device();
        vk::SemaphoreCreateInfo sem_ci{};
        vk::FenceCreateInfo fence_ci{};
        fence_ci.flags = vk::FenceCreateFlagBits::eSignaled;

        for (auto& slot : m_frames) {
            slot.fence = device.createFence(fence_ci);
            if (!m_offscreen)
                slot.image_available = device.createSemaphore(sem_ci);
        }

        vk::CommandBufferAllocateInfo cai{};
        cai.commandPool = m_graphics_pool;
        cai.level = vk::CommandBufferLevel::ePrimary;
        cai.commandBufferCount = MAX_CONCURRENT_FRAMES;
        auto bufs = device.allocateCommandBuffers(cai);
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; i++)
            m_frames[i].cmd = bufs[i];
    }


    // One Tracy GPU context per scene renderer (each records into its own command buffers). Tracy calibrates the GPU clock
    // with a few one-time submits on frame slot 0's command buffer, which is unused at this point
    void scene_renderer::create_gpu_profiler() {
        GLT_PROFILE_SCOPE("scene_renderer::create_gpu_profiler");

#if defined(GLT_TRACY_ENABLED)
        const auto& queues = m_plugin.get_queues_ref();
        auto* ctx = tracy::CreateVkContext(static_cast<VkPhysicalDevice>(m_plugin.get_physical_device()),
            static_cast<VkDevice>(m_plugin.get_vk_device()), static_cast<VkQueue>(queues.graphics_queue),
            static_cast<VkCommandBuffer>(m_frames[0].cmd), nullptr, nullptr);
        if (ctx) {
            const std::string name = m_offscreen ? "GPU (offscreen target)" : "GPU (window)";
            ctx->Name(name.c_str(), static_cast<uint16_t>(name.size()));
        }
        m_gpu_profiler = ctx;
#endif
    }


    void scene_renderer::destroy_gpu_profiler() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_gpu_profiler");

#if defined(GLT_TRACY_ENABLED)
        if (m_gpu_profiler)
            tracy::DestroyVkContext(static_cast<tracy::VkCtx*>(m_gpu_profiler));
#endif
        m_gpu_profiler = nullptr;
        if (m_graph)
            m_graph->set_gpu_profiler(nullptr);
    }


    void scene_renderer::destroy_internal() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_internal");

        if (!m_plugin.get_vk_device())
            return;

        m_plugin.get_vk_device().waitIdle();
        destroy_gpu_profiler();

        if (m_offscreen && m_output_image)
            m_output_image.reset();

        if (m_imgui_initialized)
            imgui_shutdown();                                 // before swapchain

        destroy_gpu_state();

        for (auto& slot : m_frames) {
            if (slot.fence)            {
                m_plugin.get_vk_device().destroyFence(slot.fence);
                slot.fence = nullptr;
            }
            if (slot.image_available)  {
                m_plugin.get_vk_device().destroySemaphore(slot.image_available);
                slot.image_available = nullptr;
            }
        }

        if (m_graphics_pool) {
            m_plugin.get_vk_device().destroyCommandPool(m_graphics_pool);
            m_graphics_pool = nullptr;
            for (auto& slot : m_frames) slot.cmd = nullptr;
        }

        destroy_present_semaphores();
        destroy_timestamp_queries();

        if (!m_offscreen && m_swapchain.swapchain_handle) {
            for (auto& view : m_swapchain.swapchain_image_views)
                m_plugin.get_vk_device().destroyImageView(view);
            m_plugin.get_vk_device().destroySwapchainKHR(m_swapchain.swapchain_handle);
            m_swapchain = {};
        }

        m_graph.reset();

        // Release the override materials and mesh references we took
        for (u32 slot : m_held_materials)
            m_shared->release_material(slot);
        m_held_materials.clear();

        for (const auto& [h, id] : m_resident_meshes)
            m_shared->release_mesh(id);
        m_resident_meshes.clear();

        // Hand the surface back to the renderer. It was refcounted; if this was the last holder the
        // renderer destroys the surface. The window itself is unaffected — the platform plugin owns it.
        if (!m_offscreen && m_window.is_valid())
            m_plugin.release_surface(m_window);
    }


    // Offscreen output is sampled by ImGui (another submission, typically the main window's next frame). Declaring a
    // sampled read as the last use makes the graph emit the colour-write -> fragment-read barrier and transition the image
    // to SHADER_READ_ONLY_OPTIMAL, the layout its ImGui descriptor was created with. Before, it stayed in
    // COLOR_ATTACHMENT_OPTIMAL with no barrier
    void scene_renderer::add_output_to_shader_read_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_output_to_shader_read_pass");

        m_graph->add_pass("output_to_shader_read",
            [this](graph::pass_builder& b) {
                b.read(m_scene_color, graph::texture_usage::sampled);
                b.set_side_effects(true);
            },
            [](graph::pass_context&) {});      // the barrier emitted before this pass is the whole point
    }


    // Scaled copy of the last frame into a freshly resized output image (or a clear if there is no frame yet), leaving
    // it in SHADER_READ_ONLY_OPTIMAL. Synchronous (immediate_submit waits); caller already waited for the device
    void scene_renderer::seed_resized_output(GLT::renderer::vk_rasterizer::image& from, GLT::renderer::vk_rasterizer::image& to) {
        GLT_PROFILE_SCOPE("scene_renderer::seed_resized_output");

        const util::allocated_image& src = from.get_allocated_image_ref();
        const util::allocated_image& dst = to.get_allocated_image_ref();
        const vk::ImageLayout src_layout = from.get_accessible_image_ref().layout;
        const bool has_content = src.image && src_layout != vk::ImageLayout::eUndefined;

        if (!dst.image)
            return;

        const vk::ImageSubresourceRange range{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

        m_shared->immediate_submit([&](vk::CommandBuffer cmd) {

            std::array<vk::ImageMemoryBarrier2, 2> to_transfer{};
            u32 barrier_count = 0;

            vk::ImageMemoryBarrier2& dst_barrier = to_transfer[barrier_count++];
            dst_barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
            dst_barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
            dst_barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
            dst_barrier.oldLayout = vk::ImageLayout::eUndefined;
            dst_barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            dst_barrier.image = dst.image;
            dst_barrier.subresourceRange = range;

            if (has_content) {
                vk::ImageMemoryBarrier2& src_barrier = to_transfer[barrier_count++];
                src_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
                src_barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
                src_barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
                src_barrier.dstAccessMask = vk::AccessFlagBits2::eTransferRead;
                src_barrier.oldLayout = src_layout;
                src_barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
                src_barrier.image = src.image;
                src_barrier.subresourceRange = range;
            }

            vk::DependencyInfo dep{};
            dep.imageMemoryBarrierCount = barrier_count;
            dep.pImageMemoryBarriers = to_transfer.data();
            cmd.pipelineBarrier2(dep);

            if (has_content) {
                vk::ImageBlit blit{};
                blit.srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
                blit.srcOffsets[1] = vk::Offset3D(static_cast<i32>(src.width), static_cast<i32>(src.height), 1);
                blit.dstSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
                blit.dstOffsets[1] = vk::Offset3D(static_cast<i32>(dst.width), static_cast<i32>(dst.height), 1);
                cmd.blitImage(src.image, vk::ImageLayout::eTransferSrcOptimal, dst.image, vk::ImageLayout::eTransferDstOptimal,
                    blit, vk::Filter::eLinear);
            } else {
                const vk::ClearColorValue clear(std::array<f32, 4>{ m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a });
                cmd.clearColorImage(dst.image, vk::ImageLayout::eTransferDstOptimal, clear, range);
            }

            vk::ImageMemoryBarrier2 to_read{};
            to_read.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
            to_read.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
            to_read.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
            to_read.dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead;
            to_read.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            to_read.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            to_read.image = dst.image;
            to_read.subresourceRange = range;

            vk::DependencyInfo read_dep{};
            read_dep.imageMemoryBarrierCount = 1;
            read_dep.pImageMemoryBarriers = &to_read;
            cmd.pipelineBarrier2(read_dep);
        });

        to.get_accessible_image_ref().layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }


    void scene_renderer::add_clear_backbuffer_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_clear_backbuffer_pass");

        m_graph->add_pass("clear_backbuffer",
            [this](graph::pass_builder& b) {
                b.color_attachment(m_backbuffer_handle,
                    vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore,
                    vk::ClearColorValue(std::array<f32,4>{m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a }));
                b.set_side_effects(true);
            },
            [](graph::pass_context& ctx) {
                ctx.begin_rendering();
                ctx.end_rendering();
            });
    }

}

#include "scene/scene_renderer_util.inl"
#include "scene/scene_renderer_procedural.inl"
#include "scene/scene_renderer_scene.inl"
#include "scene/scene_renderer_material_bind.inl"
#include "scene/scene_renderer_clusters.inl"
#include "scene/scene_renderer_lights.inl"
#include "scene/scene_renderer_light_culling.inl"
#include "scene/scene_renderer_cull.inl"
#include "scene/scene_renderer_virtual_geometry.inl"
#include "scene/scene_renderer_virtual_geometry_debug.inl"
#include "scene/scene_renderer_shadow.inl"
#include "scene/scene_renderer_blit.inl"
#include "scene/scene_renderer_tonemap.inl"
#include "scene/scene_renderer_light_cull_debug.inl"
#include "scene/scene_renderer_tri_density.inl"
#include "scene/scene_renderer_environment.inl"

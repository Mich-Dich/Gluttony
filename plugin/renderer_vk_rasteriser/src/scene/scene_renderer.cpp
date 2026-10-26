
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



namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    static const glm::vec3 SHADOW_FACE_DIRECTIONS[6] = {
        { 1.f,  0.f,  0.f},
        {-1.f,  0.f,  0.f},
        { 0.f,  1.f,  0.f},
        { 0.f, -1.f,  0.f},
        { 0.f,  0.f,  1.f},
        { 0.f,  0.f, -1.f}
    };

    // MACROS ==========================================================================================================

    #if defined(DEBUG)
        #define VK_SR_CHECK(expr)  ASSERT_S(expr == vk::Result::eSuccess)
    #else
        #define VK_SR_CHECK(expr)
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static FORCE_INLINE void extract_frustum_planes(const glm::mat4& view_projection, glm::vec4 (&planes)[6]);

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

        if (!m_active && m_offscreen)       // Offscreen scene renderers don't own ImGui and don't need to run when inactive
            return;

        m_delta_time = delta_time;
        VK_SR_CHECK(m_plugin.get_vk_device().waitForFences(m_frames[m_current_frame].fence, VK_TRUE, UINT64_MAX));

        if (m_imgui_initialized)            // Surface target: ImGui frame MUST open every frame
            begin_imgui_frame();

        if (!m_active)                      // Surface target, inactive: keep ImGui, skip 3D CPU work
            return;

        ensure_scene_meshes();
        build_instance_buffer();
        update_lights();
        update_shadow();
        update_camera_ubo();
        update_globals();
        write_scene_material_descriptors();
        process_debug_primitives();
    }


    std::expected<void, GLT::render::render_error> scene_renderer::render(u64 /*frame_number*/) {

        if (!m_active && m_offscreen)       // Offscreen inactive: nothing to do, leave fence + frame slot alone
            return {};

        auto device = m_plugin.get_vk_device();
        const auto& queues = m_plugin.get_queues_ref();
        auto& frame = m_frames[m_current_frame];

        device.resetFences(frame.fence);

        if (!m_offscreen) {
            try {

                auto r = device.acquireNextImageKHR(m_swapchain.swapchain_handle, UINT64_MAX, frame.image_available, nullptr);
                m_current_swapchain_image = r.value;

            } catch (const vk::OutOfDateKHRError&) {

                return std::unexpected(GLT::render::render_error{
                    GLT::render::render_error::code::target_out_of_date,
                    "swapchain out of date"
                });
            }
        }

        frame.cmd.reset();
        frame.cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

        // LOG(trace, "render: window=[{},{}] active={} instances={} lights={}",
        //     m_window.index, m_window.generation, m_active, m_instance_count, m_light_count)

        m_graph->begin_frame();
        update_cluster_dimensions();
        update_light_meta_ubo();
        import_frame_textures();
        update_light_cull_descriptors();
        update_mesh_descriptors();

        if (m_active) {

            add_reset_draw_count_pass();
            add_cull_pass();
            if (m_shadow.active) {
                add_reset_shadow_draw_count_pass();
                add_shadow_cull_pass();
                add_shadow_pass();
            }
            add_local_shadow_passes();
            add_depth_prepass_pass();
            add_clear_light_grid_pass();
            add_light_culling_pass();
            add_scene_pass();
            add_debug_draw_pass();

            if (!m_offscreen)
                add_blit_to_backbuffer_pass();
        } else {

            if (!m_offscreen)                   // Inactive surface: clear the backbuffer so ImGui's eLoad has valid base content
                add_clear_backbuffer_pass();
        }

        if (m_imgui_initialized)
            add_imgui_pass();
        if (m_imgui_initialized)
            end_imgui_frame();                  // ImGui::Render() — pass consumes draw data at exec

        m_graph->compile();
        m_graph->execute(frame.cmd);
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

        frame.cmd.end();

        // Submit
        vk::CommandBufferSubmitInfo cbsi{};
        cbsi.commandBuffer = frame.cmd;

        vk::SemaphoreSubmitInfo wait_si{}, signal_si{};
        if (!m_offscreen) {
            wait_si.semaphore = frame.image_available;
            wait_si.stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
            signal_si.semaphore = frame.render_finished;
            signal_si.stageMask = vk::PipelineStageFlagBits2::eAllCommands;
        }

        vk::SubmitInfo2 si{};
        si.commandBufferInfoCount = 1;
        si.pCommandBufferInfos = &cbsi;
        if (!m_offscreen) {
            si.waitSemaphoreInfoCount = 1;
            si.pWaitSemaphoreInfos = &wait_si;
            si.signalSemaphoreInfoCount = 1;
            si.pSignalSemaphoreInfos = &signal_si;
        }

        try {

            queues.graphics_queue.submit2(si, frame.fence);

        } catch (const std::exception& e) {
            return std::unexpected(GLT::render::render_error{
                GLT::render::render_error::code::backend, 
                std::format("submit failed: {}", e.what())
            });
        }

        if (!m_offscreen) {
            vk::PresentInfoKHR pi{};
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &frame.render_finished;
            pi.swapchainCount = 1;
            pi.pSwapchains = &m_swapchain.swapchain_handle;
            pi.pImageIndices = &m_current_swapchain_image;
            try {

                (void)queues.present_queue.presentKHR(pi);

            } catch (const vk::OutOfDateKHRError&) {
                return std::unexpected(GLT::render::render_error{
                    GLT::render::render_error::code::target_out_of_date,
                    "present out of date"
                });
            }
        }

        m_frame_counter++;
        m_current_frame = (m_current_frame + 1) % MAX_CONCURRENT_FRAMES;
        return {};
    }


    void scene_renderer::resize(glm::uvec2 extent) {

        if (extent.x == 0 || extent.y == 0 || extent == m_extent)
            return;

        m_plugin.get_vk_device().waitIdle();

        if (m_offscreen) {
            m_output_image->resize({ extent.x, extent.y, 1 });
            m_extent = extent;
            return;
        }

        m_swapchain_builder.destroy_swapchain(m_plugin.get_vk_device(), m_swapchain);
        m_swapchain_builder.width = extent.x;
        m_swapchain_builder.height = extent.y;
        m_swapchain = m_swapchain_builder.build_swapchain();
        m_image_count = static_cast<u32>(m_swapchain.swapchain_images.size());
        m_swapchain_images_layout.assign(m_image_count, vk::ImageLayout::eUndefined);
        m_extent = { m_swapchain.swapchain_extent.width, m_swapchain.swapchain_extent.height };

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
    }


    void scene_renderer::init_offscreen_target(const GLT::render::offscreen_target_desc& target) {

        if (target.extent.x == 0 || target.extent.y == 0)
            throw std::runtime_error("offscreen target extent is zero");

        m_output_image = GLT::create_ref<GLT::renderer::vk_rasterizer::image>();
        m_output_image->resize({ target.extent.x, target.extent.y, 1 });
        m_extent = target.extent;
    }


    void scene_renderer::init_frames() {

        auto device = m_plugin.get_vk_device();
        vk::SemaphoreCreateInfo sem_ci{};
        vk::FenceCreateInfo fence_ci{};
        fence_ci.flags = vk::FenceCreateFlagBits::eSignaled;

        for (auto& slot : m_frames) {
            slot.fence = device.createFence(fence_ci);
            if (!m_offscreen) {
                slot.image_available = device.createSemaphore(sem_ci);
                slot.render_finished = device.createSemaphore(sem_ci);
            }
        }

        vk::CommandBufferAllocateInfo cai{};
        cai.commandPool = m_graphics_pool;
        cai.level = vk::CommandBufferLevel::ePrimary;
        cai.commandBufferCount = MAX_CONCURRENT_FRAMES;
        auto bufs = device.allocateCommandBuffers(cai);
        for (u32 i = 0; i < MAX_CONCURRENT_FRAMES; i++)
            m_frames[i].cmd = bufs[i];
    }


    void scene_renderer::destroy_internal() {

        if (!m_plugin.get_vk_device())
            return;

        m_plugin.get_vk_device().waitIdle();

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
            if (slot.render_finished)  {
                m_plugin.get_vk_device().destroySemaphore(slot.render_finished);
                slot.render_finished = nullptr;
            }
        }

        if (m_graphics_pool) {
            m_plugin.get_vk_device().destroyCommandPool(m_graphics_pool);
            m_graphics_pool = nullptr;
            for (auto& slot : m_frames) slot.cmd = nullptr;
        }

        if (!m_offscreen && m_swapchain.swapchain_handle) {
            for (auto& view : m_swapchain.swapchain_image_views)
                m_plugin.get_vk_device().destroyImageView(view);
            m_plugin.get_vk_device().destroySwapchainKHR(m_swapchain.swapchain_handle);
            m_swapchain = {};
        }

        m_graph.reset();

        // Release all mesh references we took
        for (const auto& [h, id] : m_resident_meshes)
            m_shared->release_mesh(id);
        m_resident_meshes.clear();

        // Hand the surface back to the renderer. It was refcounted; if this was the last holder the
        // renderer destroys the surface. The window itself is unaffected — the platform plugin owns it.
        if (!m_offscreen && m_window.is_valid())
            m_plugin.release_surface(m_window);
    }


    void scene_renderer::add_clear_backbuffer_pass() {

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
#include "scene/scene_renderer_shadow.inl"
#include "scene/scene_renderer_blit.inl"

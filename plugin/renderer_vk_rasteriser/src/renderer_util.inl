
#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void renderer::transition_image_layout(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout oldL, vk::ImageLayout newL,
        vk::ImageSubresourceRange range) {

        m_dev->transition_image_layout(cmd, image, oldL, newL, range, vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands);
    }


    void* renderer::get_rendered_image() { return static_cast<void*>(m_output_image->get_descriptor_set()); }


    glm::uvec2 renderer::get_rendered_image_size() { return m_output_image->get_size(); }


    void renderer::set_active_camera(const GLT::world::camera_snapshot& camera) { m_active_camera = camera; }


    [[nodiscard]] debug::render_stats renderer::get_render_stats() const {

        return debug::render_stats{
            .gpu_time_ms = m_last_gpu_time_ms,
            .draw_calls = m_frame_draw_calls,
            .triangles = 1,
            .vertices = 3,
            .render_passes = m_frame_render_passes,
            #if defined(DEBUG)
                .vram_bytes = GLT::render::image::get_live_vram_bytes(),
            #endif
            .ram_bytes = 0,
            #if defined(DEBUG)
                .texture_count = GLT::render::image::get_live_count(),
            #endif
            .buffer_count = m_live_buffer_count,
            .descriptor_set_count = m_live_descriptor_set_count,
            .pipeline_count = m_live_pipeline_count,
        };
    }

    // mode ------------------------------------------------------------------------------------------------------------

    std::span<const GLT::render::render_mode_info> renderer::supported_modes() const { return modes::mode_table; }


    void renderer::set_render_mode(u32 id) { m_active_mode = id; }


    u32  renderer::get_render_mode() const { return m_active_mode; }

    // settings --------------------------------------------------------------------------------------------------------

    const GLT::reflect::type_descriptor* renderer::settings_descriptor() const { return GLT::reflect::type_of<settings::visual>(); }


    void* renderer::settings_data() { return &m_visual_settings; }


    void  renderer::on_settings_changed() { }

    // general utility -------------------------------------------------------------------------------------------------

    void renderer::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) {

        VK_CHECK_S(m_device.waitForFences(m_immediate_submit_fence, VK_TRUE, UINT64_MAX));
        m_device.resetFences(m_immediate_submit_fence);
        m_immediate_submit_command_buffer.reset();

        m_immediate_submit_command_buffer.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        function(m_immediate_submit_command_buffer);
        m_immediate_submit_command_buffer.end();

        vk::CommandBufferSubmitInfo command_buffer_si{};
        command_buffer_si.commandBuffer = m_immediate_submit_command_buffer;
        vk::SubmitInfo2 submit_info{};
        submit_info.commandBufferInfoCount = 1;
        submit_info.pCommandBufferInfos = &command_buffer_si;
        m_queues.graphics_queue.submit2(submit_info, m_immediate_submit_fence);
        VK_CHECK_S(m_device.waitForFences(m_immediate_submit_fence, VK_TRUE, UINT64_MAX));
    }


    glm::mat4 renderer::camera_projection() const {

        const f32 aspect_ratio = static_cast<f32>(m_render_size.x) / static_cast<f32>(m_render_size.y);
        glm::mat4 projection = glm::perspective(glm::radians(m_active_camera.fov), aspect_ratio, m_active_camera.near_plane,
            m_active_camera.far_plane);
        projection[1][1] *= -1.0f;
        return projection;
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::import_frame_textures() {

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

        // Scene depth -------------------------------------------------------------------------------------------------
        graph::texture_desc depth_desc{};
        depth_desc.name = "scene_depth";
        depth_desc.format = vk::Format::eD32Sfloat;
        depth_desc.width = m_render_size.x;
        depth_desc.height = m_render_size.y;
        depth_desc.usage = VKIU::eDepthStencilAttachment | VKIU::eSampled;
        depth_desc.persistent = true;

        m_depth_target = m_graph->create_texture(depth_desc);

        // Directional shadow map --------------------------------------------------------------------------------------
        graph::texture_desc shadow_desc{};
        shadow_desc.name = "shadow_map";
        shadow_desc.format = vk::Format::eD32Sfloat;
        shadow_desc.width = m_shadow_settings.resolution;
        shadow_desc.height = m_shadow_settings.resolution;
        shadow_desc.usage = VKIU::eDepthStencilAttachment | VKIU::eSampled;
        shadow_desc.persistent = true;

        m_shadow_map = m_graph->create_texture(shadow_desc);

        // Local shadow atlas ------------------------------------------------------------------------------------------
        graph::texture_desc atlas_desc{};
        atlas_desc.name = "shadow_atlas";
        atlas_desc.format = vk::Format::eD32Sfloat;                 // must match m_depth_pipeline's depth format
        atlas_desc.width = m_shadow_settings.atlas_size;
        atlas_desc.height = m_shadow_settings.atlas_size;
        atlas_desc.usage = VKIU::eDepthStencilAttachment | VKIU::eSampled;
        atlas_desc.persistent = true;

        m_shadow_atlas = m_graph->create_texture(atlas_desc);
    }

}

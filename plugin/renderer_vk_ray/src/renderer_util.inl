#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_ray {

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

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

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


    void renderer::clear_gbuffer(vk::CommandBuffer cmd, GLT::ref<image>& gbuffer) {

        const vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        const vk::Image img = gbuffer->get_allocated_image_ref().image;

        // to TRANSFER_DST
        m_vr_dev->transition_image_layout(cmd, img, gbuffer->get_accessible_image_ref().layout, vk::ImageLayout::eTransferDstOptimal, range,
            vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer);

        vk::ClearColorValue cv{};
        cv.float32[0] = 0.f; cv.float32[1] = 0.f; cv.float32[2] = 0.f; cv.float32[3] = 0.f;
        cmd.clearColorImage(img, vk::ImageLayout::eTransferDstOptimal, cv, range);

        // back to GENERAL so the rchit can imageStore and the raygen can imageLoad
        m_vr_dev->transition_image_layout(cmd, img, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral, range,
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands);

        gbuffer->get_accessible_image_ref().layout = vk::ImageLayout::eGeneral;
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
                    vk::PipelineStageFlagBits::eAllCommands,        // Source stage
                    vk::PipelineStageFlagBits::eAllCommands         // Destination stage
                );
                m_output_image->get_accessible_image_ref().layout = new_layout;

            } break;

            case image_type::accum: {
                const vk::ImageSubresourceRange range(
                    vk::ImageAspectFlagBits::eColor,                // Color aspect
                    0,                                              // Base mip level
                    1,                                              // Level count
                    0,                                              // Base array layer
                    1                                               // Layer count
                );

                for (u32 i = 0; i < 2; ++i) {
                    m_vr_dev->transition_image_layout(
                        command_buffer,
                        m_accum_image[i]->get_allocated_image_ref().image,
                        m_accum_image[i]->get_accessible_image_ref().layout,
                        new_layout,
                        range,
                        vk::PipelineStageFlagBits::eAllCommands,
                        vk::PipelineStageFlagBits::eAllCommands
                    );
                    m_accum_image[i]->get_accessible_image_ref().layout = new_layout;
                }
            } break;
        }
    }


    void* renderer::get_rendered_image() { return static_cast<void*>(m_output_image->get_descriptor_set()); }


    glm::uvec2 renderer::get_rendered_image_size() { return m_output_image->get_size(); }


    void renderer::set_active_camera(const GLT::world::camera_snapshot& camera) { m_active_camera = camera; }


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

    // modes -----------------------------------------------------------------------------------------------------------

    std::span<const GLT::render::render_mode_info> renderer::supported_modes() const { return modes::mode_table; }


    void renderer::set_render_mode(u32 mode_id) {

        // Validate; ignore unknown ids
        for (const auto& mode : modes::mode_table)
            if (mode.id == mode_id) {
                m_active_mode = mode_id;
                return;
            }

        LOG(warn, "Renderer: unknown render mode id [{}]", mode_id);
    }


    u32 renderer::get_render_mode() const { return m_active_mode; }

    // settings --------------------------------------------------------------------------------------------------------

    void renderer::set_sun_settings(const GLT::render::sun_settings& settings) {

        m_sun_settings = settings;
        m_temporal_valid = false;
    }


    const GLT::render::sun_settings& renderer::get_sun_settings() const { return m_sun_settings; }


    const GLT::reflect::type_descriptor* renderer::settings_descriptor() const { return GLT::reflect::type_of<settings::visual>(); }


    void* renderer::settings_data() { return &m_visual_settings; }


    void renderer::on_settings_changed() { 

        // Any cached state derived from settings must be invalidated
        m_temporal_valid = false;
    }

}

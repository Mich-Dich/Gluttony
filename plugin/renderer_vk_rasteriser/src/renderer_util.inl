
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
            .vram_bytes = GLT::render::image::get_live_vram_bytes(),
            .ram_bytes = 0,
            .texture_count = GLT::render::image::get_live_count(),
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

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}

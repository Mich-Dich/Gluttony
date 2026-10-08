#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

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

}

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

                vk::ImageSubresourceRange accum_range(
                    vk::ImageAspectFlagBits::eColor,                // Color aspect
                    0,                                              // Base mip level
                    1,                                              // Level count
                    0,                                              // Base array layer
                    1                                               // Layer count
                );

                m_vr_dev->transition_image_layout(
                    command_buffer,
                    m_accum_image->get_allocated_image_ref().image,
                    m_accum_image->get_accessible_image_ref().layout,
                    new_layout,
                    accum_range,
                    vk::PipelineStageFlagBits::eAllCommands,
                    vk::PipelineStageFlagBits::eAllCommands
                );
                m_accum_image->get_accessible_image_ref().layout = new_layout;

            } break;
        }
    }

}

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

}

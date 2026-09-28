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

}

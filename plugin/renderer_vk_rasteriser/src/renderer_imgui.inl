
#pragma once

#include <config/imgui_config.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    #if defined(DEBUG)
        #define VALIDATE_INIT                               if (!m_imgui_initialized) { return; }
    #else
        #define VALIDATE_INIT
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Counts primitive draws ImGui will issue - i.e. every cmd buffer entry that isn't a user callback. This matches
    // the Vulkan backend's actual vkCmdDraw* count
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

        util::create_imgui_resources(m_imgui_descriptor_pool, m_device, m_swapchain, m_imgui_render_pass,
            m_instance, m_physical_device, m_queues, m_imgui_framebuffers, m_imgui_initialized);
    }


    void renderer::destroy_imgui_resources() {

        util::destroy_imgui_resources(m_device, m_imgui_framebuffers, m_imgui_render_pass, 
            m_imgui_descriptor_pool, m_imgui_initialized);
    }


    void renderer::begin_imgui_frame() {

        VALIDATE_INIT

        ImGui::SetCurrentContext(imgui_config::get_context_imgui());
        ImGui_ImplVulkan_NewFrame();
        mp_window->begin_imgui_frame();
        ImGui::NewFrame();
    }


    void renderer::end_imgui_frame() {

        VALIDATE_INIT

        ImGui::EndFrame();
        ImGui::Render();                    // draw data is now valid; the graph's imgui pass will consume it
    }

}


#pragma once

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <config/imgui_config.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    #if defined(DEBUG)
        #define VALIDATE_IMGUI_INIT                 if (!m_imgui_initialized) return;
    #else
        #define VALIDATE_IMGUI_INIT
    #endif

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // static u32 count_imgui_draw_calls(const ImDrawData* dd);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // static u32 count_imgui_draw_calls(const ImDrawData* dd) {
    //
    //     if (!dd)
    //         return 0;
    //     u32 n = 0;
    //     for (int i = 0; i < dd->CmdListsCount; ++i) {
    //         const ImDrawList* l = dd->CmdLists[i];
    //         for (int j = 0; j < l->CmdBuffer.Size; ++j)
    //             if (!l->CmdBuffer[j].UserCallback) ++n;
    //     }
    //     return n;
    // }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void scene_renderer::imgui_init() {

        if (m_offscreen)
            return;

        auto window = m_plugin.get_window();
        ASSERT(window, "", "imgui_init: no window plugin")
        ASSERT(window->is_valid(m_window), "", "imgui_init: invalid window handle")

        // Note: per-window ImGui context management lives in the platform plugin's imgui_init(handle,…).
        // The renderer-side backend (ImGui_ImplVulkan) is bound to whichever context is current when
        // it initialises. If you later need independent ImGui state per window, replace
        // imgui_config::get_context_imgui() below with a per-window context fetched from the platform
        // plugin (e.g. via a new get_imgui_context(handle) accessor).
        ImGui::SetCurrentContext(imgui_config::get_context_imgui());

        window->imgui_init(m_window, GLT::render::backend_api::vulkan);

        vk::Instance inst = m_plugin.get_instance();
        ASSERT(ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4,
            [](const char* fn, void* ud) -> PFN_vkVoidFunction {
                auto* i = static_cast<vk::Instance*>(ud);
                return vkGetInstanceProcAddr(static_cast<VkInstance>(*i), fn);
            },
            &inst),
            "", "Failed to load Vulkan functions for ImGui")

        util::create_imgui_resources(
            m_imgui_descriptor_pool,
            m_plugin.get_vk_device_ref(),
            m_swapchain,
            m_imgui_render_pass,
            m_plugin.instance_wrapper(),
            m_plugin.get_physical_device_ref(),
            m_plugin.get_queues_ref(),
            m_imgui_framebuffers,
            m_imgui_initialized);

        LOG(trace, " ImGui initialised for window [{}, {}]", m_window.index, m_window.generation);
    }


    void scene_renderer::imgui_shutdown() {

        if (m_offscreen || !m_imgui_initialized)
            return;

        // Ensure the shared context is current before tearing down its backends
        ImGui::SetCurrentContext(imgui_config::get_context_imgui());

        util::destroy_imgui_resources(
            m_plugin.get_vk_device_ref(),
            m_imgui_framebuffers,
            m_imgui_render_pass,
            m_imgui_descriptor_pool,
            m_imgui_initialized);

        if (auto window = m_plugin.get_window())
            window->imgui_shutdown(m_window);

        m_imgui_framebuffers.clear();
    }


    void scene_renderer::begin_imgui_frame() {

        VALIDATE_IMGUI_INIT
        ImGui::SetCurrentContext(imgui_config::get_context_imgui());
        ImGui_ImplVulkan_NewFrame();
        m_plugin.get_window()->begin_imgui_frame(m_window);
        ImGui::NewFrame();
    }


    void scene_renderer::end_imgui_frame() {

        VALIDATE_IMGUI_INIT
        ImGui::EndFrame();
        ImGui::Render();    // produces draw data consumed by add_imgui_pass()
    }


    void scene_renderer::add_imgui_pass() {

        VALIDATE_IMGUI_INIT

        m_graph->add_pass("imgui",
            [&](graph::pass_builder& builder) {
                builder.color_attachment(m_backbuffer_handle,
                    vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore);
                builder.read(m_scene_color, graph::texture_usage::sampled);
                builder.set_side_effects(true);
            },
            [this](graph::pass_context& ctx) {

                const ImDrawData* dd = ImGui::GetDrawData();

                vk::RenderPassBeginInfo rpbi{};
                rpbi.renderPass = m_imgui_render_pass;
                rpbi.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
                rpbi.renderArea.extent = m_swapchain.swapchain_extent;

                std::array<vk::ClearValue, 1> clear{};
                clear[0].color = { m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a };
                rpbi.clearValueCount = static_cast<u32>(clear.size());
                rpbi.pClearValues = clear.data();

                ctx.cmd.beginRenderPass(rpbi, vk::SubpassContents::eInline);
                ImGui_ImplVulkan_RenderDrawData(const_cast<ImDrawData*>(dd), ctx.cmd);
                ctx.cmd.endRenderPass();

                ImGuiIO& io = ImGui::GetIO();
                if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                }
            });
    }


    void scene_renderer::recreate_imgui_framebuffers() {

        if (m_offscreen || !m_imgui_initialized)
            return;

        for (auto fb : m_imgui_framebuffers)
            m_plugin.get_vk_device().destroyFramebuffer(fb);
        m_imgui_framebuffers.clear();

        for (auto view : m_swapchain.swapchain_image_views) {
            vk::ImageView att[] = { view };
            vk::FramebufferCreateInfo fi{};
            fi.renderPass = m_imgui_render_pass;
            fi.attachmentCount = 1;
            fi.pAttachments = att;
            fi.width = m_swapchain.swapchain_extent.width;
            fi.height = m_swapchain.swapchain_extent.height;
            fi.layers = 1;
            m_imgui_framebuffers.push_back(m_plugin.get_vk_device().createFramebuffer(fi));
        }
    }

    #undef VALIDATE_IMGUI_INIT

}

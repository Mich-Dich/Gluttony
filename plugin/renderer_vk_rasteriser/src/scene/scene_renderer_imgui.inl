
#pragma once

#include <algorithm>
#include <atomic>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <config/imgui_config.h>
#include <debug/profiler.h>
#include <debug/trace.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // Guard in every configuration (it was debug-only)
    #define VALIDATE_IMGUI_INIT                     if (!m_imgui_initialized) return;

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

    // ImGui platform / renderer backend hooks -----------------------------------------------------------------------
    // Every backend callback (GLFW platform + Vulkan renderer, incl. the per-viewport ones run by UpdatePlatformWindows /
    // RenderPlatformWindowsDefault and ImGui::NewFrame) is wrapped in a profiling zone, so a slow detached window, a
    // blocking present or a slow window-system call shows up by name. Originals are kept and called through
    namespace imgui_hooks {
        static decltype(ImGuiPlatformIO::Platform_CreateWindow) s_Platform_CreateWindow = nullptr;
        static void Platform_CreateWindow(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: create window");
            return s_Platform_CreateWindow(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_DestroyWindow) s_Platform_DestroyWindow = nullptr;
        static void Platform_DestroyWindow(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: destroy window");
            return s_Platform_DestroyWindow(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_ShowWindow) s_Platform_ShowWindow = nullptr;
        static void Platform_ShowWindow(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: show window");
            return s_Platform_ShowWindow(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_SetWindowPos) s_Platform_SetWindowPos = nullptr;
        static void Platform_SetWindowPos(ImGuiViewport* vp, ImVec2 v) {
            GLT_PROFILE_SCOPE("ImGui platform: set window pos");
            return s_Platform_SetWindowPos(vp, v);
        }
        static decltype(ImGuiPlatformIO::Platform_GetWindowPos) s_Platform_GetWindowPos = nullptr;
        static ImVec2 Platform_GetWindowPos(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: get window pos");
            return s_Platform_GetWindowPos(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_SetWindowSize) s_Platform_SetWindowSize = nullptr;
        static void Platform_SetWindowSize(ImGuiViewport* vp, ImVec2 v) {
            GLT_PROFILE_SCOPE("ImGui platform: set window size");
            return s_Platform_SetWindowSize(vp, v);
        }
        static decltype(ImGuiPlatformIO::Platform_GetWindowSize) s_Platform_GetWindowSize = nullptr;
        static ImVec2 Platform_GetWindowSize(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: get window size");
            return s_Platform_GetWindowSize(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_GetWindowFramebufferScale) s_Platform_GetWindowFramebufferScale = nullptr;
        static ImVec2 Platform_GetWindowFramebufferScale(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: get framebuffer scale");
            return s_Platform_GetWindowFramebufferScale(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_SetWindowFocus) s_Platform_SetWindowFocus = nullptr;
        static void Platform_SetWindowFocus(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: set window focus");
            return s_Platform_SetWindowFocus(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_GetWindowFocus) s_Platform_GetWindowFocus = nullptr;
        static bool Platform_GetWindowFocus(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: get window focus");
            return s_Platform_GetWindowFocus(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_GetWindowMinimized) s_Platform_GetWindowMinimized = nullptr;
        static bool Platform_GetWindowMinimized(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: get window minimized");
            return s_Platform_GetWindowMinimized(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_SetWindowTitle) s_Platform_SetWindowTitle = nullptr;
        static void Platform_SetWindowTitle(ImGuiViewport* vp, const char* str) {
            GLT_PROFILE_SCOPE("ImGui platform: set window title");
            return s_Platform_SetWindowTitle(vp, str);
        }
        static decltype(ImGuiPlatformIO::Platform_SetWindowAlpha) s_Platform_SetWindowAlpha = nullptr;
        static void Platform_SetWindowAlpha(ImGuiViewport* vp, float a) {
            GLT_PROFILE_SCOPE("ImGui platform: set window alpha");
            return s_Platform_SetWindowAlpha(vp, a);
        }
        static decltype(ImGuiPlatformIO::Platform_UpdateWindow) s_Platform_UpdateWindow = nullptr;
        static void Platform_UpdateWindow(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: update window");
            return s_Platform_UpdateWindow(vp);
        }
        static decltype(ImGuiPlatformIO::Platform_RenderWindow) s_Platform_RenderWindow = nullptr;
        static void Platform_RenderWindow(ImGuiViewport* vp, void* arg) {
            GLT_PROFILE_SCOPE("ImGui platform: render window");
            return s_Platform_RenderWindow(vp, arg);
        }
        static decltype(ImGuiPlatformIO::Platform_SwapBuffers) s_Platform_SwapBuffers = nullptr;
        static void Platform_SwapBuffers(ImGuiViewport* vp, void* arg) {
            GLT_PROFILE_WAIT("ImGui platform: swap buffers");
            return s_Platform_SwapBuffers(vp, arg);
        }
        static decltype(ImGuiPlatformIO::Platform_GetWindowDpiScale) s_Platform_GetWindowDpiScale = nullptr;
        static float Platform_GetWindowDpiScale(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui platform: get dpi scale");
            return s_Platform_GetWindowDpiScale(vp);
        }
        static decltype(ImGuiPlatformIO::Renderer_CreateWindow) s_Renderer_CreateWindow = nullptr;
        static void Renderer_CreateWindow(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui renderer: create window (swapchain)");
            return s_Renderer_CreateWindow(vp);
        }
        static decltype(ImGuiPlatformIO::Renderer_DestroyWindow) s_Renderer_DestroyWindow = nullptr;
        static void Renderer_DestroyWindow(ImGuiViewport* vp) {
            GLT_PROFILE_SCOPE("ImGui renderer: destroy window");
            return s_Renderer_DestroyWindow(vp);
        }
        static decltype(ImGuiPlatformIO::Renderer_SetWindowSize) s_Renderer_SetWindowSize = nullptr;
        static void Renderer_SetWindowSize(ImGuiViewport* vp, ImVec2 v) {
            GLT_PROFILE_SCOPE("ImGui renderer: resize swapchain");
            return s_Renderer_SetWindowSize(vp, v);
        }
        static decltype(ImGuiPlatformIO::Renderer_RenderWindow) s_Renderer_RenderWindow = nullptr;
        static void Renderer_RenderWindow(ImGuiViewport* vp, void* arg) {
            GLT_PROFILE_SCOPE("ImGui renderer: render window (acquire + fence + record + submit)");
            return s_Renderer_RenderWindow(vp, arg);
        }
        static decltype(ImGuiPlatformIO::Renderer_SwapBuffers) s_Renderer_SwapBuffers = nullptr;
        static void Renderer_SwapBuffers(ImGuiViewport* vp, void* arg) {
            GLT_PROFILE_WAIT("ImGui renderer: present window");
            return s_Renderer_SwapBuffers(vp, arg);
        }

        // Idempotent: a field that already points at its hook is left alone (re-init keeps the original)
        static void install() {
            ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
            if (pio.Platform_CreateWindow && pio.Platform_CreateWindow != &Platform_CreateWindow) { s_Platform_CreateWindow = pio.Platform_CreateWindow; pio.Platform_CreateWindow = &Platform_CreateWindow; }
            if (pio.Platform_DestroyWindow && pio.Platform_DestroyWindow != &Platform_DestroyWindow) { s_Platform_DestroyWindow = pio.Platform_DestroyWindow; pio.Platform_DestroyWindow = &Platform_DestroyWindow; }
            if (pio.Platform_ShowWindow && pio.Platform_ShowWindow != &Platform_ShowWindow) { s_Platform_ShowWindow = pio.Platform_ShowWindow; pio.Platform_ShowWindow = &Platform_ShowWindow; }
            if (pio.Platform_SetWindowPos && pio.Platform_SetWindowPos != &Platform_SetWindowPos) { s_Platform_SetWindowPos = pio.Platform_SetWindowPos; pio.Platform_SetWindowPos = &Platform_SetWindowPos; }
            if (pio.Platform_GetWindowPos && pio.Platform_GetWindowPos != &Platform_GetWindowPos) { s_Platform_GetWindowPos = pio.Platform_GetWindowPos; pio.Platform_GetWindowPos = &Platform_GetWindowPos; }
            if (pio.Platform_SetWindowSize && pio.Platform_SetWindowSize != &Platform_SetWindowSize) { s_Platform_SetWindowSize = pio.Platform_SetWindowSize; pio.Platform_SetWindowSize = &Platform_SetWindowSize; }
            if (pio.Platform_GetWindowSize && pio.Platform_GetWindowSize != &Platform_GetWindowSize) { s_Platform_GetWindowSize = pio.Platform_GetWindowSize; pio.Platform_GetWindowSize = &Platform_GetWindowSize; }
            if (pio.Platform_GetWindowFramebufferScale && pio.Platform_GetWindowFramebufferScale != &Platform_GetWindowFramebufferScale) { s_Platform_GetWindowFramebufferScale = pio.Platform_GetWindowFramebufferScale; pio.Platform_GetWindowFramebufferScale = &Platform_GetWindowFramebufferScale; }
            if (pio.Platform_SetWindowFocus && pio.Platform_SetWindowFocus != &Platform_SetWindowFocus) { s_Platform_SetWindowFocus = pio.Platform_SetWindowFocus; pio.Platform_SetWindowFocus = &Platform_SetWindowFocus; }
            if (pio.Platform_GetWindowFocus && pio.Platform_GetWindowFocus != &Platform_GetWindowFocus) { s_Platform_GetWindowFocus = pio.Platform_GetWindowFocus; pio.Platform_GetWindowFocus = &Platform_GetWindowFocus; }
            if (pio.Platform_GetWindowMinimized && pio.Platform_GetWindowMinimized != &Platform_GetWindowMinimized) { s_Platform_GetWindowMinimized = pio.Platform_GetWindowMinimized; pio.Platform_GetWindowMinimized = &Platform_GetWindowMinimized; }
            if (pio.Platform_SetWindowTitle && pio.Platform_SetWindowTitle != &Platform_SetWindowTitle) { s_Platform_SetWindowTitle = pio.Platform_SetWindowTitle; pio.Platform_SetWindowTitle = &Platform_SetWindowTitle; }
            if (pio.Platform_SetWindowAlpha && pio.Platform_SetWindowAlpha != &Platform_SetWindowAlpha) { s_Platform_SetWindowAlpha = pio.Platform_SetWindowAlpha; pio.Platform_SetWindowAlpha = &Platform_SetWindowAlpha; }
            if (pio.Platform_UpdateWindow && pio.Platform_UpdateWindow != &Platform_UpdateWindow) { s_Platform_UpdateWindow = pio.Platform_UpdateWindow; pio.Platform_UpdateWindow = &Platform_UpdateWindow; }
            if (pio.Platform_RenderWindow && pio.Platform_RenderWindow != &Platform_RenderWindow) { s_Platform_RenderWindow = pio.Platform_RenderWindow; pio.Platform_RenderWindow = &Platform_RenderWindow; }
            if (pio.Platform_SwapBuffers && pio.Platform_SwapBuffers != &Platform_SwapBuffers) { s_Platform_SwapBuffers = pio.Platform_SwapBuffers; pio.Platform_SwapBuffers = &Platform_SwapBuffers; }
            if (pio.Platform_GetWindowDpiScale && pio.Platform_GetWindowDpiScale != &Platform_GetWindowDpiScale) { s_Platform_GetWindowDpiScale = pio.Platform_GetWindowDpiScale; pio.Platform_GetWindowDpiScale = &Platform_GetWindowDpiScale; }
            if (pio.Renderer_CreateWindow && pio.Renderer_CreateWindow != &Renderer_CreateWindow) { s_Renderer_CreateWindow = pio.Renderer_CreateWindow; pio.Renderer_CreateWindow = &Renderer_CreateWindow; }
            if (pio.Renderer_DestroyWindow && pio.Renderer_DestroyWindow != &Renderer_DestroyWindow) { s_Renderer_DestroyWindow = pio.Renderer_DestroyWindow; pio.Renderer_DestroyWindow = &Renderer_DestroyWindow; }
            if (pio.Renderer_SetWindowSize && pio.Renderer_SetWindowSize != &Renderer_SetWindowSize) { s_Renderer_SetWindowSize = pio.Renderer_SetWindowSize; pio.Renderer_SetWindowSize = &Renderer_SetWindowSize; }
            if (pio.Renderer_RenderWindow && pio.Renderer_RenderWindow != &Renderer_RenderWindow) { s_Renderer_RenderWindow = pio.Renderer_RenderWindow; pio.Renderer_RenderWindow = &Renderer_RenderWindow; }
            if (pio.Renderer_SwapBuffers && pio.Renderer_SwapBuffers != &Renderer_SwapBuffers) { s_Renderer_SwapBuffers = pio.Renderer_SwapBuffers; pio.Renderer_SwapBuffers = &Renderer_SwapBuffers; }
        }
    }


    // ImGui Vulkan backend: function interposition --------------------------------------------------------------------
    // The backend reaches Vulkan only through the table filled by ImGui_ImplVulkan_LoadFunctions(). load_function() hands
    // it the real entry points, except for the few wrapped below (the ImGui sources stay untouched):
    //
    //  - vkAcquireNextImageKHR / vkQueuePresentKHR: the backend rebuilds a detached window's swapchain whenever one of
    //    them returns VK_SUBOPTIMAL_KHR, and every rebuild starts with vkDeviceWaitIdle(): the main thread blocks until
    //    the GPU has finished the frame that was just submitted. A window system that reports SUBOPTIMAL again and again
    //    turns that into a stall per frame (suspected cause of +3.3 ms in 76 of 300 frames, trace 2026-11-11 15:03; the
    //    counters below tell). SUBOPTIMAL only says the swapchain could match the surface better: presenting still works,
    //    and this renderer ignores it for its own swapchains too. The wrappers report VK_SUCCESS instead, unless the
    //    surface has another size than the swapchain (then a rebuild changes something and the backend gets to do it).
    //    VK_ERROR_OUT_OF_DATE_KHR and every other result pass through unchanged. Resizes made through ImGui never
    //    depended on SUBOPTIMAL: they reach the backend through Renderer_SetWindowSize
    //  - vkCreateSwapchainKHR / vkDestroySwapchainKHR: remember surface + extent per swapchain for that size check
    //  - vkDeviceWaitIdle / vkQueueWaitIdle / vkWaitForFences (+ acquire): wait zones, so a blocking call inside the
    //    backend shows up by name in a trace instead of as self time of "ImGui renderer: render window"
    namespace imgui_vk {

        // A swapchain that reports SUBOPTIMAL is compared with its surface at most this often: the query is a round trip
        // to the window system, and a swapchain may go on reporting SUBOPTIMAL with every acquire and present
        static constexpr u64                                    EXTENT_CHECK_INTERVAL_NS = 500'000'000;

        struct tracked_swapchain {
            VkSwapchainKHR                                      swapchain = VK_NULL_HANDLE;
            VkSurfaceKHR                                        surface = VK_NULL_HANDLE;
            VkExtent2D                                          extent{};
            u64                                                 next_extent_check_ns = 0;
        };

        // Real entry points (set by load_function)
        static PFN_vkAcquireNextImageKHR                        s_acquire_next_image = nullptr;
        static PFN_vkQueuePresentKHR                            s_queue_present = nullptr;
        static PFN_vkCreateSwapchainKHR                         s_create_swapchain = nullptr;
        static PFN_vkDestroySwapchainKHR                        s_destroy_swapchain = nullptr;
        static PFN_vkDeviceWaitIdle                             s_device_wait_idle = nullptr;
        static PFN_vkQueueWaitIdle                              s_queue_wait_idle = nullptr;
        static PFN_vkWaitForFences                              s_wait_for_fences = nullptr;
        static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR    s_get_surface_capabilities = nullptr;

        static VkPhysicalDevice                                 s_physical_device = VK_NULL_HANDLE;
        static std::vector<tracked_swapchain>                   s_swapchains{};         // swapchains of detached windows
        static handle                                           s_stats_provider = INVALID_HANDLE;

        // Totals since imgui_init(), shown as "ImGui viewports" in the stats HUD and in the trace summary
        static std::atomic<u64>                                 s_suboptimal_ignored{ 0 };
        static std::atomic<u64>                                 s_suboptimal_forwarded{ 0 };
        static std::atomic<u64>                                 s_out_of_date{ 0 };
        static std::atomic<u64>                                 s_swapchains_created{ 0 };
        static std::atomic<u64>                                 s_device_idle_waits{ 0 };


        // [swapchain] reported SUBOPTIMAL. True: hide it from the backend, a rebuild would create the same swapchain again
        static bool can_ignore_suboptimal(VkSwapchainKHR swapchain) {

            const auto it = std::find_if(s_swapchains.begin(), s_swapchains.end(),
                [swapchain](const tracked_swapchain& tracked) { return tracked.swapchain == swapchain; });
            if (it == s_swapchains.end() || !s_get_surface_capabilities || s_physical_device == VK_NULL_HANDLE)
                return true;

            const u64 now = GLT::debug::trace::now_ns();
            if (now < it->next_extent_check_ns)
                return true;
            it->next_extent_check_ns = now + EXTENT_CHECK_INTERVAL_NS;

            VkSurfaceCapabilitiesKHR capabilities{};
            if (s_get_surface_capabilities(s_physical_device, it->surface, &capabilities) != VK_SUCCESS)
                return true;

            // 0xFFFFFFFF: the surface takes its size from the swapchain (Wayland). 0: minimized, nothing to rebuild to
            const VkExtent2D current = capabilities.currentExtent;
            if (current.width == 0xFFFFFFFFu || current.height == 0xFFFFFFFFu || current.width == 0 || current.height == 0)
                return true;
            return current.width == it->extent.width && current.height == it->extent.height;
        }


        static VkResult filter_swapchain_result(VkResult result, const VkSwapchainKHR* swapchains, u32 swapchain_count) {

            if (result == VK_ERROR_OUT_OF_DATE_KHR)
                s_out_of_date.fetch_add(1, std::memory_order_relaxed);
            if (result != VK_SUBOPTIMAL_KHR)
                return result;

            bool ignore = true;
            for (u32 i = 0; i < swapchain_count; ++i)
                ignore = can_ignore_suboptimal(swapchains[i]) && ignore;

            (ignore ? s_suboptimal_ignored : s_suboptimal_forwarded).fetch_add(1, std::memory_order_relaxed);
            return ignore ? VK_SUCCESS : VK_SUBOPTIMAL_KHR;
        }


        static VKAPI_ATTR VkResult VKAPI_CALL acquire_next_image(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
            VkSemaphore semaphore, VkFence fence, uint32_t* image_index) {

            VkResult result = VK_SUCCESS;
            {
                GLT_PROFILE_WAIT("ImGui viewport: acquire swapchain image");
                result = s_acquire_next_image(device, swapchain, timeout, semaphore, fence, image_index);
            }
            return filter_swapchain_result(result, &swapchain, 1);
        }


        // Runs inside the "ImGui renderer: present window" wait zone
        static VKAPI_ATTR VkResult VKAPI_CALL queue_present(VkQueue queue, const VkPresentInfoKHR* present_info) {

            const VkResult result = s_queue_present(queue, present_info);
            const VkResult filtered = filter_swapchain_result(result, present_info->pSwapchains, present_info->swapchainCount);
            if (filtered != result && present_info->pResults)
                for (u32 i = 0; i < present_info->swapchainCount; ++i)
                    if (present_info->pResults[i] == VK_SUBOPTIMAL_KHR)
                        present_info->pResults[i] = VK_SUCCESS;
            return filtered;
        }


        static VKAPI_ATTR VkResult VKAPI_CALL create_swapchain(VkDevice device, const VkSwapchainCreateInfoKHR* create_info,
            const VkAllocationCallbacks* allocator, VkSwapchainKHR* swapchain) {

            const VkResult result = s_create_swapchain(device, create_info, allocator, swapchain);
            if (result == VK_SUCCESS) {
                s_swapchains.push_back(tracked_swapchain{ *swapchain, create_info->surface, create_info->imageExtent, 0 });
                s_swapchains_created.fetch_add(1, std::memory_order_relaxed);
            }
            return result;
        }


        static VKAPI_ATTR void VKAPI_CALL destroy_swapchain(VkDevice device, VkSwapchainKHR swapchain,
            const VkAllocationCallbacks* allocator) {

            std::erase_if(s_swapchains, [swapchain](const tracked_swapchain& tracked) { return tracked.swapchain == swapchain; });
            s_destroy_swapchain(device, swapchain, allocator);
        }


        static VKAPI_ATTR VkResult VKAPI_CALL device_wait_idle(VkDevice device) {

            GLT_PROFILE_WAIT("ImGui backend: vkDeviceWaitIdle (swapchain create / rebuild / destroy)");
            s_device_idle_waits.fetch_add(1, std::memory_order_relaxed);
            return s_device_wait_idle(device);
        }


        static VKAPI_ATTR VkResult VKAPI_CALL queue_wait_idle(VkQueue queue) {

            GLT_PROFILE_WAIT("ImGui backend: vkQueueWaitIdle (texture upload)");
            return s_queue_wait_idle(queue);
        }


        // The backend polls its frame fence with a 100 ns timeout until it is signalled. Signalled already (the normal
        // case): no zone. Otherwise the wait shows up in a trace and blocks in the driver (1 ms per call) instead of spinning
        static VKAPI_ATTR VkResult VKAPI_CALL wait_for_fences(VkDevice device, uint32_t fence_count, const VkFence* fences,
            VkBool32 wait_all, uint64_t timeout) {

            const VkResult ready = s_wait_for_fences(device, fence_count, fences, wait_all, 0);
            if (ready != VK_TIMEOUT || timeout == 0)
                return ready;

            GLT_PROFILE_WAIT("ImGui viewport: wait for frame fence");
            return s_wait_for_fences(device, fence_count, fences, wait_all, std::max<uint64_t>(timeout, 1'000'000));
        }


        // Loader for ImGui_ImplVulkan_LoadFunctions(). [user_data]: the vk::Instance
        static PFN_vkVoidFunction load_function(const char* function_name, void* user_data) {

            const VkInstance instance = static_cast<VkInstance>(*static_cast<vk::Instance*>(user_data));
            const PFN_vkVoidFunction real = vkGetInstanceProcAddr(instance, function_name);
            if (!real)
                return nullptr;

            const std::string_view name{ function_name };
            if (name == "vkAcquireNextImageKHR") {
                s_acquire_next_image = reinterpret_cast<PFN_vkAcquireNextImageKHR>(real);
                return reinterpret_cast<PFN_vkVoidFunction>(&acquire_next_image);
            }
            if (name == "vkQueuePresentKHR") {
                s_queue_present = reinterpret_cast<PFN_vkQueuePresentKHR>(real);
                return reinterpret_cast<PFN_vkVoidFunction>(&queue_present);
            }
            if (name == "vkCreateSwapchainKHR") {
                s_create_swapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(real);
                return reinterpret_cast<PFN_vkVoidFunction>(&create_swapchain);
            }
            if (name == "vkDestroySwapchainKHR") {
                s_destroy_swapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(real);
                return reinterpret_cast<PFN_vkVoidFunction>(&destroy_swapchain);
            }
            if (name == "vkDeviceWaitIdle") {
                s_device_wait_idle = reinterpret_cast<PFN_vkDeviceWaitIdle>(real);
                return reinterpret_cast<PFN_vkVoidFunction>(&device_wait_idle);
            }
            if (name == "vkQueueWaitIdle") {
                s_queue_wait_idle = reinterpret_cast<PFN_vkQueueWaitIdle>(real);
                return reinterpret_cast<PFN_vkVoidFunction>(&queue_wait_idle);
            }
            if (name == "vkWaitForFences") {
                s_wait_for_fences = reinterpret_cast<PFN_vkWaitForFences>(real);
                return reinterpret_cast<PFN_vkVoidFunction>(&wait_for_fences);
            }
            if (name == "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")
                s_get_surface_capabilities = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(real);
            return real;
        }


        // Before ImGui_ImplVulkan_LoadFunctions(): forget the previous backend instance
        static void reset(VkPhysicalDevice physical_device) {

            s_physical_device = physical_device;
            s_swapchains.clear();
            s_suboptimal_ignored = 0;
            s_suboptimal_forwarded = 0;
            s_out_of_date = 0;
            s_swapchains_created = 0;
            s_device_idle_waits = 0;
        }


        // Idempotent. The provider reads the counters above, so it must be gone before this plugin is unloaded
        static void register_stats() {

            if (s_stats_provider != INVALID_HANDLE)
                return;
            s_stats_provider = GLT::debug::register_provider(GLT::debug::stats_provider{
                .name = "ImGui viewports",
                .fill = [](GLT::debug::system_stats& out) {
                    const auto row = [&out](const char* label, const std::atomic<u64>& counter) {
                        out.custom.push_back({ label, static_cast<f32>(counter.load(std::memory_order_relaxed)), "%.0f" });
                    };
                    row("suboptimal results ignored (no swapchain rebuild)", s_suboptimal_ignored);
                    row("suboptimal results forwarded (surface size changed)", s_suboptimal_forwarded);
                    row("out-of-date results", s_out_of_date);
                    row("swapchains created", s_swapchains_created);
                    row("vkDeviceWaitIdle calls", s_device_idle_waits);
                }
            });
        }


        static void unregister_stats() {

            if (s_stats_provider == INVALID_HANDLE)
                return;
            GLT::debug::unregister_provider(s_stats_provider);
            s_stats_provider = INVALID_HANDLE;
        }
    }


    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void scene_renderer::imgui_init() {
        GLT_PROFILE_SCOPE("scene_renderer::imgui_init");

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

        // The backend gets its Vulkan entry points from imgui_vk::load_function: the real ones, a few of them wrapped
        vk::Instance inst = m_plugin.get_instance();
        imgui_vk::reset(static_cast<VkPhysicalDevice>(m_plugin.get_physical_device_ref()));
        ASSERT(ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_4, &imgui_vk::load_function, &inst),
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

        imgui_hooks::install();                         // after both backends registered their callbacks
        if (m_imgui_initialized)
            imgui_vk::register_stats();

        // Same attachment format as m_imgui_render_pass (so ImGui's pipeline and the framebuffers stay compatible), but it
        // clears the backbuffer: used on UI-only frames instead of a separate clear pass
        {
            vk::AttachmentDescription attachment{};
            attachment.format = m_swapchain.swapchain_format;
            attachment.samples = vk::SampleCountFlagBits::e1;
            attachment.loadOp = vk::AttachmentLoadOp::eClear;
            attachment.storeOp = vk::AttachmentStoreOp::eStore;
            attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
            attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
            attachment.initialLayout = vk::ImageLayout::eUndefined;
            attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

            vk::AttachmentReference color{ 0, vk::ImageLayout::eColorAttachmentOptimal };
            vk::SubpassDescription subpass{};
            subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &color;

            vk::SubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

            vk::RenderPassCreateInfo rpci{};
            rpci.attachmentCount = 1;
            rpci.pAttachments = &attachment;
            rpci.subpassCount = 1;
            rpci.pSubpasses = &subpass;
            rpci.dependencyCount = 1;
            rpci.pDependencies = &dependency;
            m_imgui_clear_render_pass = m_plugin.get_vk_device().createRenderPass(rpci);
        }
        LOG(trace, " ImGui initialised for window [{}, {}]", m_window.index, m_window.generation);
    }


    void scene_renderer::imgui_shutdown() {
        GLT_PROFILE_SCOPE("scene_renderer::imgui_shutdown");

        if (m_offscreen || !m_imgui_initialized)
            return;

        imgui_vk::unregister_stats();

        // Ensure the shared context is current before tearing down its backends
        ImGui::SetCurrentContext(imgui_config::get_context_imgui());
        m_plugin.get_vk_device().waitIdle();
        destroy_imgui_textures();                       // before the backend (RemoveTexture needs it)
        if (m_imgui_clear_render_pass) {
            m_plugin.get_vk_device().destroyRenderPass(m_imgui_clear_render_pass);
            m_imgui_clear_render_pass = nullptr;
        }

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
        GLT_PROFILE_SCOPE("ImGui new frame");
        ImGui::SetCurrentContext(imgui_config::get_context_imgui());
        ImGui_ImplVulkan_NewFrame();
        m_plugin.get_window()->begin_imgui_frame(m_window);
        ImGui::NewFrame();
    }


    void scene_renderer::end_imgui_frame() {

        VALIDATE_IMGUI_INIT
        GLT_PROFILE_SCOPE("ImGui render (draw lists)");
        ImGui::EndFrame();
        ImGui::Render();    // produces draw data consumed by add_imgui_pass()
    }


    void scene_renderer::abort_imgui_frame() {
        GLT_PROFILE_SCOPE("scene_renderer::abort_imgui_frame");

        if (!m_imgui_initialized)
            return;
        ImGui::SetCurrentContext(imgui_config::get_context_imgui());

        // Render() (not just EndFrame()) so every viewport gets valid DrawData: RenderPlatformWindowsDefault() dereferences
        // viewport->DrawData, which is null after a bare EndFrame() -> segfault. The main viewport's draw data is simply not
        // recorded this frame; secondary platform windows (e.g. a detached stats window) keep rendering, even while the main
        // window is minimized
        ImGui::Render();

        m_imgui_platform_pending = true;
        render_imgui_platform_windows();
    }


    void scene_renderer::add_imgui_pass() {
        GLT_PROFILE_SCOPE("scene_renderer::add_imgui_pass");

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

                // Font atlas / ImGui textures: uploaded here, in this frame's command buffer (outside the render pass).
                // The backend would otherwise do it itself with a vkQueueWaitIdle per update, which also waits for the
                // previous frames' presents (vsync) -> tens of ms of CPU stall every time a glyph gets added
                upload_imgui_textures(ctx.cmd);

                vk::RenderPassBeginInfo rpbi{};
                rpbi.renderPass = m_imgui_clears_backbuffer ? m_imgui_clear_render_pass : m_imgui_render_pass;
                rpbi.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
                rpbi.renderArea.extent = m_swapchain.swapchain_extent;

                std::array<vk::ClearValue, 1> clear{};
                clear[0].color = { m_clear_color.r, m_clear_color.g, m_clear_color.b, m_clear_color.a };
                rpbi.clearValueCount = static_cast<u32>(clear.size());
                rpbi.pClearValues = clear.data();

                ctx.cmd.beginRenderPass(rpbi, vk::SubpassContents::eInline);
                {
                    GLT_PROFILE_SCOPE("ImGui record draw data");
                    ImGui_ImplVulkan_RenderDrawData(const_cast<ImDrawData*>(dd), ctx.cmd);
                }
                ctx.cmd.endRenderPass();

                // Detached ImGui windows are rendered + presented by the backend with their own submits. Doing that here
                // (inside this target's command recording, before its own submit + present) used to put every secondary
                // window's acquire / fence wait / present into "Graph execute (record)". Now: after present, see render()
                m_imgui_platform_pending = true;
            });
    }


    // Detached ImGui windows (multi-viewport). Called once per frame after this target submitted and presented
    void scene_renderer::render_imgui_platform_windows() {
        GLT_PROFILE_SCOPE("scene_renderer::render_imgui_platform_windows");

        if (!m_imgui_initialized || !m_imgui_platform_pending)
            return;
        m_imgui_platform_pending = false;

        ImGuiIO& io = ImGui::GetIO();
        if (!(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable))
            return;

        {
            GLT_PROFILE_SCOPE("ImGui update platform windows");
            ImGui::UpdatePlatformWindows();
        }
        {
            GLT_PROFILE_SCOPE("ImGui render platform windows");
            ImGui::RenderPlatformWindowsDefault();      // per window: acquire, fence wait, record, submit, present
        }
    }


    // Honours ImGui's texture requests (ImGuiBackendFlags_RendererHasTextures) before the backend sees them: images are
    // created / updated through [cmd] with staging buffers that are retired once the frame is done, so nothing blocks.
    // Statuses are OK afterwards, so ImGui_ImplVulkan_RenderDrawData skips its own (queue-idle) upload path
    void scene_renderer::upload_imgui_textures(vk::CommandBuffer cmd) {
        GLT_PROFILE_SCOPE("scene_renderer::upload_imgui_textures");

        auto& textures = ImGui::GetPlatformIO().Textures;
        if (textures.empty())
            return;

        GLT_PROFILE_SCOPE("ImGui texture uploads");
        auto* dev = m_plugin.get_dev();
        const vk::Device device = m_plugin.get_vk_device();

        auto barrier = [&](vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
            vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access) {
            vk::ImageMemoryBarrier2 b{};
            b.srcStageMask = src_stage;
            b.srcAccessMask = src_access;
            b.dstStageMask = dst_stage;
            b.dstAccessMask = dst_access;
            b.oldLayout = from;
            b.newLayout = to;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
            b.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
            vk::DependencyInfo di{};
            di.imageMemoryBarrierCount = 1;
            di.pImageMemoryBarriers = &b;
            cmd.pipelineBarrier2(di);
        };

        // Copies [rects] of [tex] into a fresh staging buffer and records the copies. The buffer is retired with the frame
        auto upload_rects = [&](ImTextureData* tex, vk::Image image, const std::vector<ImTextureRect>& rects) {

            const u32 bpp = static_cast<u32>(tex->BytesPerPixel);
            vk::DeviceSize total = 0;
            for (const auto& r : rects)
                total += static_cast<vk::DeviceSize>(r.w) * r.h * bpp;
            if (total == 0)
                return;

            auto staging = dev->create_buffer(total, vk::BufferUsageFlagBits::eTransferSrc,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            auto* dst = static_cast<std::byte*>(dev->map_buffer(staging));

            std::vector<vk::BufferImageCopy> copies;
            copies.reserve(rects.size());
            vk::DeviceSize offset = 0;
            for (const auto& r : rects) {
                const size_t row_bytes = static_cast<size_t>(r.w) * bpp;
                for (u32 y = 0; y < r.h; ++y)
                    std::memcpy(dst + offset + y * row_bytes, tex->GetPixelsAt(r.x, r.y + static_cast<int>(y)), row_bytes);

                vk::BufferImageCopy c{};
                c.bufferOffset = offset;
                c.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
                c.imageOffset = vk::Offset3D{ static_cast<i32>(r.x), static_cast<i32>(r.y), 0 };
                c.imageExtent = vk::Extent3D{ r.w, r.h, 1 };
                copies.push_back(c);
                offset += static_cast<vk::DeviceSize>(row_bytes) * r.h;
            }
            dev->unmap_buffer(staging);

            cmd.copyBufferToImage(staging.buffer, image, vk::ImageLayout::eTransferDstOptimal, copies);
            m_shared->retire_buffer(staging);
        };

        using PS = vk::PipelineStageFlagBits2;
        using AF = vk::AccessFlagBits2;
        using IL = vk::ImageLayout;

        for (ImTextureData* tex : textures) {

            switch (tex->Status) {

                case ImTextureStatus_WantCreate: {

                    const bool alpha8 = tex->Format == ImTextureFormat_Alpha8;
                    vk::ImageCreateInfo ici{};
                    ici.imageType = vk::ImageType::e2D;
                    ici.format = alpha8 ? vk::Format::eR8Unorm : vk::Format::eR8G8B8A8Unorm;
                    ici.extent = vk::Extent3D{ static_cast<u32>(tex->Width), static_cast<u32>(tex->Height), 1 };
                    ici.mipLevels = 1;
                    ici.arrayLayers = 1;
                    ici.samples = vk::SampleCountFlagBits::e1;
                    ici.tiling = vk::ImageTiling::eOptimal;
                    ici.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
                    ici.initialLayout = IL::eUndefined;

                    imgui_texture t{};
                    t.image = dev->create_image(ici, 0);

                    vk::ImageViewCreateInfo vci{};
                    vci.image = t.image.image;
                    vci.viewType = vk::ImageViewType::e2D;
                    vci.format = ici.format;
                    if (alpha8)                                  // ImGui's shader samples RGBA: white with coverage in alpha
                        vci.components = vk::ComponentMapping{ vk::ComponentSwizzle::eOne, vk::ComponentSwizzle::eOne,
                            vk::ComponentSwizzle::eOne, vk::ComponentSwizzle::eR };
                    vci.subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
                    t.view = device.createImageView(vci);

                    barrier(t.image.image, IL::eUndefined, IL::eTransferDstOptimal, PS::eNone, {}, PS::eCopy, AF::eTransferWrite);
                    upload_rects(tex, t.image.image, { ImTextureRect{ 0, 0, static_cast<unsigned short>(tex->Width),
                        static_cast<unsigned short>(tex->Height) } });
                    barrier(t.image.image, IL::eTransferDstOptimal, IL::eShaderReadOnlyOptimal, PS::eCopy, AF::eTransferWrite,
                        PS::eFragmentShader, AF::eShaderSampledRead);

                    t.set = ImGui_ImplVulkan_AddTexture(static_cast<VkImageView>(t.view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    tex->SetTexID((ImTextureID)t.set);
                    tex->SetStatus(ImTextureStatus_OK);
                    m_imgui_textures[tex] = t;
                    break;
                }

                case ImTextureStatus_WantUpdates: {

                    const auto it = m_imgui_textures.find(tex);
                    if (it == m_imgui_textures.end())
                        break;                                  // not ours (created before we took over): backend handles it
                    std::vector<ImTextureRect> rects(tex->Updates.begin(), tex->Updates.end());
                    if (rects.empty())
                        rects.push_back(tex->UpdateRect);
                    const vk::Image image = it->second.image.image;
                    barrier(image, IL::eShaderReadOnlyOptimal, IL::eTransferDstOptimal, PS::eFragmentShader, AF::eShaderSampledRead,
                        PS::eCopy, AF::eTransferWrite);
                    upload_rects(tex, image, rects);
                    barrier(image, IL::eTransferDstOptimal, IL::eShaderReadOnlyOptimal, PS::eCopy, AF::eTransferWrite,
                        PS::eFragmentShader, AF::eShaderSampledRead);
                    tex->SetStatus(ImTextureStatus_OK);
                    break;
                }

                case ImTextureStatus_WantDestroy: {

                    // ImGui keeps asking every frame until we comply. Once the texture has not been referenced by any draw list
                    // for longer than the frames in flight (+ margin for detached-window swapchains), no pending GPU work can
                    // sample it and it is destroyed right away (not via the retire queue: that may run after the backend is gone)
                    if (tex->UnusedFrames < static_cast<int>(MAX_CONCURRENT_FRAMES) + 2)
                        break;
                    const auto it = m_imgui_textures.find(tex);
                    if (it == m_imgui_textures.end())
                        break;                                  // not ours
                    ImGui_ImplVulkan_RemoveTexture(it->second.set);
                    device.destroyImageView(it->second.view);
                    dev->destroy_image(it->second.image);
                    m_imgui_textures.erase(it);
                    tex->SetTexID(ImTextureID_Invalid);
                    tex->SetStatus(ImTextureStatus_Destroyed);
                    break;
                }

                default:
                    break;
            }
        }
    }


    // Shutdown: the GPU is idle (caller waited)
    void scene_renderer::destroy_imgui_textures() {
        GLT_PROFILE_SCOPE("scene_renderer::destroy_imgui_textures");

        const vk::Device device = m_plugin.get_vk_device();
        for (auto& [key, t] : m_imgui_textures) {
            auto* tex = const_cast<ImTextureData*>(static_cast<const ImTextureData*>(key));
            ImGui_ImplVulkan_RemoveTexture(t.set);
            device.destroyImageView(t.view);
            m_plugin.get_dev()->destroy_image(t.image);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
        m_imgui_textures.clear();
    }


    void scene_renderer::recreate_imgui_framebuffers() {
        GLT_PROFILE_SCOPE("scene_renderer::recreate_imgui_framebuffers");

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

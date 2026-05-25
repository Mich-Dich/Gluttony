
#include "util/pch.h"

#include <vk_ray/vk_ray.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>

#include <application.h>
#include <plugin_system/i_window_plugin.h>

// #include "initializer.h"

#include "utils.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::renderer_vk_ray::utils {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    vk::SurfaceKHR create_surface(vk::Instance instance) {
        ASSERT(glfwVulkanSupported() == GLFW_TRUE, "", "Vulkan is not supported on this system");

        VkSurfaceKHR rawSurface;
        VkResult result = glfwCreateWindowSurface(static_cast<VkInstance>(instance), 
            (GLFWwindow*)GLT::application::get().get_window()->get_native_window_handle(), nullptr, &rawSurface);

        ASSERT_VK(result, "", "Failed to create Vulkan Surface");
        return vk::SurfaceKHR(rawSurface);
    }


    IGNORE_UNUSED_PARAMETER_START
    IGNORE_UNUSED_VARIABLE_START
    
    VkBool32 vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,VkDebugUtilsMessageTypeFlagsEXT message_type,
        const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data) {

        // Map Vulkan severity levels to your log levels
        if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            LOG(error, "[VULKAN] {}", p_callback_data->pMessage)
        
        else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            LOG(warn, "[VULKAN] {}", p_callback_data->pMessage)
        
        else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            LOG(trace, "[VULKAN] {}", p_callback_data->pMessage)
        
        else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
            LOG(trace, "[VULKAN] {}", p_callback_data->pMessage)

        return VK_FALSE;
    }

    IGNORE_UNUSED_VARIABLE_STOP
    IGNORE_UNUSED_PARAMETER_STOP
    

    const char* error_string(const vk::Result result) {

        switch (result)
        {
            case vk::Result::eSuccess:                                      return "success";
            case vk::Result::eNotReady:                                     return "not ready";
            case vk::Result::eTimeout:                                      return "timeout";
            case vk::Result::eEventSet:                                     return "event set";
            case vk::Result::eEventReset:                                   return "event reset";
            case vk::Result::eIncomplete:                                   return "incomplete";
            case vk::Result::eErrorOutOfHostMemory:                         return "error out of host memory";
            case vk::Result::eErrorOutOfDeviceMemory:                       return "error out of device memory";
            case vk::Result::eErrorInitializationFailed:                    return "error initialization failed";
            case vk::Result::eErrorDeviceLost:                              return "error device lost";
            case vk::Result::eErrorMemoryMapFailed:                         return "error memory map failed";
            case vk::Result::eErrorLayerNotPresent:                         return "error layer not present";
            case vk::Result::eErrorExtensionNotPresent:                     return "error extension not present";
            case vk::Result::eErrorFeatureNotPresent:                       return "error feature not present";
            case vk::Result::eErrorIncompatibleDriver:                      return "error incompatible driver";
            case vk::Result::eErrorTooManyObjects:                          return "error too many objects";
            case vk::Result::eErrorFormatNotSupported:                      return "error format not supported";
            case vk::Result::eErrorFragmentedPool:                          return "error fragmented pool";
            case vk::Result::eErrorUnknown:                                 return "error unknown";
            // case vk::Result::eErrorValidationFailed:                        return "error validation failed";
            case vk::Result::eErrorValidationFailedEXT:                     return "error validation failed ext";
            case vk::Result::eErrorOutOfPoolMemory:                         return "error out of pool memory";
            // case vk::Result::eErrorOutOfPoolMemoryKHR:                      return "error out of pool memory khr";
            case vk::Result::eErrorInvalidExternalHandle:                   return "error invalid external handle";
            // case vk::Result::eErrorInvalidExternalHandleKHR:                return "error invalid external handle khr";
            case vk::Result::eErrorInvalidOpaqueCaptureAddress:             return "error invalid opaque capture address";
            // case vk::Result::eErrorInvalidDeviceAddressEXT:                 return "error invalid device address ext";
            // case vk::Result::eErrorInvalidOpaqueCaptureAddressKHR:          return "error invalid opaque capture address khr";
            case vk::Result::eErrorFragmentation:                           return "error fragmentation";
            // case vk::Result::eErrorFragmentationEXT:                        return "error fragmentation ext";
            case vk::Result::ePipelineCompileRequired:                      return "pipeline compile required";
            // case vk::Result::ePipelineCompileRequiredEXT:                   return "pipeline compile required ext";
            // case vk::Result::eErrorPipelineCompileRequiredEXT:              return "error pipeline compile required ext";
            case vk::Result::eErrorNotPermitted:                            return "error not permitted";
            // case vk::Result::eErrorNotPermittedEXT:                         return "error not permitted ext";
            // case vk::Result::eErrorNotPermittedKHR:                         return "error not permitted khr";
            case vk::Result::eErrorSurfaceLostKHR:                          return "error surface lost khr";
            case vk::Result::eErrorNativeWindowInUseKHR:                    return "error native window in use khr";
            case vk::Result::eSuboptimalKHR:                                return "suboptimal khr";
            case vk::Result::eErrorOutOfDateKHR:                            return "error out of date khr";
            case vk::Result::eErrorIncompatibleDisplayKHR:                  return "error incompatible display khr";
            case vk::Result::eErrorInvalidShaderNV:                         return "error invalid shader nv";
            case vk::Result::eErrorImageUsageNotSupportedKHR:               return "error image usage not supported khr";
            case vk::Result::eErrorVideoPictureLayoutNotSupportedKHR:       return "error video picture layout not supported khr";
            case vk::Result::eErrorVideoProfileOperationNotSupportedKHR:    return "error video profile operation not supported khr";
            case vk::Result::eErrorVideoProfileFormatNotSupportedKHR:       return "error video profile format not supported khr";
            case vk::Result::eErrorVideoProfileCodecNotSupportedKHR:        return "error video profile codec not supported khr";
            case vk::Result::eErrorVideoStdVersionNotSupportedKHR:          return "error video std version not supported khr";
            case vk::Result::eErrorInvalidDrmFormatModifierPlaneLayoutEXT:  return "error invalid drm format modifier plane layout ext";
            // case vk::Result::eErrorPresentTimingQueueFullEXT:               return "error present timing queue full ext";
            case vk::Result::eThreadIdleKHR:                                return "thread idle khr";
            case vk::Result::eThreadDoneKHR:                                return "thread done khr";
            case vk::Result::eOperationDeferredKHR:                         return "operation deferred khr";
            case vk::Result::eOperationNotDeferredKHR:                      return "operation not deferred khr";
            case vk::Result::eErrorInvalidVideoStdParametersKHR:            return "error invalid video std parameters khr";
            case vk::Result::eErrorCompressionExhaustedEXT:                 return "error compression exhausted ext";
            case vk::Result::eIncompatibleShaderBinaryEXT:                  return "incompatible shader binary ext";
            // case vk::Result::eErrorIncompatibleShaderBinaryEXT:             return "error incompatible shader binary ext";
            case vk::Result::ePipelineBinaryMissingKHR:                     return "pipeline binary missing khr";
            case vk::Result::eErrorNotEnoughSpaceKHR:                       return "error not enough space kh";
            default:                                                        return "unknown error";
        }

    }


    // void begin_imgui_frame(f32 delta_time) {
    //     if (!m_imgui_initialized) return;
    //
    //     ImGui_ImplVulkan_NewFrame();
    //     ImGui_ImplGlfw_NewFrame();
    //     ImGui::NewFrame();
    //
    //     // Example UI - you can customize this
    //     static bool show_demo = true;
    //     if (show_demo) {
    //         ImGui::ShowDemoWindow(&show_demo);
    //     }
    //
    //     // Stats window
    //     ImGui::Begin("Renderer Stats");
    //     ImGui::Text("Frame: %llu", m_frame_count);
    //     ImGui::Text("FPS: %.1f", 1.0f / delta_time);
    //     ImGui::Text("Resolution: %u x %u", m_render_width, m_render_height);
    //     ImGui::Text("Camera Pos: %.2f, %.2f, %.2f",
    //                 m_active_camera->m_position.x,
    //                 m_active_camera->m_position.y,
    //                 m_active_camera->m_position.z);
    //     ImGui::End();
    // }
    //
    // void render_imgui(vk::CommandBuffer cmd_buffer) {
    //     if (!m_imgui_initialized) return;
    //
    //     ImGui::Render();
    //
    //     // Begin render pass
    //     vk::RenderPassBeginInfo rp_info = {};
    //     rp_info.renderPass = m_imgui_render_pass;
    //     rp_info.framebuffer = m_imgui_framebuffers[m_current_swapchain_image];
    //     rp_info.renderArea.offset = vk::Offset2D(0, 0);
    //     rp_info.renderArea.extent = m_swapchain_resources.swapchain_extent;
    //
    //     cmd_buffer.beginRenderPass(rp_info, vk::SubpassContents::eInline);
    //     ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_buffer);
    //     cmd_buffer.endRenderPass();
    //
    //     // Update and Render additional Platform Windows
    //     ImGuiIO& io = ImGui::GetIO();
    //     if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    //         ImGui::UpdatePlatformWindows();
    //         ImGui::RenderPlatformWindowsDefault();
    //     }
    // }


    void transition_image(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout current_layout, vk::ImageLayout new_layout) {

        vk::ImageAspectFlags aspect_mask = (new_layout == vk::ImageLayout::eDepthAttachmentOptimal) ?
            vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eColor;

        vk::ImageMemoryBarrier2 image_barrier{};
        image_barrier.sType = vk::StructureType::eImageMemoryBarrier2;
        image_barrier.srcStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        image_barrier.srcAccessMask = vk::AccessFlagBits2::eMemoryWrite;
        image_barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
        image_barrier.dstAccessMask = vk::AccessFlagBits2::eMemoryWrite | vk::AccessFlagBits2::eMemoryRead;
        image_barrier.oldLayout = current_layout;
        image_barrier.newLayout = new_layout;
        image_barrier.subresourceRange = vk::ImageSubresourceRange(aspect_mask, 0, 1, 0, 1);
        image_barrier.image = image;

        vk::DependencyInfo dependency_I{};
        dependency_I.sType = vk::StructureType::eDependencyInfo;
        dependency_I.imageMemoryBarrierCount = 1;
        dependency_I.pImageMemoryBarriers = &image_barrier;

        cmd.pipelineBarrier2(dependency_I);   // Use member function
    }


    void copy_image_to_image(vk::CommandBuffer cmd, vk::Image source, vk::Image destination, vk::Extent2D srcSize, vk::Extent2D dstSize) {

        vk::ImageBlit2 blit_region{};
        blit_region.sType = vk::StructureType::eImageBlit2;
        blit_region.srcOffsets[1].x = srcSize.width;
        blit_region.srcOffsets[1].y = srcSize.height;
        blit_region.srcOffsets[1].z = 1;
        blit_region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit_region.srcSubresource.baseArrayLayer = 0;
        blit_region.srcSubresource.layerCount = 1;
        blit_region.srcSubresource.mipLevel = 0;

        blit_region.dstOffsets[1].x = dstSize.width;
        blit_region.dstOffsets[1].y = dstSize.height;
        blit_region.dstOffsets[1].z = 1;
        blit_region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit_region.dstSubresource.baseArrayLayer = 0;
        blit_region.dstSubresource.layerCount = 1;
        blit_region.dstSubresource.mipLevel = 0;

        vk::BlitImageInfo2 blit_I{};
        blit_I.sType = vk::StructureType::eBlitImageInfo2;
        blit_I.dstImage = destination;
        blit_I.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
        blit_I.srcImage = source;
        blit_I.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
        blit_I.filter = vk::Filter::eLinear;
        blit_I.regionCount = 1;
        blit_I.pRegions = &blit_region;

        cmd.blitImage2(blit_I);
    }


    void create_imgui_resources(vk::DescriptorPool& imgui_descriptor_pool, vk::Device& device, 
        vr::swapchain_resources& swapchain, vk::RenderPass& imgui_render_pass, vr::instance_wrapper& instance,
        vk::PhysicalDevice& physical_device, vr::command_queues& queues, 
        std::vector<vk::Framebuffer>& imgui_framebuffers, bool& imgui_initialized) {

        // Create descriptor pool for ImGui (optional in newer versions, but still recommended)
        std::vector<vk::DescriptorPoolSize> pool_sizes = {
            { vk::DescriptorType::eSampler, 1000 },
            { vk::DescriptorType::eCombinedImageSampler, 1000 },
            { vk::DescriptorType::eSampledImage, 1000 },
            { vk::DescriptorType::eStorageImage, 1000 },
            { vk::DescriptorType::eUniformTexelBuffer, 1000 },
            { vk::DescriptorType::eStorageTexelBuffer, 1000 },
            { vk::DescriptorType::eUniformBuffer, 1000 },
            { vk::DescriptorType::eStorageBuffer, 1000 },
            { vk::DescriptorType::eUniformBufferDynamic, 1000 },
            { vk::DescriptorType::eStorageBufferDynamic, 1000 },
            { vk::DescriptorType::eInputAttachment, 1000 }
        };

        vk::DescriptorPoolCreateInfo pool_info = {};
        pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
        pool_info.maxSets = 1000 * static_cast<uint32_t>(pool_sizes.size());
        pool_info.poolSizeCount = static_cast<u32>(pool_sizes.size());
        pool_info.pPoolSizes = pool_sizes.data();

        try {
            imgui_descriptor_pool = device.createDescriptorPool(pool_info);
        } catch (const vk::SystemError& e) {
            LOG(error, "Failed to create ImGui descriptor pool: {}", e.what());
            throw;
        }

        // Create render pass for ImGui FIRST (needed for init_info)
        vk::AttachmentDescription attachment = {};
        attachment.format = swapchain.swapchain_format;
        attachment.samples = vk::SampleCountFlagBits::e1;
        // attachment.loadOp = vk::AttachmentLoadOp::eClear;  // Load existing content (our rendered image)
        attachment.loadOp = vk::AttachmentLoadOp::eLoad;
        attachment.storeOp = vk::AttachmentStoreOp::eStore;
        attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
        attachment.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
        attachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

        vk::AttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = vk::ImageLayout::eColorAttachmentOptimal;

        vk::SubpassDescription subpass = {};
        subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;

        vk::SubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        dependency.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

        vk::RenderPassCreateInfo render_pass_info = {};
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        try {
            imgui_render_pass = device.createRenderPass(render_pass_info);
        } catch (const vk::SystemError& e) {
            LOG(error, "Failed to create ImGui render pass: {}", e.what());
            throw;
        }

        // Create ImGui Vulkan backend initialization info for NEW API
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = instance.instance_handle;
        init_info.PhysicalDevice = physical_device;
        init_info.Device = device;
        init_info.QueueFamily = queues.graphics_index;
        init_info.Queue = queues.graphics_queue;
        init_info.PipelineCache = VK_NULL_HANDLE;
        init_info.DescriptorPool = imgui_descriptor_pool;
        init_info.MinImageCount = static_cast<u32>(swapchain.swapchain_images.size());
        init_info.ImageCount = static_cast<u32>(swapchain.swapchain_images.size());
        init_info.Allocator = nullptr;

        // Required: Set the API version
        init_info.ApiVersion = VK_API_VERSION_1_3; // Use 1.2 or 1.3 based on what you initialized

        // Set up pipeline rendering info for render pass
        init_info.PipelineInfoMain.RenderPass = imgui_render_pass;
        init_info.PipelineInfoMain.Subpass = 0;
        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        // For dynamic rendering (if you were using it), you'd need to set PipelineRenderingCreateInfo
        // But since we're using a traditional render pass, we don't need that

        // Min allocation size (optional but recommended to avoid validation warnings)
        init_info.MinAllocationSize = 256; // 256 bytes minimum allocation

        init_info.CheckVkResultFn = [](VkResult err) {
            ASSERT(err == VK_SUCCESS, "", "ImGui Vulkan error: {}", vk::to_string(static_cast<vk::Result>(err)))
        };

        // Initialize ImGui Vulkan backend (single argument in new API)
        ASSERT(ImGui_ImplVulkan_Init(&init_info), "", "Failed to initialize ImGui Vulkan backend");

        // Create framebuffers for each swapchain image
        imgui_framebuffers.resize(swapchain.swapchain_images.size());
        for (size_t x = 0; x < swapchain.swapchain_images.size(); x++) {

            vk::ImageView attachments[] = { swapchain.swapchain_image_views[x] };
            vk::FramebufferCreateInfo fb_info = {};
            fb_info.renderPass = imgui_render_pass;
            fb_info.attachmentCount = 1;
            fb_info.pAttachments = attachments;
            fb_info.width = swapchain.swapchain_extent.width;
            fb_info.height = swapchain.swapchain_extent.height;
            fb_info.layers = 1;

            try {
                imgui_framebuffers[x] = device.createFramebuffer(fb_info);
            } catch (const vk::SystemError& e) {
                LOG(error, "Failed to create ImGui framebuffer: [{}]", e.what());
                throw;
            }
        }

        // Note: In newer ImGui versions, font texture creation is automatic!
        // The first call to ImGui::NewFrame() will create the font texture if needed.
        // No manual font upload required anymore.
        imgui_initialized = true;
    }


    void destroy_imgui_resources(vk::Device& device, std::vector<vk::Framebuffer>& imgui_framebuffers, 
        vk::RenderPass& imgui_render_pass, vk::DescriptorPool& imgui_descriptor_pool, bool& imgui_initialized) {

        device.waitIdle();

        for (auto& framebuffer : imgui_framebuffers) {            // Destroy framebuffers
            if (framebuffer)
                device.destroyFramebuffer(framebuffer);
        }
        imgui_framebuffers.clear();

        if (imgui_render_pass) {                                  // Destroy render pass
            device.destroyRenderPass(imgui_render_pass);
            imgui_render_pass = nullptr;
        }

        ImGui_ImplVulkan_Shutdown();

        if (imgui_descriptor_pool) {                              // Destroy descriptor pool
            device.destroyDescriptorPool(imgui_descriptor_pool);
            imgui_descriptor_pool = nullptr;
        }

        imgui_initialized = false;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

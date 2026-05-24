
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray::utils {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

	#define VK_FLAGS_NONE 0											// Custom define for better code readability
	#define DEFAULT_FENCE_TIMEOUT 100000000000						// Default fence timeout in nanoseconds

    #define ASSERT_VK(expr, success_message, fail_message, ...)             \
        ASSERT((static_cast<vk::Result>(expr) == vk::Result::eSuccess), success_message, fail_message __VA_OPT__(,) __VA_ARGS__)

	// #define ASSERT_VK(expr, success_message, fail_message, ...)		ASSERT((expr == VK_SUCCESS), success_message, fail_message __VA_OPT__(,) __VA_ARGS__)
	#define ASSERT_VK_S(expr)										ASSERT((expr == VK_SUCCESS), "", #expr)

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    vk::SurfaceKHR create_surface(vk::Instance instance);


    VkBool32 vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type,
        const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data);


    const char* error_string(const vk::Result result);


	void transition_image(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout current_layout, vk::ImageLayout new_layout);

    
	//@brief Vulkan has 2 main ways of copying one image to another. you can use VkCmdCopyImage or VkCmdBlitImage.
	// CopyImage is faster, but its much more restricted, for example the resolution on both images must match.
	// Meanwhile, blit image lets you copy images of different formats and different sizes into one another.
	// You have a source rectangle and a target rectangle, and the system copies it into its position.
	// Those two functions are useful when setting up the engine, but later its best to ignore them and write your own version that can do extra logic on a fullscreen fragment shader.
	void copy_image_to_image(vk::CommandBuffer cmd, vk::Image source, vk::Image destination, vk::Extent2D srcSize, 
        vk::Extent2D dstSize);


    void create_imgui_resources(vk::DescriptorPool& imgui_descriptor_pool, vk::Device& device, 
        vr::swapchain_resources& swapchain, vk::RenderPass& imgui_render_pass, vr::instance_wrapper& instance,
        vk::PhysicalDevice& physical_device, vr::command_queues& queues, 
        std::vector<vk::Framebuffer>& imgui_framebuffers, bool& imgui_initialized);


    void destroy_imgui_resources(vk::Device& device, std::vector<vk::Framebuffer>& imgui_framebuffers, 
        vk::RenderPass& imgui_render_pass, vk::DescriptorPool& imgui_descriptor_pool, bool& imgui_initialized);


    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

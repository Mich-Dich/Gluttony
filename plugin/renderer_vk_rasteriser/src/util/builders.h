
#pragma once

#include <vulkan/vulkan.hpp>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct instance_wrapper {

        vk::Instance                                    instance_handle;
        vk::DebugUtilsMessengerEXT                      debug_messenger;

        static void destroy_instance(vk::Instance instance, vk::DebugUtilsMessengerEXT messenger = nullptr);

        static void destroy_instance(instance_wrapper instance);
    };


    struct swapchain_resources {

        vk::SwapchainKHR                                swapchain_handle = nullptr;
        std::vector<vk::Image>                          swapchain_images;
        std::vector<vk::ImageView>                      swapchain_image_views;
        vk::Format                                      swapchain_format;
        vk::Extent2D                                    swapchain_extent;
    };


    struct command_queues {

        vk::Queue                                       graphics_queue = nullptr;
        vk::Queue                                       compute_queue = nullptr;
        vk::Queue                                       transfer_queue = nullptr;
        vk::Queue                                       present_queue = nullptr;

        uint32_t                                        graphics_index = ~0U;
        uint32_t                                        compute_index = ~0U;
        uint32_t                                        transfer_index = ~0U;
        uint32_t                                        present_index = ~0U;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    std::vector<const char *> get_required_extensions_for_vk_ray();

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // Convenience wrapper for building Vulkan devices quickly wihtout own setup
    // All the Vulkan structures that are built MUST use the same vulkan_builder, because it keeps track of the internal
    // state Client has to destroy the created objects
    struct vulkan_builder {

        // Fully transparent class

        vulkan_builder();
        ~vulkan_builder();

        [[nodiscard]] instance_wrapper create_instance();

        [[nodiscard]] vk::PhysicalDevice pick_physical_device(vk::SurfaceKHR surface);

        [[nodiscard]] vk::Device create_device();

        // Call after CreateDevice() to get all the needed queues the device
        [[nodiscard]] command_queues get_queues();

        bool                                            enable_debug = false;                    // Enables validation layers
        std::vector<vk::ValidationFeatureEnableEXT>     validation_features;                     // Enables raytracing extensions
        bool                                            dedicated_compute = false;               // Device creation will fail if the device does not support the needed dedicated queues
        bool                                            dedicated_transfer = false;
        VkPhysicalDeviceFeatures                        physical_device_features10 = {};
        VkPhysicalDeviceVulkan11Features                physical_device_features11 = {};
        VkPhysicalDeviceVulkan12Features                physical_device_features12 = {};
        VkPhysicalDeviceVulkan13Features                physical_device_features13 = {};

        std::vector<const char*>                        instance_extensions;                     // Debug layers are added automatically if EnableDebug is true
        std::vector<const char*>                        instance_layers;
        std::vector<const char*>                        device_extensions;                       // Raytracing extensions are added automatically, no need to add them
        PFN_vkDebugUtilsMessengerCallbackEXT            debug_callback = nullptr;                // set nullptr to use default callback
        void*                                           debug_callback_user_data = nullptr;      // The pointer to the debug callback user data

    private:

        std::shared_ptr<void>                           struct_data = nullptr;

    };


    // utility to create swapchain, all resources that are created must be destroyed by the client
    struct swapchain_builder {

        swapchain_builder() = default;
        swapchain_builder(vk::Device device, vk::PhysicalDevice phys_device, vk::SurfaceKHR surface, uint32_t gfx_queueidx, uint32_t present_queueidx);

        // Structures need to be filled before building
        vk::Device                                      device = nullptr;
        vk::PhysicalDevice                              physical_device = nullptr;
        vk::SurfaceKHR                                  surface = nullptr;
        uint32_t                                        graphics_queue_index = UINT32_MAX;
        uint32_t                                        present_queue_index = UINT32_MAX;
        uint32_t                                        height = 1, width = 1;
        uint32_t                                        back_buffer_count = 2;
        vk::ImageUsageFlags                             image_usage = vk::ImageUsageFlagBits::eColorAttachment;
        vk::Format                                      desired_format = vk::Format::eB8G8R8A8Srgb;
        vk::ColorSpaceKHR                               color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
        vk::PresentModeKHR                              present_mode = vk::PresentModeKHR::eMailbox;

        // Creates a swapchain from the settings
        // if oldswapchain is passed it uses that swapchain as an oldswapchain to recreate the swapchain.
        // not using oldswapchain works fine too
        // The previous swapchain will not be destroyed, the client has to destroy it via DestroySwapchain()
        [[nodiscard]] swapchain_resources build_swapchain(vk::SwapchainKHR old_swapchain = nullptr);


        // This destroys the swapchain supplies
        static void destroy_swapchain(vk::Device device, const swapchain_resources &res);


        // This destroys the swapchain resources, but not the swapchain itself, useful for recreating the swapchain,
        // because the oldswapchain is needed The client has to destroy the swapchain handle
        static void destroy_swapchain_resources(vk::Device device, const swapchain_resources &res);

    private:

        std::shared_ptr<void>                           struct_data = nullptr;                  // pointer to vkb::Swapchain

    };

}

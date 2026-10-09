
#include "util/pch.h"

#include <VkBootstrap.h>

#include "builders.h"



static VKAPI_ATTR VkBool32 VKAPI_CALL vk_ray_vulkan_debug_cback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type, const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* p_user_data);

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    static std::vector<const char *> ray_tracing_extensions = {
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, // required by accel struct extension
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,     // Required by vk_ray if using Descriptors that vk_ray creates
        VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME, // for independent sets
        VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME // for ray tracing position fet
    };

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct builder_vkb_structs {

        vkb::Instance               instance;
        vkb::PhysicalDevice         physical_device;
        vkb::Device                 device;
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void instance_wrapper::destroy_instance(vk::Instance instance, vk::DebugUtilsMessengerEXT messenger) {

        if (instance) {
            if (messenger) {

                auto vkDestroyDebugUtilsMessengerEXT_ = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
                vkDestroyDebugUtilsMessengerEXT_(instance, messenger, nullptr);
            }
            instance.destroy();
            return;
        }
        LOG(warn, "Called Instance::DestroyInstance with invalid vk::Instance");
    }


    void instance_wrapper::destroy_instance(instance_wrapper instance) { instance_wrapper::destroy_instance(instance.instance_handle, instance.debug_messenger); }


    std::vector<const char *> get_required_extensions_for_vk_ray() { return ray_tracing_extensions; }

    // CLASS IMPLEMENTATION ============================================================================================

    vulkan_builder::vulkan_builder() {

        struct_data = std::make_shared<builder_vkb_structs>();
    }


    vulkan_builder::~vulkan_builder() {}

    // CLASS PUBLIC ====================================================================================================

    instance_wrapper vulkan_builder::create_instance() {

        auto inst_builder = vkb::InstanceBuilder().require_api_version(1, 3, 0);

        for (auto v : validation_features)
            inst_builder.add_validation_feature_enable(static_cast<VkValidationFeatureEnableEXT>(v));

        for (auto &ext : instance_extensions)
            inst_builder.enable_extension(ext);

        for (auto &layer : instance_layers)
            inst_builder.enable_layer(layer);

        // required extensions by vk_ray
        inst_builder.enable_extension(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        if (enable_debug)
        {
            inst_builder.request_validation_layers()
                .set_debug_callback_user_data_pointer(debug_callback_user_data)
                .set_debug_callback(debug_callback == nullptr ? vk_ray_vulkan_debug_cback : debug_callback)
                .set_debug_messenger_severity(
                    (VkDebugUtilsMessageSeverityFlagsEXT)(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
                                                          vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                                                          vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
                                                          vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose))
                .add_debug_messenger_type(
                    (VkDebugUtilsMessageTypeFlagsEXT)(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                                      vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                                                      vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance));
        }

        auto instance_result = inst_builder.build();
        if (!instance_result.has_value())
        {

            LOG(error, "Instance Build failed, Error: %s", instance_result.error().message());
            throw std::runtime_error("No Instance Created");
            return {};
        }

        vkb::Instance &return_structs = reinterpret_cast<builder_vkb_structs *>(struct_data.get())->instance;
        return_structs = instance_result.value();

        return {return_structs.instance, return_structs.debug_messenger};
    }


    vk::PhysicalDevice vulkan_builder::pick_physical_device(vk::SurfaceKHR surface) {

        vkb::Instance &instance = reinterpret_cast<builder_vkb_structs *>(struct_data.get())->instance;

        auto phys_selector = vkb::PhysicalDeviceSelector(instance, surface)
                                 .add_required_extensions(device_extensions)
                                 .add_required_extensions(ray_tracing_extensions)
                                 .prefer_gpu_device_type(vkb::PreferredDeviceType::discrete)
                                 .set_surface(surface)
                                 .require_present();

        // Enable needed features
        auto raytracing_features = vk::PhysicalDeviceRayTracingPipelineFeaturesKHR().setRayTracingPipeline(true);
        auto ray_query_features = vk::PhysicalDeviceRayQueryFeaturesKHR().setRayQuery(true);
        auto ray_tracing_position_fetch_features = vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR().setRayTracingPositionFetch(true);

        auto accelFeatures = vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
                                 .setAccelerationStructure(true)
                                 .setDescriptorBindingAccelerationStructureUpdateAfterBind(true);

        auto descbufferFeatures = vk::PhysicalDeviceDescriptorBufferFeaturesEXT()
                                      .setDescriptorBuffer(true)
                                      .setDescriptorBufferImageLayoutIgnored(true);

        phys_selector.add_required_extension_features(raytracing_features);
        phys_selector.add_required_extension_features(ray_query_features);
        phys_selector.add_required_extension_features(ray_tracing_position_fetch_features);
        phys_selector.add_required_extension_features(accelFeatures);
        phys_selector.add_required_extension_features(descbufferFeatures);

        physical_device_features12.bufferDeviceAddress = true;
        physical_device_features12.descriptorIndexing = true;
        physical_device_features12.descriptorBindingVariableDescriptorCount = true;
        physical_device_features12.descriptorBindingPartiallyBound = true;
        physical_device_features12.runtimeDescriptorArray = true;
        physical_device_features12.shaderSampledImageArrayNonUniformIndexing = true;
        physical_device_features12.shaderStorageBufferArrayNonUniformIndexing = true;
        physical_device_features12.shaderStorageImageArrayNonUniformIndexing = true;
        physical_device_features12.shaderUniformBufferArrayNonUniformIndexing = true;
        physical_device_features12.shaderUniformTexelBufferArrayNonUniformIndexing = true;
        physical_device_features12.shaderStorageTexelBufferArrayNonUniformIndexing = true;

        phys_selector.set_required_features(physical_device_features10);
        phys_selector.set_required_features_11(physical_device_features11);
        phys_selector.set_required_features_12(physical_device_features12);
        phys_selector.set_required_features_13(physical_device_features13);

        phys_selector.set_minimum_version(1, 2);
        auto phys_result = phys_selector.select();
        if (!phys_result.has_value())
        {
            LOG(error, "Physical Device Build failed, Error: %s", phys_result.error().message());
            throw std::runtime_error("Physical Device Build failed");
            return {};
        }

        vkb::PhysicalDevice &return_struct = reinterpret_cast<builder_vkb_structs*>(struct_data.get())->physical_device;
        return_struct = phys_result.value();

        return return_struct.physical_device;
    }


    vk::Device vulkan_builder::create_device() {

        vkb::PhysicalDevice &phys_dev = reinterpret_cast<builder_vkb_structs*>(struct_data.get())->physical_device;
        auto dev_builder = vkb::DeviceBuilder(phys_dev);
        auto devResult = dev_builder.build();

        if (!devResult.has_value())
        {

            LOG(error, "Logical Device Build failed, Error: %s", devResult.error().message());
            throw std::runtime_error("No Logical Devices Created");
            return {};
        }
        vkb::Device &dev = reinterpret_cast<builder_vkb_structs *>(struct_data.get())->device;
        dev = devResult.value();
        return dev.device;
    }


    command_queues vulkan_builder::get_queues() {

        vkb::Device &device = reinterpret_cast<builder_vkb_structs *>(struct_data.get())->device;
        auto vk_device = static_cast<vk::Device>(device.device);
        command_queues queues = {};

        auto getQueue = [&](uint32_t index) -> vk::Queue{ return vk_device.getQueue(index, 0); };

        bool ded_compute = false;
        bool ded_transfer = false;

        for (uint32_t x = 0; x < device.queue_families.size(); x++) {

            auto &queue = device.queue_families[x];
            auto currentQueue = getQueue(x);

            if (static_cast<vk::QueueFlags>(queue.queueFlags) & vk::QueueFlagBits::eGraphics) {

                queues.graphics_queue = currentQueue;
                queues.graphics_index = x;

                queues.present_queue = currentQueue;
                queues.present_index = x;
            }

            if (static_cast<vk::QueueFlags>(queue.queueFlags) & vk::QueueFlagBits::eCompute) {

                if (ded_compute)
                    continue; // If we already have a dedicated compute queue, skip

                if (queues.graphics_index == x)
                    ded_compute = false;
                else
                    ded_compute = true;

                queues.compute_queue = currentQueue;
                queues.compute_index = x;
            }

            if (static_cast<vk::QueueFlags>(queue.queueFlags) & vk::QueueFlagBits::eTransfer) {

                if (ded_transfer)
                    continue;

                if (queues.graphics_index == x || queues.compute_index == x)
                    ded_transfer = false;
                else
                    ded_transfer = true;

                queues.transfer_queue = currentQueue;
                queues.transfer_index = x;
            }
        }

        if (!queues.graphics_queue) {

            LOG(error, "No Graphics Queue Found");
            throw std::runtime_error("No Graphics Queue Found");
        }

        if (dedicated_compute && !ded_compute) {

            LOG(error, "No Compute Queue Found");
            throw std::runtime_error("No Compute Queue Found");
        }

        if (dedicated_transfer && !ded_transfer) {

            LOG(error, "No Transfer Queue Found");
            throw std::runtime_error("No Transfer Queue Found");
        }

        return queues;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    swapchain_builder::swapchain_builder(vk::Device device, vk::PhysicalDevice physical_device, vk::SurfaceKHR surface, uint32_t gfx_queueidx, uint32_t present_queueidx)
        : device(device), physical_device(physical_device), surface(surface), graphics_queue_index(gfx_queueidx), present_queue_index(present_queueidx) {}

    // CLASS PUBLIC ====================================================================================================

    swapchain_resources swapchain_builder::build_swapchain(vk::SwapchainKHR old_swapchain) {

        assert(height != 0);
        assert(width != 0);
        assert(device);
        assert(physical_device);
        assert(graphics_queue_index != UINT32_MAX);
        assert(present_queue_index != UINT32_MAX);
        assert(physical_device);
        assert(surface);

        std::vector<vk::SurfaceFormatKHR> surface_formats = physical_device.getSurfaceFormatsKHR(surface);
        vk::SurfaceFormatKHR compatible_format = {};
        bool found_surface_format = false;
        // See if a compatible format is found, we don't guarantee the colorspace
        // will be the same, but we try to find the desired format
        for (auto format : surface_formats) {

            // if it fulfills our criteria
            if (format.format == desired_format && format.colorSpace == color_space) {

                compatible_format = format;
                found_surface_format = true;
                break;
            }

            if (format.format == desired_format) {

                compatible_format = format;
                found_surface_format = true;
            }
        }

        if (compatible_format.format != desired_format)
            LOG(error, "Desired Format is not available, Fallback is vk::Format::eB8G8R8A8Srgb");

        if (compatible_format.colorSpace != color_space)
            LOG(error, "Desired ColorSpace is not available, Using: %s", vk::to_string(compatible_format.colorSpace));

        vkb::SwapchainBuilder *swap_builder = nullptr;
        swap_builder = new vkb::SwapchainBuilder(physical_device, device, surface, graphics_queue_index, present_queue_index);

        if (old_swapchain)
            swap_builder->set_old_swapchain(old_swapchain);

        if (found_surface_format)
            swap_builder->set_desired_format(compatible_format);

        else
            swap_builder->use_default_format_selection();

        swap_builder->set_desired_extent(width, height)
            .set_desired_present_mode((VkPresentModeKHR)present_mode)
            .add_fallback_present_mode((VkPresentModeKHR)vk::PresentModeKHR::eMailbox)
            .set_desired_min_image_count(back_buffer_count) /*copy raytracing texture*/
            .set_image_usage_flags((VkImageUsageFlags)(image_usage | vk::ImageUsageFlagBits::eColorAttachment));

        auto build_result = swap_builder->build();
        if (!build_result.has_value()) {

            LOG(error, "Swapchain Build failed, Error: %s", build_result.error().message());
            throw std::runtime_error("Swapchain Build failed");
            return {};
        }

        auto swapchain = build_result.value();
        auto swap_image_views = swapchain.get_image_views();
        auto swap_images = swapchain.get_images();

        if (!swap_images.has_value())
            LOG(error, "Swapchain Images Error: %s", swap_images.error().message());

        if (!swap_image_views.has_value())
            LOG(error, "Swapchain Image Views Error: %s", swap_image_views.error().message());

        auto images = *reinterpret_cast<std::vector<vk::Image> *>(&swap_images.value());
        auto imageviews = *reinterpret_cast<std::vector<vk::ImageView> *>(&swap_image_views.value());
        return {swapchain.swapchain, images, imageviews, (vk::Format)swapchain.image_format, (vk::Extent2D)swapchain.extent};
    }


    void swapchain_builder::destroy_swapchain(vk::Device device, const swapchain_resources& resource) {

        device.destroySwapchainKHR(resource.swapchain_handle);
        for (auto image_view : resource.swapchain_image_views)
            device.destroyImageView(image_view);
    }


    void swapchain_builder::destroy_swapchain_resources(vk::Device device, const swapchain_resources& resource) {

        for (auto image_view : resource.swapchain_image_views)
            device.destroyImageView(image_view);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}


static VKAPI_ATTR VkBool32 VKAPI_CALL vk_ray_vulkan_debug_cback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data, void* /*p_user_data*/) {

    std::string msg_head = "[Vulkan][";
    const char* msg_severity = vkb::to_string_message_severity(message_severity);
    const char* msg_type = vkb::to_string_message_type(message_type);
    switch (message_severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:       LOG(trace, "[Vulkan][%s][%s]: %s", msg_type, msg_severity, p_callback_data->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:          LOG(info, "[Vulkan][%s][%s]: %s", msg_type, msg_severity, p_callback_data->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:       LOG(warn, "[Vulkan][%s][%s]: %s", msg_type, msg_severity, p_callback_data->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:         LOG(error, "[Vulkan][%s][%s]: %s", msg_type, msg_severity, p_callback_data->pMessage); break;
        default: break;
    }

    return VK_FALSE;
}

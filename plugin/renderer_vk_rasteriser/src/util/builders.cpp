
#include "util/pch.h"

#include <cctype>
#include <cstdlib>

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


    // Higher wins. A software rasteriser is only ever the last resort: it implements nearly every extension and feature,
    // so "the first device that passes the requirement checks" silently picked llvmpipe over the real GPU as soon as one
    // requirement was stricter than the GPU's driver (2026-11: ~15 fps on a laptop with a perfectly usable iGPU)
    static int device_type_rank(VkPhysicalDeviceType type) {

        switch (type) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:      return 4;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:    return 3;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:       return 2;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:               return 0;
            default:                                        return 1;
        }
    }


    static std::string to_lower(std::string text) {

        std::transform(text.begin(), text.end(), text.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    }


    // Enables ONE feature bit of a Vulkan 1.1 / 1.2 / 1.3 / extension feature struct if the device supports it. One bit
    // per call: vk-bootstrap enables a struct all-or-nothing, so a single missing feature must not block the others
    template<typename features_t>
    static bool enable_feature_if_present(vkb::PhysicalDevice& device, VkStructureType type, VkBool32 features_t::* member) {

        features_t features{};
        features.sType = type;
        features.*member = VK_TRUE;
        return device.enable_extension_features_if_present(features);
    }

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
            // request_validation_layers() silently carries on without the layer when it is not installed. Say so: a debug
            // build that validates nothing looks exactly like one that has no API misuse
            const auto system_info = vkb::SystemInfo::get_system_info();
            if (system_info.has_value() && !system_info.value().validation_layers_available)
                LOG(warn, "Vulkan validation was requested, but VK_LAYER_KHRONOS_validation is not installed / not found: this build "
                    "validates NOTHING. Install the package 'vulkan-validationlayers' (or source the Vulkan SDK's setup-env.sh)")

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

            LOG(error, "Instance Build failed, Error: {}", instance_result.error().message());
            throw std::runtime_error("No Instance Created");
            return {};
        }

        vkb::Instance &return_structs = reinterpret_cast<builder_vkb_structs *>(struct_data.get())->instance;
        return_structs = instance_result.value();

        return {return_structs.instance, return_structs.debug_messenger};
    }


    vk::PhysicalDevice vulkan_builder::pick_physical_device(vk::SurfaceKHR surface) {

        vkb::Instance &instance = reinterpret_cast<builder_vkb_structs *>(struct_data.get())->instance;

        // ---- HARD requirements: only what the rasteriser cannot run without ------------------------------------------
        // Everything is bound through VK_EXT_descriptor_buffer (and therefore by buffer device address), deferred
        // destruction uses a timeline semaphore, dynamic rendering + synchronization2 are core (and mandatory) in 1.3.
        // This list used to contain the whole ray tracing extension set and descriptorBufferImageLayoutIgnored, none of
        // which the rasteriser uses: hardware GPUs that lack one of them were rejected and the software rasteriser
        // (which has them all) was picked instead
        physical_device_features12.bufferDeviceAddress = true;
        physical_device_features12.timelineSemaphore = true;            // util::retire_queue (deferred destruction)
        physical_device_features13.dynamicRendering = true;
        physical_device_features13.synchronization2 = true;

        std::vector<const char*> required_extensions = device_extensions;
        const auto require_extension = [&](const char* extension) {
            const bool listed = std::any_of(required_extensions.begin(), required_extensions.end(),
                [&](const char* other) { return std::strcmp(other, extension) == 0; });
            if (!listed)
                required_extensions.push_back(extension);
        };
        require_extension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME);
        if (require_ray_tracing)
            for (const char* extension : ray_tracing_extensions)
                require_extension(extension);

        const auto configure = [&](vkb::PhysicalDeviceSelector& selector) {

            selector.add_required_extensions(required_extensions)
                .set_surface(surface)
                .require_present()
                .allow_any_gpu_device_type(true)        // the device type is ranked below, it never rejects a device
                .set_minimum_version(1, 3);

            selector.add_required_extension_features(vk::PhysicalDeviceDescriptorBufferFeaturesEXT().setDescriptorBuffer(true));

            if (require_ray_tracing) {
                selector.add_required_extension_features(vk::PhysicalDeviceRayTracingPipelineFeaturesKHR().setRayTracingPipeline(true));
                selector.add_required_extension_features(vk::PhysicalDeviceRayQueryFeaturesKHR().setRayQuery(true));
                selector.add_required_extension_features(
                    vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR().setRayTracingPositionFetch(true));
                selector.add_required_extension_features(vk::PhysicalDeviceAccelerationStructureFeaturesKHR()
                    .setAccelerationStructure(true)
                    .setDescriptorBindingAccelerationStructureUpdateAfterBind(true));
            }

            selector.set_required_features(physical_device_features10);
            selector.set_required_features_11(physical_device_features11);
            selector.set_required_features_12(physical_device_features12);
            selector.set_required_features_13(physical_device_features13);
        };

        // ---- Check every device on its own, so the reason for a rejection can be reported ----------------------------
        struct candidate {
            vkb::PhysicalDevice                         device;
            u32                                         index = 0;      // position in the enumeration (= logged index)
            int                                         rank = 0;       // device_type_rank()
        };
        std::vector<candidate> candidates;
        std::vector<std::string> rejections;

        const std::vector<vk::PhysicalDevice> all_devices = vk::Instance(instance.instance).enumeratePhysicalDevices();
        for (u32 index = 0; index < all_devices.size(); ++index) {

            const vk::PhysicalDeviceProperties properties = all_devices[index].getProperties();
            const std::string name = properties.deviceName.data();
            const std::string label = std::format("[{}] {} ({}, Vulkan {}.{}.{})", index, name, vk::to_string(properties.deviceType),
                VK_API_VERSION_MAJOR(properties.apiVersion), VK_API_VERSION_MINOR(properties.apiVersion),
                VK_API_VERSION_PATCH(properties.apiVersion));

            vkb::PhysicalDeviceSelector selector(instance, surface);
            configure(selector);
            selector.set_name(name);                    // restrict this run to the one device
            const auto result = selector.select();

            if (result.has_value()) {

                const VkPhysicalDevice picked = result.value().physical_device;
                const bool known = std::any_of(candidates.begin(), candidates.end(),
                    [&](const candidate& other) { return other.device.physical_device == picked; });
                if (!known) {
                    candidates.push_back({ result.value(), index,
                        device_type_rank(static_cast<VkPhysicalDeviceType>(properties.deviceType)) });
                    LOG(info, "Vulkan device {}: usable", label)
                }
                continue;
            }

            // vk-bootstrap prefixes every reason with the device it belongs to; the other devices only fail the name filter
            std::string reasons;
            const std::string prefix = "Physical Device " + name + " not selected due to: ";
            for (const std::string& reason : result.detailed_failure_reasons()) {
                if (!reason.starts_with(prefix) || reason.find("doesn't match requested name") != std::string::npos)
                    continue;
                if (!reasons.empty())
                    reasons += "; ";
                reasons += reason.substr(prefix.size());
            }
            if (reasons.empty())
                reasons = result.error().message();

            rejections.push_back(label + ": " + reasons);
            LOG(warn, "Vulkan device {}: REJECTED - {}", label, reasons)
        }

        if (candidates.empty()) {

            for (const std::string& rejection : rejections)
                LOG(error, "Vulkan device REJECTED: {}", rejection)
            LOG(error, "No Vulkan device fulfils the renderer's requirements (Vulkan 1.3, VK_EXT_descriptor_buffer, present support)")
            throw std::runtime_error("Physical Device Build failed");
        }

        // ---- Choose: an explicit request first, otherwise the best device type (ties: the system's own order) -------
        const candidate* chosen = nullptr;

        std::string wanted = preferred_device;
        if (const char* env = std::getenv("GLT_VK_DEVICE"); env && *env)
            wanted = env;

        if (!wanted.empty()) {

            const bool is_index = std::all_of(wanted.begin(), wanted.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
            const std::string needle = to_lower(wanted);
            for (const candidate& option : candidates) {
                const bool match = is_index ? (std::to_string(option.index) == wanted)
                                            : (to_lower(option.device.name).find(needle) != std::string::npos);
                if (match) {
                    chosen = &option;
                    break;
                }
            }
            if (!chosen)
                LOG(error, "Requested Vulkan device '{}' (GLT_VK_DEVICE) matches no usable device, choosing automatically", wanted)
        }

        if (!chosen) {
            chosen = &candidates.front();
            for (const candidate& option : candidates)
                if (option.rank > chosen->rank)
                    chosen = &option;
        }

        vkb::PhysicalDevice selected = chosen->device;

        if (selected.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {

            // error level on purpose: it is the only level (besides fatal) that reaches the log in release builds
            for (const std::string& rejection : rejections)
                LOG(error, "Vulkan device REJECTED: {}", rejection)
            LOG(error, "Rendering on the SOFTWARE rasteriser '{}': no hardware GPU fulfils the renderer's requirements, so every "
                "pixel is shaded on the CPU and the frame rate will be very low. See the rejection reasons above (GPU driver / "
                "Mesa too old?), or force a device with the environment variable GLT_VK_DEVICE=<part of the device name>",
                selected.name)
        }

        // ---- Features the renderer uses when they exist: enabled on the chosen device, never a reason to reject it --
        std::string missing_used;           // used by the renderer: every desktop driver has them
        std::string missing_optional;       // enabled for completeness only
        const auto note = [](std::string& list, bool enabled, const char* feature) {
            if (enabled)
                return;
            if (!list.empty())
                list += ", ";
            list += feature;
        };

        const auto enable_10 = [&](VkBool32 VkPhysicalDeviceFeatures::* member) {
            VkPhysicalDeviceFeatures features{};
            features.*member = VK_TRUE;
            return selected.enable_features_if_present(features);
        };
        const auto enable_11 = [&](VkBool32 VkPhysicalDeviceVulkan11Features::* member) {
            return enable_feature_if_present<VkPhysicalDeviceVulkan11Features>(selected,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, member);
        };
        const auto enable_12 = [&](VkBool32 VkPhysicalDeviceVulkan12Features::* member) {
            return enable_feature_if_present<VkPhysicalDeviceVulkan12Features>(selected,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, member);
        };
        const auto enable_13 = [&](VkBool32 VkPhysicalDeviceVulkan13Features::* member) {
            return enable_feature_if_present<VkPhysicalDeviceVulkan13Features>(selected,
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, member);
        };

        // Used by the draw path. They were never enabled before (drivers tolerate that, the spec does not)
        note(missing_used, enable_10(&VkPhysicalDeviceFeatures::multiDrawIndirect), "multiDrawIndirect");                  // one indirect call per material
        note(missing_used, enable_10(&VkPhysicalDeviceFeatures::drawIndirectFirstInstance), "drawIndirectFirstInstance");  // firstInstance = instance index
        note(missing_used, enable_12(&VkPhysicalDeviceVulkan12Features::drawIndirectCount), "drawIndirectCount");          // drawIndexedIndirectCount
        note(missing_used, enable_11(&VkPhysicalDeviceVulkan11Features::shaderDrawParameters), "shaderDrawParameters");    // SV_StartInstanceLocation

        note(missing_optional, enable_10(&VkPhysicalDeviceFeatures::textureCompressionBC), "textureCompressionBC");        // BC1-7 texture assets
        note(missing_optional, enable_13(&VkPhysicalDeviceVulkan13Features::shaderDemoteToHelperInvocation), "shaderDemoteToHelperInvocation");
        note(missing_optional, enable_13(&VkPhysicalDeviceVulkan13Features::maintenance4), "maintenance4");

        // Enabled since the first version of this builder; the rasteriser has no descriptor arrays, so they are optional
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::descriptorIndexing), "descriptorIndexing");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::descriptorBindingVariableDescriptorCount), "descriptorBindingVariableDescriptorCount");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::descriptorBindingPartiallyBound), "descriptorBindingPartiallyBound");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::runtimeDescriptorArray), "runtimeDescriptorArray");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::shaderSampledImageArrayNonUniformIndexing), "shaderSampledImageArrayNonUniformIndexing");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::shaderStorageBufferArrayNonUniformIndexing), "shaderStorageBufferArrayNonUniformIndexing");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::shaderStorageImageArrayNonUniformIndexing), "shaderStorageImageArrayNonUniformIndexing");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::shaderUniformBufferArrayNonUniformIndexing), "shaderUniformBufferArrayNonUniformIndexing");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::shaderUniformTexelBufferArrayNonUniformIndexing), "shaderUniformTexelBufferArrayNonUniformIndexing");
        note(missing_optional, enable_12(&VkPhysicalDeviceVulkan12Features::shaderStorageTexelBufferArrayNonUniformIndexing), "shaderStorageTexelBufferArrayNonUniformIndexing");

        // Optional capability of the extension, and not needed here: every image descriptor is written with the layout the
        // render graph puts the image in. It used to be a hard requirement, which disqualified every GPU without it
        note(missing_optional, enable_feature_if_present<VkPhysicalDeviceDescriptorBufferFeaturesEXT>(selected,
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
            &VkPhysicalDeviceDescriptorBufferFeaturesEXT::descriptorBufferImageLayoutIgnored), "descriptorBufferImageLayoutIgnored");

        if (!missing_used.empty())
            LOG(error, "Vulkan device '{}' does not report features the renderer uses: {}", selected.name, missing_used)
        if (!missing_optional.empty())
            LOG(info, "Vulkan device '{}': optional features not available: {}", selected.name, missing_optional)

        LOG(info, "Using Vulkan device [{}] {} ({})", chosen->index, selected.name,
            vk::to_string(static_cast<vk::PhysicalDeviceType>(selected.properties.deviceType)))

        vkb::PhysicalDevice &return_struct = reinterpret_cast<builder_vkb_structs*>(struct_data.get())->physical_device;
        return_struct = selected;

        return return_struct.physical_device;
    }


    vk::Device vulkan_builder::create_device() {

        vkb::PhysicalDevice &phys_dev = reinterpret_cast<builder_vkb_structs*>(struct_data.get())->physical_device;
        auto dev_builder = vkb::DeviceBuilder(phys_dev);
        auto devResult = dev_builder.build();

        if (!devResult.has_value())
        {

            LOG(error, "Logical Device Build failed, Error: {}", devResult.error().message());
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
            LOG(error, "Desired ColorSpace is not available, Using: {}", vk::to_string(compatible_format.colorSpace));

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

            LOG(error, "Swapchain Build failed, Error: {}", build_result.error().message());
            throw std::runtime_error("Swapchain Build failed");
            return {};
        }

        auto swapchain = build_result.value();
        auto swap_image_views = swapchain.get_image_views();
        auto swap_images = swapchain.get_images();

        if (!swap_images.has_value())
            LOG(error, "Swapchain Images Error: {}", swap_images.error().message());

        if (!swap_image_views.has_value())
            LOG(error, "Swapchain Image Views Error: {}", swap_image_views.error().message());

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
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:       LOG(trace, "[Vulkan][{}][{}]: {}", msg_type, msg_severity, p_callback_data->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:          LOG(info, "[Vulkan][{}][{}]: {}", msg_type, msg_severity, p_callback_data->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:       LOG(warn, "[Vulkan][{}][{}]: {}", msg_type, msg_severity, p_callback_data->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:         LOG(error, "[Vulkan][{}][{}]: {}", msg_type, msg_severity, p_callback_data->pMessage); break;
        default: break;
    }

    return VK_FALSE;
}

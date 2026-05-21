
#include "util/pch.h"
#include "data_structures.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray::utils {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

	void deletion_queue::flush_pointer(std::pair<std::type_index, void*> pointer) {

		VALIDATE(pointer.second, return, "", "TODO: add error message")

		#define IS_OF_TYPE(name)        pointer.first == std::type_index(typeid(name))
		#define USE_AS(name)            static_cast<name>(pointer.second)
		#define VK_DESTROY_FUNC(name)   (IS_OF_TYPE(Vk##name)) { vkDestroy##name(m_dq_device, USE_AS(Vk##name), nullptr); }

		if VK_DESTROY_FUNC(Sampler)
		else if VK_DESTROY_FUNC(CommandPool)
		else if VK_DESTROY_FUNC(DescriptorPool)
		else if VK_DESTROY_FUNC(Fence)
		else if VK_DESTROY_FUNC(DescriptorSetLayout)
		else if VK_DESTROY_FUNC(Pipeline)
		else if VK_DESTROY_FUNC(PipelineLayout)
		else if VK_DESTROY_FUNC(QueryPool)
		else if VK_DESTROY_FUNC(Buffer)
		else if VK_DESTROY_FUNC(Image)
		else if VK_DESTROY_FUNC(ImageView)
		else if VK_DESTROY_FUNC(Semaphore)
		else if VK_DESTROY_FUNC(RenderPass)
		else if VK_DESTROY_FUNC(Framebuffer)
		else if VK_DESTROY_FUNC(PipelineCache)
    	// else if VK_DESTROY_FUNC(DeviceMemory)
    	else if VK_DESTROY_FUNC(BufferView)
    	else if VK_DESTROY_FUNC(ShaderModule)
		else if (IS_OF_TYPE(VkCommandBuffer*)) {
			// This is tricky - we need the command pool
			// Better approach: Don't push individual command buffers to the queue
			LOG(warn, "VkCommandBuffer should not be pushed individually. Use command pool destruction instead.");
		}
		else if (IS_OF_TYPE(VkDeviceMemory)) {
			vkFreeMemory(m_dq_device, USE_AS(VkDeviceMemory), nullptr);
		}
		else if (IS_OF_TYPE(VkSurfaceKHR)) {
			// Note: surface destruction requires the instance
			// This should be handled differently or we need to store the instance
			// For now, skip - surfaces are destroyed in swap_chain cleanup
			LOG(warn, "VkSurfaceKHR should be destroyed with vkDestroySurfaceKHR using instance, not device");
		}
		else {
			LOG(error, "Renderer deletion queue used with an unsupported type [{}]", pointer.first.name());
		}

		#undef IS_OF_TYPE
		#undef USE_AS
		#undef VK_DESTROY_FUNC
	}


    void deletion_queue::setup(VkDevice device) {

		m_dq_device = device;
        LOG_INIT
	}


	void deletion_queue::shutdown() {

		m_dq_device = nullptr;
		flush();
		LOG_SHUTDOWN
	}

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

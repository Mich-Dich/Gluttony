
#include "util/pch.h"

IGNORE_UNUSED_VARIABLE_START
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
IGNORE_UNUSED_VARIABLE_STOP

#include "util/device.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    device::device(vk::Instance inst, vk::Device dev, vk::PhysicalDevice physDev, VmaAllocator allocator)
        : m_instance(inst), m_device(dev), m_physical_device(physDev), m_vma_allocator(allocator) {

        m_dyn_loader.init(inst, vkGetInstanceProcAddr, dev, vkGetDeviceProcAddr);

        // Only the descriptor buffer properties are needed for rasterization
        auto deviceProperties = vk::PhysicalDeviceProperties2KHR();
        deviceProperties.pNext = &m_descriptor_buffer_properties;
        m_descriptor_buffer_properties.pNext = nullptr;
        m_physical_device.getProperties2KHR(&deviceProperties, m_dyn_loader);
        m_device_properties = m_physical_device.getProperties();

        if (m_vma_allocator != nullptr) {
            m_user_supplied_allocator = true;
            return;
        }

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = physDev;
        allocatorInfo.device = dev;
        allocatorInfo.instance = inst;
        allocatorInfo.flags = VmaAllocatorCreateFlagBits::VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

        vmaCreateAllocator(&allocatorInfo, &m_vma_allocator);
        m_user_supplied_allocator = false;
    }


    device::~device() {

        if (!m_user_supplied_allocator)
            vmaDestroyAllocator(m_vma_allocator);
    }

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

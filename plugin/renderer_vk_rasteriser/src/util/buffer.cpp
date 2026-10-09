
#include "util/pch.h"

#include "util/buffer.h"
#include "util/device.h"

// FORWARD DECLARATIONS ================================================================================================


namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    u32 align_up(u32 value, u32 alignment)            { return (value + alignment - 1) & ~(alignment - 1); }


    u64 align_up(u64 value, u64 alignment)            { return (value + alignment - 1) & ~(alignment - 1); }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    allocated_image device::create_image(const vk::ImageCreateInfo& image_info, VmaAllocationCreateFlags flags, VmaPool pool) {

        allocated_image out_image = {};
        VmaAllocationCreateInfo alloc_inf = {};
        alloc_inf.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        alloc_inf.flags = flags;
        alloc_inf.pool = pool == nullptr ? m_current_pool : pool;

        VmaAllocationInfo allocationInfo = {};

        auto result = (vk::Result)vmaCreateImage(m_vma_allocator, (VkImageCreateInfo*)&image_info, &alloc_inf, (VkImage*)&out_image.image, &out_image.allocation, &allocationInfo);
        if (result != vk::Result::eSuccess)
            LOG(error, "Failed to create Image: %s", vk::to_string(result));

        out_image.size = allocationInfo.size;
        out_image.width = image_info.extent.width;
        out_image.height = image_info.extent.height;
        return out_image;
    }


    allocated_buffer device::create_buffer(vk::DeviceSize size, vk::BufferUsageFlags buffer_usage, VmaAllocationCreateFlags flags, u32 alignment, VmaPool pool) {

        allocated_buffer out_buffer = {};
        VmaAllocationCreateInfo alloc_inf = {};
        alloc_inf.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        alloc_inf.flags = flags;
        alloc_inf.pool = pool == nullptr ? m_current_pool : pool;

        vk::BufferCreateInfo buffer_info = {};
        buffer_info.setSize(size);
        buffer_info.setUsage(buffer_usage);

        vk::Result result;
        if (alignment)
            result = (vk::Result)vmaCreateBufferWithAlignment(m_vma_allocator, (VkBufferCreateInfo*)& buffer_info, &alloc_inf, // type punning
                alignment, (VkBuffer*)&out_buffer.buffer, &out_buffer.allocation, nullptr);
        else
            result = (vk::Result)vmaCreateBuffer(m_vma_allocator, (VkBufferCreateInfo*)& buffer_info, &alloc_inf, (VkBuffer*)&out_buffer.buffer, &out_buffer.allocation, nullptr);

        if (result != vk::Result::eSuccess)
        {
            LOG(error, "Failed to create buffer: %s", vk::to_string(result));
            return out_buffer;
        }

        out_buffer.dev_address = m_device.getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(out_buffer.buffer));
        out_buffer.size = size;
        return out_buffer;
    }


    descriptor_buffer device::create_descriptor_buffer(vk::DescriptorSetLayout layout, std::vector<descriptor_item> &items, descriptor_buffer_type type, u32 set_count) {

        descriptor_buffer out_buffer = {};
        vk::BufferUsageFlags usage_flags = (vk::BufferUsageFlagBits)type; // descriptor_buffer_type is a vulkan buffer usage flag enum
        vk::DeviceSize size = m_device.getDescriptorSetLayoutSizeEXT(layout, m_dyn_loader);
        size = align_up(size, m_descriptor_buffer_properties.descriptorBufferOffsetAlignment);

        // create a buffer that is big enough to hold all the descriptor sets and with the proper alignment
        out_buffer.buffer = create_buffer(size * set_count, usage_flags, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            m_descriptor_buffer_properties.descriptorBufferOffsetAlignment);

        // fill the offsets to the items
        for (auto &item : items)
        {
            u32 offset = m_device.getDescriptorSetLayoutBindingOffsetEXT(layout, item.binding, m_dyn_loader);
            item.binding_offset = offset;
        }

        out_buffer.set_count = set_count;
        out_buffer.single_descriptor_size = size;
        out_buffer.type = type;

        return out_buffer;
    }


    void device::destroy_buffer(allocated_buffer &buffer) {

        vmaDestroyBuffer(m_vma_allocator, buffer.buffer, buffer.allocation);
        buffer.buffer = nullptr;
        buffer.allocation = nullptr;
        buffer.dev_address = 0;
    }


    void device::destroy_image(allocated_image &image) {

        vmaDestroyImage(m_vma_allocator, image.image, image.allocation);
        image.image = nullptr;
        image.allocation = nullptr;
    }


    void device::update_buffer(allocated_buffer allocated_buffer, void *data, const vk::DeviceSize size, u32 offset) {

        void *mappedData;
        vmaMapMemory(m_vma_allocator, allocated_buffer.allocation, &mappedData);
        memcpy((uint8_t *)mappedData + offset, data, size);
        vmaUnmapMemory(m_vma_allocator, allocated_buffer.allocation);
    }


    void device::copy_data(allocated_buffer src, allocated_buffer dst, vk::DeviceSize size, vk::CommandBuffer cmdBuf) {

        auto copyRegion = vk::BufferCopy().setSize(size);
        cmdBuf.copyBuffer(src.buffer, dst.buffer, copyRegion);
    }


    void *device::map_buffer(allocated_buffer &buffer) {

        void *mappedData;
        vmaMapMemory(m_vma_allocator, buffer.allocation, &mappedData);
        return mappedData;
    }


    void device::unmap_buffer(allocated_buffer &buffer)         { vmaUnmapMemory(m_vma_allocator, buffer.allocation); }


    void device::transition_image_layout(vk::CommandBuffer cmdBuf, vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout,
        const vk::ImageSubresourceRange& range, vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage) {

        // transition image layout with ..set functions
        auto barrier = vk::ImageMemoryBarrier()
            .setOldLayout(old_layout)
            .setNewLayout(new_layout)
            .setImage(image)
            .setSubresourceRange(range);

        // set src and dst access masks
        switch (old_layout) {
            case vk::ImageLayout::eUndefined:                       barrier.setSrcAccessMask((vk::AccessFlagBits)0); break;
            case vk::ImageLayout::ePreinitialized:                  barrier.setSrcAccessMask(vk::AccessFlagBits::eHostWrite); break;
            case vk::ImageLayout::eTransferDstOptimal:              barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite); break;
            case vk::ImageLayout::eTransferSrcOptimal:              barrier.setSrcAccessMask(vk::AccessFlagBits::eTransferRead); break;
            case vk::ImageLayout::eColorAttachmentOptimal:          barrier.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite); break;
            case vk::ImageLayout::eDepthStencilAttachmentOptimal:   barrier.setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite); break;
            case vk::ImageLayout::eShaderReadOnlyOptimal:           barrier.setSrcAccessMask(vk::AccessFlagBits::eShaderRead); break;
            default: break;
        }
        // set dst access masks
        switch (new_layout) {
            case vk::ImageLayout::eTransferDstOptimal:              barrier.setDstAccessMask(vk::AccessFlagBits::eTransferWrite); break;
            case vk::ImageLayout::eTransferSrcOptimal:              barrier.setDstAccessMask(vk::AccessFlagBits::eTransferRead); break;
            case vk::ImageLayout::eColorAttachmentOptimal:          barrier.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite); break;
            case vk::ImageLayout::eDepthStencilAttachmentOptimal:   barrier.setDstAccessMask(barrier.dstAccessMask | vk::AccessFlagBits::eDepthStencilAttachmentWrite); break;
            case vk::ImageLayout::eShaderReadOnlyOptimal:
                if (barrier.srcAccessMask == (vk::AccessFlagBits)0)
                    barrier.setSrcAccessMask(vk::AccessFlagBits::eHostWrite | vk::AccessFlagBits::eTransferWrite);
                barrier.setDstAccessMask(vk::AccessFlagBits::eShaderRead);
                break;
            default: break;
        }
        cmdBuf.pipelineBarrier(srcStage, dstStage, (vk::DependencyFlagBits)0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

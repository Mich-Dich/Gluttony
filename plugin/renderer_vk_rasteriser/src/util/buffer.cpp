
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

        if (image_info.extent.width == 0 || image_info.extent.height == 0 || image_info.extent.depth == 0) {
            LOG(error, "rejecting empty image ({}x{}x{}, fmt {})",
                image_info.extent.width, image_info.extent.height, image_info.extent.depth, vk::to_string(image_info.format));
            return out_image;
        }

        VmaAllocationCreateInfo alloc_inf = {};
        alloc_inf.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        alloc_inf.flags = flags;
        alloc_inf.pool = pool == nullptr ? m_current_pool : pool;

        VmaAllocationInfo allocationInfo = {};

        const auto result = static_cast<vk::Result>(vmaCreateImage(m_vma_allocator, (VkImageCreateInfo*)&image_info, &alloc_inf,
            (VkImage*)&out_image.image, &out_image.allocation, &allocationInfo));

        if (result != vk::Result::eSuccess || out_image.image == nullptr || out_image.allocation == nullptr) {

            LOG(error, "Failed: {}x{}, fmt {}, result {}",
                image_info.extent.width, image_info.extent.height, vk::to_string(image_info.format), vk::to_string(result));
            // VMA can leave a partial handle behind on some failures; clean up defensively
            if (out_image.image || out_image.allocation)
                vmaDestroyImage(m_vma_allocator, out_image.image, out_image.allocation);
            return {};
        }

        out_image.size = allocationInfo.size;
        out_image.width = image_info.extent.width;
        out_image.height = image_info.extent.height;
        return out_image;
    }


    allocated_buffer device::create_buffer(vk::DeviceSize size, vk::BufferUsageFlags buffer_usage, VmaAllocationCreateFlags flags,
        u32 alignment, VmaPool pool) {

        allocated_buffer out_buffer = {};

        if (size == 0) {
            LOG(error, "rejecting zero-size buffer (usage {})", vk::to_string(buffer_usage));
            return out_buffer;
        }

        VmaAllocationCreateInfo alloc_inf = {};
        alloc_inf.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        alloc_inf.flags = flags;
        alloc_inf.pool = pool == nullptr ? m_current_pool : pool;

        vk::BufferCreateInfo buffer_info = {};
        buffer_info.setSize(size);
        buffer_info.setUsage(buffer_usage);

        vk::Result result;
        if (alignment)
            result = static_cast<vk::Result>(vmaCreateBufferWithAlignment(m_vma_allocator, (VkBufferCreateInfo*)&buffer_info,
                &alloc_inf, alignment, (VkBuffer*)&out_buffer.buffer, &out_buffer.allocation, nullptr));
        else
            result = static_cast<vk::Result>(vmaCreateBuffer(m_vma_allocator, (VkBufferCreateInfo*)&buffer_info, &alloc_inf,
                (VkBuffer*)&out_buffer.buffer, &out_buffer.allocation, nullptr));

        if (result != vk::Result::eSuccess || out_buffer.buffer == nullptr || out_buffer.allocation == nullptr) {

            LOG(error, "Failed: {} bytes, usage {}, result {}", size, vk::to_string(buffer_usage), vk::to_string(result));
            if (out_buffer.buffer || out_buffer.allocation)
                vmaDestroyBuffer(m_vma_allocator, out_buffer.buffer, out_buffer.allocation);
            return {};
        }

        out_buffer.dev_address = m_device.getBufferAddress(vk::BufferDeviceAddressInfo().setBuffer(out_buffer.buffer));
        out_buffer.size = size;
        return out_buffer;
    }


    descriptor_buffer device::create_descriptor_buffer(vk::DescriptorSetLayout layout, std::vector<descriptor_item>& items,
        descriptor_buffer_type type, u32 set_count) {

        descriptor_buffer out_buffer = {};

        if (!layout) {
            LOG(error, "null layout");
            return out_buffer;
        }
        if (set_count == 0) set_count = 1;

        const vk::BufferUsageFlags usage_flags = (vk::BufferUsageFlagBits)type;
        vk::DeviceSize size = m_device.getDescriptorSetLayoutSizeEXT(layout, m_dyn_loader);
        size = align_up(size, m_descriptor_buffer_properties.descriptorBufferOffsetAlignment);

        out_buffer.buffer = create_buffer(size * set_count, usage_flags, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
            m_descriptor_buffer_properties.descriptorBufferOffsetAlignment);

        if (!out_buffer.buffer.buffer || !out_buffer.buffer.allocation) {
            LOG(error, "buffer creation failed ({} bytes, {} sets)", size, set_count);
            return {};          // do NOT call compute_descriptor_binding_offsets on a broken buffer
        }

        compute_descriptor_binding_offsets(layout, items);

        out_buffer.set_count = set_count;
        out_buffer.single_descriptor_size = size;
        out_buffer.type = type;
        return out_buffer;
    }


    void device::destroy_buffer(allocated_buffer& buffer) {

        if (!buffer.buffer && !buffer.allocation) {
            buffer = {};            // idempotent on already-cleared buffers
            return;
        }

        vmaDestroyBuffer(m_vma_allocator, buffer.buffer, buffer.allocation);
        buffer = {};
    }


    void device::destroy_image(allocated_image& image) {

        if (!image.image && !image.allocation) {
            image = {};
            return;
        }

        vmaDestroyImage(m_vma_allocator, image.image, image.allocation);
        image = {};
    }


    void device::update_buffer(allocated_buffer buffer, const void* data, const vk::DeviceSize size, u32 offset) {

        if (!buffer.allocation) {
            LOG(error, "null allocation (buffer handle {:p})", (void*)buffer.buffer);
            return;
        }
        if (!data || size == 0)
            return;
        if (buffer.size > 0 && offset + size > buffer.size) {
            LOG(error, "{} bytes at offset {} exceeds buffer size {}", size, offset, buffer.size);
            return;
        }

        void* mapped = map_buffer(buffer);
        if (!mapped)
            return;

        std::memcpy((uint8_t*)mapped + offset, data, size);
        unmap_buffer(buffer);
    }


    void* device::map_buffer(allocated_buffer& buffer) {

        if (!buffer.allocation) {
            LOG(error, "null allocation (buffer handle {:p}, size {})", (void*)buffer.buffer, (u64)buffer.size);
            return nullptr;
        }

        void* mappedData = nullptr;
        const auto result = static_cast<vk::Result>(
            vmaMapMemory(m_vma_allocator, buffer.allocation, &mappedData));

        if (result != vk::Result::eSuccess || mappedData == nullptr) {
            LOG(error, "vmaMapMemory failed ({})", vk::to_string(result));
            return nullptr;
        }
        return mappedData;
    }


    void device::unmap_buffer(allocated_buffer& buffer) {

        if (!buffer.allocation)
            return;                 // was previously a hard VMA assert; make it a no-op
        vmaUnmapMemory(m_vma_allocator, buffer.allocation);
    }


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

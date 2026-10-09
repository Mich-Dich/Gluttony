
#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "util/descriptors.h"
#include "util/shader.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class device {
    public:
        device(vk::Instance inst, vk::Device dev, vk::PhysicalDevice physDev, VmaAllocator allocator = nullptr);
        ~device();

        void set_vma_pool(VmaPool pool) { m_current_pool = pool; }

        DEFAULT_GETTER_C(vk::Device, device)
        DEFAULT_GETTER_C(vk::PhysicalDevice, physical_device)
        DEFAULT_GETTER_C(vk::Instance, instance)
        GETTER_C(vk::detail::DispatchLoaderDynamic, dynamic_loader, m_dyn_loader)
        GETTER_C(vk::PhysicalDeviceProperties, properties, m_device_properties)
        DEFAULT_GETTER_C(vk::PhysicalDeviceDescriptorBufferPropertiesEXT, descriptor_buffer_properties)

        // --- command buffer helpers ----------------------------------------------------------------
        void transition_image_layout(vk::CommandBuffer command_buffer, vk::Image image,
            vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
            const vk::ImageSubresourceRange& range = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1),
            vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eAllGraphics,
            vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eAllCommands);

        // --- allocation ----------------------------------------------------------------------------
        VmaAllocator get_allocator() const { return m_vma_allocator; }

        [[nodiscard]] allocated_image  create_image(const vk::ImageCreateInfo& imgInfo, VmaAllocationCreateFlags flags, VmaPool pool = nullptr);
        [[nodiscard]] allocated_buffer create_buffer(vk::DeviceSize size, vk::BufferUsageFlags bufferUsage,
            VmaAllocationCreateFlags flags = 0, u32 alignment = 0, VmaPool pool = nullptr);

        [[nodiscard]] descriptor_buffer create_descriptor_buffer(vk::DescriptorSetLayout layout,
            std::vector<descriptor_item>& items, descriptor_buffer_type type, u32 setCount = 1);

        void copy_data(allocated_buffer src, allocated_buffer dst, vk::DeviceSize size, vk::CommandBuffer command_buffer);
        void update_buffer(allocated_buffer alloc, void* data, const vk::DeviceSize size, u32 offset = 0);

        [[nodiscard]] void* map_buffer(allocated_buffer& buffer);
        void unmap_buffer(allocated_buffer& buffer);

        void destroy_buffer(allocated_buffer& buffer);
        void destroy_image(allocated_image& img);

        // --- pipeline ------------------------------------------------------------------------------
        [[nodiscard]] shader create_shader_from_spv(const std::vector<u32>& spv);
        [[nodiscard]] vk::ShaderModule create_shader_module(const std::vector<u32>& spvCode);
        void destroy_shader(shader& shader);

        [[nodiscard]] vk::PipelineLayout create_pipeline_layout(vk::DescriptorSetLayout descLayout);
        [[nodiscard]] vk::PipelineLayout create_pipeline_layout(const std::vector<vk::DescriptorSetLayout>& descLayouts);

        // --- descriptors ---------------------------------------------------------------------------
        [[nodiscard]] vk::DescriptorSetLayout create_descriptor_set_layout(const std::vector<descriptor_item>& bindings);

        void update_descriptor_buffer(descriptor_buffer& buffer, const std::vector<descriptor_item>& items,
            descriptor_buffer_type type, u32 setIndexInBuffer = 0, void* pMappedData = nullptr);

        void update_descriptor_buffer(descriptor_buffer& buffer, const descriptor_item& item,
            descriptor_buffer_type type, u32 setIndexInBuffer = 0, void* pMappedData = nullptr);

        void update_descriptor_buffer(descriptor_buffer& buffer, const descriptor_item& item, u32 itemIndex,
            descriptor_buffer_type type, u32 setIndexInBuffer = 0, void* pMappedData = nullptr);

        void bind_descriptor_buffer(const std::vector<descriptor_buffer>& buffers, vk::CommandBuffer command_buffer);

        void bind_descriptor_set(vk::PipelineLayout layout, u32 set, u32 bufferIndex, vk::DeviceSize offset,
            vk::CommandBuffer command_buffer, vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eRayTracingKHR);

        void bind_descriptor_set(vk::PipelineLayout layout, u32 set, std::vector<u32> bufferIndex,
            std::vector<vk::DeviceSize> offset,
            vk::CommandBuffer command_buffer, vk::PipelineBindPoint bindPoint = vk::PipelineBindPoint::eRayTracingKHR);

    private:
        vk::detail::DispatchLoaderDynamic                       m_dyn_loader;
        vk::Instance                                            m_instance;
        vk::Device                                              m_device;
        vk::PhysicalDevice                                      m_physical_device;
        vk::PhysicalDeviceProperties                            m_device_properties;
        vk::PhysicalDeviceDescriptorBufferPropertiesEXT         m_descriptor_buffer_properties;
        VmaAllocator                                            m_vma_allocator;
        bool                                                    m_user_supplied_allocator = false;
        VmaPool                                                 m_current_pool = nullptr;
    };

}


#include "util/pch.h"

#include "util/descriptors.h"
#include "util/shader.h"
#include "util/device.h"

// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // These functions extract the addrss info / image info from the descriptor item
    // and bind it to the corresponding pointer in the  DescriptorDataEXT struct
    // they also return the size of the descriptor item
    static void get_info_of_descriptor_item(const descriptor_item& item, u32 resource_index, vk::DescriptorAddressInfoEXT* p_address_info,
        vk::DescriptorImageInfo* p_image_info, vk::Sampler* p_sampler, vk::DescriptorDataEXT* p_data) {

        switch (item.type) {

        // Resources
        case vk::DescriptorType::eUniformBuffer:
            *p_address_info = item.get_address_info(resource_index);
            p_data->pUniformBuffer = p_address_info;
            break;
        case vk::DescriptorType::eStorageBuffer:
            *p_address_info = item.get_address_info(resource_index);
            p_data->pStorageBuffer = p_address_info;
            break;
        case vk::DescriptorType::eAccelerationStructureKHR:
            p_data->accelerationStructure = item.get_acceleration_structure(resource_index);
            break;
        case vk::DescriptorType::eStorageTexelBuffer:
            *p_address_info = item.get_texel_addressinfo(resource_index);
            p_data->pStorageTexelBuffer = p_address_info;
            break;
        case vk::DescriptorType::eUniformTexelBuffer:
            *p_address_info = item.get_texel_addressinfo(resource_index);
            p_data->pUniformTexelBuffer = p_address_info;
            break;

        // Images
        case vk::DescriptorType::eSampler:
            p_sampler = item.get_sampler(resource_index);
            p_data->pSampler = p_sampler;
            break;
        case vk::DescriptorType::eCombinedImageSampler:
            *p_image_info = item.get_image_info(resource_index);
            p_data->pCombinedImageSampler = p_image_info;
            break;
        case vk::DescriptorType::eSampledImage:
            *p_image_info = item.get_image_info(resource_index);
            p_data->pSampledImage = p_image_info;
            break;
        case vk::DescriptorType::eStorageImage:
            *p_image_info = item.get_image_info(resource_index);
            p_data->pStorageImage = p_image_info;
            break;
        default: break;
        }
    }


    static size_t get_descriptor_type_data_size(vk::DescriptorType type, const vk::PhysicalDeviceDescriptorBufferPropertiesEXT &bufferProps) {

        switch (type)
        {
        case vk::DescriptorType::eUniformBuffer:            return bufferProps.uniformBufferDescriptorSize;
        case vk::DescriptorType::eStorageBuffer:            return bufferProps.storageBufferDescriptorSize;
        case vk::DescriptorType::eAccelerationStructureKHR: return bufferProps.accelerationStructureDescriptorSize;
        case vk::DescriptorType::eStorageTexelBuffer:       return bufferProps.storageTexelBufferDescriptorSize;
        case vk::DescriptorType::eUniformTexelBuffer:       return bufferProps.uniformTexelBufferDescriptorSize;
        case vk::DescriptorType::eStorageImage:             return bufferProps.storageImageDescriptorSize;
        case vk::DescriptorType::eCombinedImageSampler:     return bufferProps.combinedImageSamplerDescriptorSize;
        case vk::DescriptorType::eSampler:                  return bufferProps.samplerDescriptorSize;
        case vk::DescriptorType::eSampledImage:             return bufferProps.sampledImageDescriptorSize;
        default:
            return 0;
        }
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    vk::DescriptorSetLayout device::create_descriptor_set_layout(const std::vector<descriptor_item>& bindings) {

        bool has_dynamic = false;

        // prepare the layout bindings
        std::vector<vk::DescriptorSetLayoutBinding> layout_bindings;
        layout_bindings.reserve(bindings.size());

        for (auto& binding : bindings)
        {
            layout_bindings.push_back(binding.get_layout_binding());
            if (binding.dynamic_array_size > 0)
                has_dynamic = true;
        }

        // if there are dynamic bindings, we need to set the flags
        // the user is responsible for having the last binding in the set be a dynamic array

        // prepare the flags
        std::vector<vk::DescriptorBindingFlags> item_flags;
        if (has_dynamic)
        {
            item_flags.reserve(bindings.size());
            for (auto& binding : bindings)
            {
                item_flags.push_back((vk::DescriptorBindingFlagBits)0);
                if (binding.dynamic_array_size > 0)
                {
                    item_flags.back() |= (vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount);
                }
            }
        }

        auto flags = vk::DescriptorSetLayoutBindingFlagsCreateInfo()
            .setBindingCount(static_cast<u32>(item_flags.size()))
            .setPBindingFlags(item_flags.data());

        return m_device.createDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo()
                .setBindingCount(static_cast<u32>(layout_bindings.size()))
                .setFlags(vk::DescriptorSetLayoutCreateFlagBits::eDescriptorBufferEXT)
                .setPBindings(layout_bindings.data())
                .setPNext(has_dynamic ? &flags : nullptr));
    }


    void device::update_descriptor_buffer(descriptor_buffer& buffer, const descriptor_item& item, u32 itemIndex,
        descriptor_buffer_type /*type*/, u32 set_index_in_buffer, void* p_mapped_data) {

        u32 set_offset = buffer.get_offset_to_set(set_index_in_buffer);                                        // offset into the buffer
        char* mapped_data = p_mapped_data == nullptr ? (char *)map_buffer(buffer.buffer) + set_offset : (char *)p_mapped_data + set_offset;

        u32 data_size = get_descriptor_type_data_size(item.type, m_descriptor_buffer_properties);
        char* cursor = mapped_data + item.binding_offset + static_cast<size_t>(itemIndex) * data_size;              // add the array element offset
        // char* cursor = mapped_data + item.binding_offset;                                                        // cursor to the item we want to update

        auto desc_get_info = vk::DescriptorGetInfoEXT().setType(item.type);
        auto address_info = vk::DescriptorAddressInfoEXT();                                                         // in case of buffer
        auto image_info = vk::DescriptorImageInfo();                                                                // in case of image or sampler
        vk::Sampler sampler = nullptr;                                                                              // in case of sampler

        get_info_of_descriptor_item(item, itemIndex, &address_info, &image_info, &sampler, &desc_get_info.data);
        m_device.getDescriptorEXT(&desc_get_info, data_size, cursor, m_dyn_loader);                                 // write to cursor
        if (p_mapped_data == nullptr)
            unmap_buffer(buffer.buffer);
    }


    void device::update_descriptor_buffer(descriptor_buffer &buffer, const std::vector<descriptor_item> &items, 
        descriptor_buffer_type /*type*/, u32 set_index_in_buffer, void *p_mapped_data) {

        u32 set_offset = buffer.get_offset_to_set(set_index_in_buffer);                                                  // offset into the buffer
        char *mapped_data = p_mapped_data == nullptr ? (char *)map_buffer(buffer.buffer) + set_offset : (char *)p_mapped_data + set_offset;  // offset into the buffer and the right descriptor set
        char *cursor = mapped_data;                                                                                     // cursor to the current item
        auto desc_get_info = vk::DescriptorGetInfoEXT();
        auto address_info = vk::DescriptorAddressInfoEXT();                                                             // in case of buffer
        auto image_info = vk::DescriptorImageInfo();                                                                    // in case of image or sampler
        vk::Sampler sampler = nullptr;                                                                                  // in case of sampler

        for (u32 i = 0; i < items.size(); i++) {

            cursor = mapped_data + items[i].binding_offset;                                                              // move the cursor to the current item
            desc_get_info.type = items[i].type;                                                                         // same type for all items in the array
            size_t data_size = get_descriptor_type_data_size(items[i].type, m_descriptor_buffer_properties);

            u32 arraySize = items[i].dynamic_array_size > 0 ? items[i].dynamic_array_size : items[i].array_size;

            for (u32 j = 0; j < arraySize; j++) {

                get_info_of_descriptor_item(items[i], j, &address_info, &image_info, &sampler, &desc_get_info.data);
                m_device.getDescriptorEXT(&desc_get_info, data_size, cursor, m_dyn_loader);                                // write to cursor
                cursor += data_size;
            }
        }

        if (p_mapped_data == nullptr)                                                                                     // we can unmap the buffer now, because we wrote all the data to it
            unmap_buffer(buffer.buffer);
    }


    void device::update_descriptor_buffer(descriptor_buffer &buffer, const descriptor_item &item, descriptor_buffer_type type,
        u32 set_index_in_buffer, void *p_mapped_data) {

        char *mapped_data = p_mapped_data == nullptr ? (char *)map_buffer(buffer.buffer) : (char *)p_mapped_data;
        u32 arraySize = item.dynamic_array_size > 0 ? item.dynamic_array_size : item.array_size;

        for (u32 i = 0; i < arraySize; i++) {
            // update the buffer for each item in the array, this is fast, because we don't have to map the buffer for each item
            update_descriptor_buffer(buffer, item, i, type, set_index_in_buffer, mapped_data);
        }

        if (p_mapped_data == nullptr)
            unmap_buffer(buffer.buffer);
    }


    void device::bind_descriptor_buffer(const std::vector<descriptor_buffer>& buffers, vk::CommandBuffer command_buffer) {

        std::vector<vk::DescriptorBufferBindingInfoEXT> binding_infos;
        binding_infos.reserve(buffers.size());

        std::vector<u32> buffer_indices;
        buffer_indices.reserve(buffers.size());

        for (size_t i = 0; i < buffers.size(); i++) {

            binding_infos.push_back(vk::DescriptorBufferBindingInfoEXT()
                .setAddress(buffers[i].buffer.dev_address)
                .setUsage((vk::BufferUsageFlagBits)buffers[i].type));

            buffer_indices.push_back(i);
        }

        command_buffer.bindDescriptorBuffersEXT(binding_infos, m_dyn_loader);
    }


    void device::bind_descriptor_set(vk::PipelineLayout layout, u32 set, u32 bufferIndex, vk::DeviceSize offset, vk::CommandBuffer command_buffer,
        vk::PipelineBindPoint bind_point) {

        command_buffer.setDescriptorBufferOffsetsEXT(bind_point, layout, set, 1, &bufferIndex, &offset, m_dyn_loader);
    }


    void device::bind_descriptor_set(vk::PipelineLayout layout, u32 set, std::vector<u32> bufferIndex, std::vector<vk::DeviceSize> offset,
        vk::CommandBuffer command_buffer, vk::PipelineBindPoint bind_point) {

        command_buffer.setDescriptorBufferOffsetsEXT(bind_point, layout, set, bufferIndex.size(), bufferIndex.data(), offset.data(), m_dyn_loader);
    }


    vk::PipelineLayout device::create_pipeline_layout(const std::vector<vk::DescriptorSetLayout> &descriptor_set_layout) {

        // create pipeline layout
        return m_device.createPipelineLayout(vk::PipelineLayoutCreateInfo()
            .setSetLayoutCount(static_cast<u32>(descriptor_set_layout.size()))
            .setPSetLayouts(descriptor_set_layout.data()));
    }


    vk::PipelineLayout device::create_pipeline_layout(vk::DescriptorSetLayout descriptor_set_layout) {

        // create pipeline layout
        return m_device.createPipelineLayout(vk::PipelineLayoutCreateInfo()
            .setSetLayoutCount(1)
            .setFlags(vk::PipelineLayoutCreateFlagBits::eIndependentSetsEXT)
            .setPSetLayouts(&descriptor_set_layout));
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

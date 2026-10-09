
#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include "util/buffer.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Enum that is used to specify the type of descriptors that will be stored in the buffer when calling
    /// CreateDescriptorBuffer(...)
    // note These enums map straight to the Vulkan BufferUsageFlagBits
    enum class descriptor_buffer_type : u32 {

        resource = (u32)vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT,     // store resource descriptors, eg uniform buffers, storage buffers
        sampler = (u32)vk::BufferUsageFlagBits::eSamplerDescriptorBufferEXT,       // store image descriptors so sampled images
        combined = (u32)(vk::BufferUsageFlagBits::eSamplerDescriptorBufferEXT | vk::BufferUsageFlagBits::eResourceDescriptorBufferEXT) // store combined image samplers
    };


    struct descriptor_buffer {

        // The buffer that will store the descriptors
        allocated_buffer                                buffer;

        // Number of IDENTICAL descriptor sets in the buffer
        // note This is useful for offsetting into the buffer that has multiple IDENTICAL descriptor sets and binding
        /// one of them
        u32                                             set_count = 0;

        // Size of a single descriptor in the buffer
        u32                                             single_descriptor_size = 0;

        // Type of descriptors that will be stored in the buffer, Default is Resource
        descriptor_buffer_type                          type = descriptor_buffer_type::resource;

        // If there are multiple descriptor sets in the buffer, this is the offset to the start of the set
        // param set_index The index of the set to get the offset to
        // return The offset to the start of the set
        u32 get_offset_to_set(u32 set_index) const { return set_index * single_descriptor_size; }
    };


    // Structure that defines a single descriptor item, such as a uniform buffer, storage buffer, image sampler,
    /// etc
    // note This supports having arrays of descriptors, such as an array of uniform buffers, or just a single
    /// descriptor
    struct descriptor_item {

        // Default constructor
        // param binding The binding of the descriptor in the shader
        // param type The type of descriptor, eg uniform buffer, storage buffer, image sampler, etc
        // param stageFlags The shader stages that the descriptor will be used in
        // param ArraySize The size of the binding array, if the array is dynamic, this is the max size
        // param pItems Pointer to the items that will be stored in the descriptor, this can be a buffer, image, etc.
        /// Can be null and set later, but must be set before calling UpdateDescriptorSet(...)
        // param dynamicArraySize If this is non-zero, the array is dynamic and this is the size of the array that you
        /// want to be used. pItems must have dynamicArraySize many items
        descriptor_item(u32 binding, vk::DescriptorType type, vk::ShaderStageFlags stage_flags, u32 array_size, void* p_items = nullptr, u32 dynamic_array_size = 0)
            : type(type), binding(binding), binding_offset(0), array_size(array_size), stage_flags(stage_flags), dynamic_array_size(dynamic_array_size),
              p_resources(reinterpret_cast<allocated_buffer*>(p_items)) // even if the item isn't a buffer, we can use this field, since its a union and a 64-bit address
        { }

        vk::DescriptorType                              type;               // Type of resource, eg uniform buffer, storage buffer, image sampler, etc
        u32                                             binding = 0;        // Binding of the descriptor in the shader
        u32                                             binding_offset = 0; // Offset of the binding in the descriptor set (filled in when creating the descriptor set)
        u32                                             array_size = 0;     // Size of the binding array, if it is dynamic, this is the max size
        vk::ShaderStageFlags                            stage_flags = vk::ShaderStageFlagBits::eAll;   // shader stages that the descriptor will be used in

        // If this is non-zero, the descriptor is dynamic and specifies of how many items you want to update
        /// when calling UpdateDescriptorSet(...)
        // warning UpdateDescriptorSet(...) will throw a segfault if pItems is null or DynamicArraySize is bigger than
        /// the size of the pointer to an array of items
        u32 dynamic_array_size = 0;

        // all of these are 64 bit pointers, so we can use a union
        union {

            allocated_buffer*                           p_resources = nullptr;              // Pointer to the resources that will be stored in the descriptor
            accessible_image*                           p_images;                           // Pointer to the images that will be stored in the descriptor
            vk::DeviceAddress*                          p_acceleration_structures;          // Pointer to the acceleration structures that will be stored in the descriptor
            allocated_texel_buffer*                     p_texel_buffers;                    // Pointer to the texel buffers that will be stored in the descriptor
        };


        // Gets the layout binding for the descriptor
        // return The layout binding for the descriptor
        vk::DescriptorSetLayoutBinding get_layout_binding() const {

            return vk::DescriptorSetLayoutBinding()
                .setBinding(binding)
                .setDescriptorType(type)
                .setDescriptorCount(array_size)
                .setStageFlags(stage_flags);
        }


        // Gets the device address of the acceleration structure
        // param resource_index The index of the resource to get the address of from the array of resources
        // return The device address of the acceleration structure
        // warning This will throw a segfault if the array is null or the index is out of bounds
        vk::DeviceAddress get_acceleration_structure(u32 resource_index = 0) const { return p_acceleration_structures[resource_index]; }


        // Gets the image view of the image
        // param resource_index The index of the resource to get the image view of from the array of resources
        // return The image view of the image
        vk::DescriptorAddressInfoEXT get_texel_addressinfo(u32 resource_index = 0) const {

            return vk::DescriptorAddressInfoEXT()
                .setRange(p_texel_buffers[resource_index].buffer.size)
                .setFormat(p_texel_buffers[resource_index].format)
                .setAddress(p_texel_buffers[resource_index].buffer.dev_address);
        }


        // Gets the AddressInfo of the resource
        // param resource_index The index of the resource to get the address info of from the array of resources
        // return The filled AddressInfo of the resource
        // warning This will throw a segfault if the array is null or the index is out of bounds
        vk::DescriptorAddressInfoEXT get_address_info(u32 resource_index = 0) const {

            auto address_info = vk::DescriptorAddressInfoEXT()
                .setRange(p_resources[resource_index].size)
                .setFormat(vk::Format::eUndefined)
                .setAddress(p_resources[resource_index].dev_address);

            return address_info;
        }


        // Gets the image view of the image
        // param resource_index The index of the resource to get the image view of from the array of resources
        // return The image view of the image
        // warning This will throw a segfault if the array is null or the index is out of bounds
        vk::DescriptorImageInfo get_image_info(u32 resource_index = 0) const {

            return vk::DescriptorImageInfo()
                .setImageView(p_images[resource_index].view ? p_images[resource_index].view : vk::ImageView())
                .setSampler(p_images[resource_index].sampler ? p_images[resource_index].sampler : vk::Sampler())
                .setImageLayout(p_images[resource_index].layout);
        }


        // Gets the sampler of the image
        // param resource_index The index of the resource to get the sampler of from the array of resources
        // return The pointer to the sampler of the image
        vk::Sampler *get_sampler(u32 resource_index = 0) const { return &p_images[resource_index].sampler; }


        // Gets the buffer info of the buffer
        // param resource_index The index of the resource to get the buffer info of from the array of resources
        // return The buffer info of the buffer
        // warning This will throw a segfault if the array is null or the index is out of bounds
        vk::DescriptorBufferInfo get_buffer_info(u32 resource_index = 0) const {

            return vk::DescriptorBufferInfo()
                .setBuffer(p_resources[resource_index].buffer)
                .setRange(p_resources[resource_index].size);
        }
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

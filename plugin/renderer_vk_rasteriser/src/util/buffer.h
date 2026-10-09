
#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::util {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Structure of a Buffer used for calls with vk_ray
    struct allocated_buffer {

        // @brief The allocation for the buffer, if this is null, the buffer is not allocated by vk_ray
        // @note If this is not null, the buffer is allocated by vk_ray and should be freed by calling
        // DestroyBuffer(...) If the buffer is allocated by the user, they do not need to set this field.
        VmaAllocation           allocation = nullptr;
        vk::Buffer              buffer = nullptr;                       // @brief The raw buffer handle
        vk::DeviceAddress       dev_address = 0;                        // @brief The device address of the buffer
        u64                     size = 0;                               // @brief The size of the buffer, without any alignment
    };


    struct allocated_texel_buffer {

        allocated_buffer        buffer;                                 // @brief The allocation for the buffer
        vk::Format              format = vk::Format::eUndefined;        // @brief The format of the texel buffer
    };


    // @brief Structure of an Image, to use with vk_ray
    struct allocated_image {

        // @brief The allocation for the image, if this is null, the image is not allocated by vk_ray
        // @note If this is not null, the image is allocated by vk_ray and should be freed by calling DestroyImage(...)
        // If the buffer is allocated by the user, they do not need to set this field.
        VmaAllocation           allocation = nullptr;
        vk::Image               image = nullptr;                        // @brief The raw image handle
        uint32_t                width = 0;
        uint32_t                height = 0;
        u64                     size = 0;
    };


    // @brief Structure of an Image, to use with vk_ray
    // @note Primarily used for images for DescriptorSets
    struct accessible_image {

        vk::ImageView           view = nullptr;                         // @brief The image view of the image
        vk::Sampler             sampler = nullptr;                      // @brief Optional sampler for the image
        vk::ImageLayout         layout = vk::ImageLayout::eUndefined;   // @brief The image layout of the image
        vk::DescriptorSet       descriptor_set = nullptr;                   // @brief Descriptor set to display the image with ImGui
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Aligns a value up to the specified alignment
    // @param value The value to align
    // @param alignment The alignment to align the value to
    // @return The aligned value
    uint32_t align_up(uint32_t value, uint32_t alignment);


    // @brief Aligns a value up to the specified alignment
    // @param value The value to align
    // @param alignment The alignment to align the value to
    // @return The aligned value
    u64 align_up(u64 value, u64 alignment);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}

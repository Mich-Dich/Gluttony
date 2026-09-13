#pragma once

#include <vulkan/vulkan.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif


#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_renderer_plugin.h>

#include "util/utils.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    static u32 bytes_per_pixel(const GLT::render::image_format format);

    static vk::Format image_format_to_vulkan_format(const GLT::render::image_format format);

    static void generate_mipmaps(vk::CommandBuffer cmd, vk::Image image, u32 width, u32 height, u32 mip_levels);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    static u32 bytes_per_pixel(const GLT::render::image_format format) {

        switch (format) {
            case GLT::render::image_format::RGBA:    return 4;
            case GLT::render::image_format::RGBA32F: return 16;
            default: return 0;
        }
    }


    static vk::Format image_format_to_vulkan_format(const GLT::render::image_format format) {

        switch (format) {
            case GLT::render::image_format::RGB:        return vk::Format::eR8G8B8Unorm;
            case GLT::render::image_format::RGBA:       return vk::Format::eR8G8B8A8Unorm;
            case GLT::render::image_format::RGBA32F:    return vk::Format::eR32G32B32A32Sfloat;
            case GLT::render::image_format::RGBA16F:    return vk::Format::eR16G16B16A16Sfloat;
            default:                                    return vk::Format::eUndefined;
        }
    }

    // Successively blits each mip level down from the previous one, transitioning each level to eShaderReadOnlyOptimal as it finishes being used as a blit source.
    static void generate_mipmaps(vk::CommandBuffer cmd, vk::Image image, u32 width, u32 height, u32 mip_levels) {

        i32 mip_width = static_cast<i32>(width);
        i32 mip_height = static_cast<i32>(height);
        for (u32 index = 1; index < mip_levels; index++) {

            vk::ImageSubresourceRange prev_level_range = vk::ImageSubresourceRange()
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(index - 1)
                .setLevelCount(1)
                .setBaseArrayLayer(0)
                .setLayerCount(1);

            // level index-1 was written as a transfer-dst (or is the base level) -> make it a transfer-src for the blit
            auto barrier = vk::ImageMemoryBarrier()
                .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setImage(image)
                .setSubresourceRange(prev_level_range)
                .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                .setDstAccessMask(vk::AccessFlagBits::eTransferRead);

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                (vk::DependencyFlagBits)0, 0, nullptr, 0, nullptr, 1, &barrier);

            const i32 next_width = mip_width > 1 ? mip_width / 2 : 1;
            const i32 next_height = mip_height > 1 ? mip_height / 2 : 1;

            vk::ImageBlit blit = vk::ImageBlit()
                .setSrcOffsets({vk::Offset3D{0, 0, 0}, vk::Offset3D{mip_width, mip_height, 1}})
                .setSrcSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(index - 1).setBaseArrayLayer(0).setLayerCount(1))
                .setDstOffsets({vk::Offset3D{0, 0, 0}, vk::Offset3D{next_width, next_height, 1}})
                .setDstSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(index).setBaseArrayLayer(0).setLayerCount(1));

            cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, 1, &blit, vk::Filter::eLinear);

            // level index-1 is done being read from, move it to its final shader-readable layout
            auto to_shader_read = vk::ImageMemoryBarrier()
                .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
                .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
                .setImage(image)
                .setSubresourceRange(prev_level_range)
                .setSrcAccessMask(vk::AccessFlagBits::eTransferRead)
                .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                (vk::DependencyFlagBits)0, 0, nullptr, 0, nullptr, 1, &to_shader_read);

            if (mip_width > 1) mip_width /= 2;
            if (mip_height > 1) mip_height /= 2;
        }

        // the last mip level was never a blit source, so it's still eTransferDstOptimal -> move it to shader-read too
        vk::ImageSubresourceRange last_level_range = vk::ImageSubresourceRange()
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(mip_levels - 1)
            .setLevelCount(1)
            .setBaseArrayLayer(0)
            .setLayerCount(1);

        auto final_barrier = vk::ImageMemoryBarrier()
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
            .setImage(image)
            .setSubresourceRange(last_level_range)
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
            (vk::DependencyFlagBits)0, 0, nullptr, 0, nullptr, 1, &final_barrier);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    image::image() {         
        
        allocate_memory(nullptr, glm::uvec3{2, 2, 1}, GLT::render::image_format::RGBA, false);      // super small image buffer
    }


    image::image(const glm::uvec3 size) {
        
        allocate_memory(nullptr, size, GLT::render::image_format::RGBA, false);      // super small image buffer
    }


    image::image(const std::filesystem::path& image_path, const bool mipmapped) {

        int channels;
        int width = 0, height = 0;
        void* data = stbi_load(image_path.string().c_str(), &width, &height, &channels, 4);
        allocate_memory(data, glm::uvec3{width, height, 1}, GLT::render::image_format::RGBA, mipmapped);
        stbi_image_free(data);
    }


	image::~image()                             { release(); }

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    glm::uvec2 image::get_size()                { return glm::uvec2{m_allocated_image.width, m_allocated_image.height}; }


    void* image::get_descriptor_set() {

        if (m_accessible_image.descriptor_set)
            return m_accessible_image.descriptor_set;
        
        vk::ImageLayout layout = m_accessible_image.layout;             // Ensure the image layout is correct for sampling
        if (layout == vk::ImageLayout::eUndefined)
            layout = vk::ImageLayout::eShaderReadOnlyOptimal;           // OR shader read only optimal

        m_accessible_image.descriptor_set = static_cast<vk::DescriptorSet>(ImGui_ImplVulkan_AddTexture(
            static_cast<VkImageView>(m_accessible_image.view),
            static_cast<VkImageLayout>(layout)));
        
        return m_accessible_image.descriptor_set;
    }


    void* image::load(const std::filesystem::path& path, u32& out_width, u32& out_height) {

        int width, height, channels;
        u8* buffer = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        VALIDATE(buffer != nullptr, return nullptr, "", "Could not load image from path [{}]", path.generic_string())

        out_width = width;
        out_height = height;
        return buffer;                                                  // caller owns the memory, free with stbi_image_free
    }


    void image::resize(const glm::uvec3& new_size, const GLT::render::image_format format, const bool mipmapped) {

        const glm::uvec3 current_size{ m_allocated_image.width, m_allocated_image.height, 1 };
        if (current_size == new_size) 
            return;                                                     // already the right size, nothing to do

        m_renderer->get_vk_device().waitIdle();                         // Make sure the GPU is done with the image before we destroy it
        release();
        allocate_memory(nullptr, new_size, format, mipmapped);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void image::allocate_memory(const void* data, const glm::uvec3 size, const GLT::render::image_format format, const bool mipmapped) {

        m_renderer = GLT::plugin_manager::get_plugin_ref<GLT::renderer_vk_ray::renderer>(GLT::plugin_manager::interface::renderer);
        vr::device* vr_dev = m_renderer->get_vr_dev();
        vk::Device vk_device = m_renderer->get_vk_device();
        const u32 mip_levels = mipmapped ? static_cast<u32>(std::floor(std::log2(std::max(size.x, size.y)))) + 1 : 1;

        const auto image_create_info = vk::ImageCreateInfo()            // Create an image to render to
            .setImageType(vk::ImageType::e2D)
            .setFormat(image_format_to_vulkan_format(format))
            .setExtent(vk::Extent3D(size.x, size.y, size.z))
            .setMipLevels(mip_levels)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eSampled |
                vk::ImageUsageFlagBits::eStorage |
                vk::ImageUsageFlagBits::eTransferSrc |
                vk::ImageUsageFlagBits::eTransferDst)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        // create the image with dedicated memory
        m_allocated_image = vr_dev->create_image(image_create_info, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);

        assign_data(data, size, format, mip_levels);

        const auto view_create_info = vk::ImageViewCreateInfo()         // create a view for the image
            .setImage(m_allocated_image.image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(image_format_to_vulkan_format(format))
            .setSubresourceRange(vk::ImageSubresourceRange()
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(mip_levels)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        m_accessible_image.view = vk_device.createImageView(view_create_info);
    }


    void image::assign_data(const void* data, const glm::uvec3 size, const GLT::render::image_format format, const u32 mip_levels) {

        vr::device* vr_dev = m_renderer->get_vr_dev();
        const size_t data_size = size.x * size.y * size.z * bytes_per_pixel(format);
        if (data != nullptr) {

            vr::allocated_buffer staging = vr_dev->create_buffer(data_size, vk::BufferUsageFlagBits::eTransferSrc,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            vr_dev->update_buffer(staging, const_cast<void*>(data), data_size);
            m_renderer->immediate_submit([&](vk::CommandBuffer cmd) {

                const vk::ImageSubresourceRange base_range = vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(0)
                    .setLevelCount(mip_levels)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1);

                vr_dev->transition_image_layout(cmd, m_allocated_image.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                    base_range, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer);

                const vk::BufferImageCopy copy_region = vk::BufferImageCopy()
                    .setImageSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1))
                    .setImageExtent({size.x, size.y, 1});

                cmd.copyBufferToImage(staging.buffer, m_allocated_image.image, vk::ImageLayout::eTransferDstOptimal, 1, &copy_region);

                if (mip_levels > 1)
                    generate_mipmaps(cmd, m_allocated_image.image, size.x, size.y, mip_levels); // leaves every level in eShaderReadOnlyOptimal
                else
                    vr_dev->transition_image_layout(cmd, 
                        m_allocated_image.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                        base_range,              vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader);
            });
            vr_dev->destroy_buffer(staging);
        }
    }


    void image::release() {

        if (!m_renderer)
            return;                                                     // never allocated, nothing to do

        vr::device* vr_dev = m_renderer->get_vr_dev();
        vk::Device vk_device = m_renderer->get_vk_device();

        if (m_accessible_image.descriptor_set)
            ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(m_accessible_image.descriptor_set));

        if (m_accessible_image.view)                                    // Destroy image view
            vk_device.destroyImageView(m_accessible_image.view);

        if (m_allocated_image.image)                                    // destroy the image
            vr_dev->destroy_image(m_allocated_image);

        m_accessible_image.descriptor_set = nullptr;
        m_accessible_image.view = nullptr;
        m_accessible_image = {};
        m_allocated_image.image = nullptr;
        m_allocated_image = {};
    }

}

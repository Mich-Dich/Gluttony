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
            case GLT::render::image_format::RGBA:    return vk::Format::eR8G8B8A8Unorm;
            case GLT::render::image_format::RGBA32F: return vk::Format::eR32G32B32A32Sfloat;
            default:                    return vk::Format::eUndefined;
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

    image::image(const std::filesystem::path& image_path) {

        int channels;
        int width = 0, height = 0;
        void* data = stbi_load(image_path.string().c_str(), &width, &height, &channels, 4);
        VALIDATE(data != nullptr, return, "", "Could not load image from path [{}]", image_path.generic_string())

        // no explicit format given -> stbi_load with 4 requested channels always yields 8-bit RGBA
        allocate_memory(data, glm::uvec3{width, height, 1}, GLT::render::image_format::RGBA, false);
        stbi_image_free(data);
    }


	image::~image()                 { release(); }

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    u32 image::get_width()          { return m_extend.x; }


    u32 image::get_height()         { return m_extend.y; }


    void* image::load(const std::filesystem::path& path, u32& out_width, u32& out_height) {

        int width, height, channels;
        u8* buffer = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        VALIDATE(buffer != nullptr, return nullptr, "", "Could not load image from path [{}]", path.generic_string())

        out_width = width;
        out_height = height;
        return buffer; // caller owns the memory, free with stbi_image_free
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void image::allocate_memory(const void* data, const glm::uvec3 size, const GLT::render::image_format format, 
        const bool mipmapped) {

        m_renderer = GLT::plugin_manager::get_plugin_ref<GLT::renderer_vk_ray::renderer>(GLT::plugin_manager::interface::renderer);
        
        const size_t data_size = size.x * size.y * size.z * bytes_per_pixel(format);
        const vk::Format vk_format = image_format_to_vulkan_format(format);
        vk::ImageUsageFlags full_usage =            // every image needs to be a blit/copy target, and sampled from in shaders
            vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eTransferSrc |
            vk::ImageUsageFlagBits::eSampled;
        allocate_image(size, vk_format, full_usage, mipmapped);

        vr::device* device = m_renderer->get_vr_dev();
        const u32 mip_levels = mipmapped ? static_cast<u32>(std::floor(std::log2(std::max(size.x, size.y)))) + 1 : 1;
        if (data != nullptr) {

            vr::allocated_buffer staging = device->create_buffer(data_size, vk::BufferUsageFlagBits::eTransferSrc,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            device->update_buffer(staging, const_cast<void*>(data), data_size);

            m_renderer->immediate_submit([&](vk::CommandBuffer cmd) {

                vk::ImageSubresourceRange base_range = vk::ImageSubresourceRange()
                    .setAspectMask(vk::ImageAspectFlagBits::eColor)
                    .setBaseMipLevel(0)
                    .setLevelCount(mip_levels)
                    .setBaseArrayLayer(0)
                    .setLayerCount(1);

                device->transition_image_layout(cmd, m_image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                    base_range, vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer);

                vk::BufferImageCopy copy_region = vk::BufferImageCopy()
                    .setImageSubresource(vk::ImageSubresourceLayers().setAspectMask(vk::ImageAspectFlagBits::eColor).setMipLevel(0).setBaseArrayLayer(0).setLayerCount(1))
                    .setImageExtent({size.x, size.y, 1});

                cmd.copyBufferToImage(staging.buffer, m_image, vk::ImageLayout::eTransferDstOptimal, 1, &copy_region);

                if (mip_levels > 1)
                    generate_mipmaps(cmd, m_image, size.x, size.y, mip_levels); // leaves every level in eShaderReadOnlyOptimal
                else
                    device->transition_image_layout(cmd, m_image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                        base_range, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader);
            });

            device->destroy_buffer(staging);
        }

        vk::ImageViewCreateInfo view_info = vk::ImageViewCreateInfo()
            .setImage(m_image)
            .setViewType(vk::ImageViewType::e2D)
            .setFormat(vk_format)
            .setSubresourceRange(vk::ImageSubresourceRange()
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(mip_levels)
                .setBaseArrayLayer(0)
                .setLayerCount(1));

        m_image_view = device->get_device().createImageView(view_info); // adjust accessor name if needed
        m_extend = size;
        m_initialized = true;
    }


    void image::allocate_image(const glm::uvec3 size, const vk::Format format, const vk::ImageUsageFlags usage, const bool mipmapped) {

        const u32 mip_levels = mipmapped ? static_cast<u32>(std::floor(std::log2(std::max(size.x, size.y)))) + 1 : 1;
        vk::ImageCreateInfo image_info = vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(format)
            .setExtent(vk::Extent3D{size.x, size.y, 1})
            .setMipLevels(mip_levels)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(usage)
            .setInitialLayout(vk::ImageLayout::eUndefined)
            .setSharingMode(vk::SharingMode::eExclusive);

        // create_image fills in width/height/size (allocation size in bytes) on the returned struct itself,
        // so there's no need to set m_allocated_image fields manually beforehand
        vr::device* device = m_renderer->get_vr_dev();
        m_allocated_image = device->create_image(image_info, 0, nullptr);
        m_image = m_allocated_image.image;

        m_initialized = true;
    }


    void image::release() {

        if (!m_initialized)
            return;

        vr::device* device = m_renderer->get_vr_dev();
        if (m_descriptor_set) {

            ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(m_descriptor_set));
            m_descriptor_set = nullptr;
        }

        if (m_image_view) {

            device->get_device().destroyImageView(m_image_view);
            m_image_view = nullptr;
        }

        device->destroy_image(m_allocated_image);
        m_image = nullptr;
        m_initialized = false;
    }

}

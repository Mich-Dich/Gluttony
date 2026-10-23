
#include "util/pch.h"
#include "util/util_pch.h"
#include "shared_resource_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct texture_format_desc {
        vk::Format                          format = vk::Format::eUndefined;
        u32                                 texel_bytes = 0;
        u32                                 block_bytes = 0;
        u32                                 source_components = 0;
        u32                                 component_bytes = 0;
        bool                                float_components = false;
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    bool describe_texture_format(GLT::asset::texture::pixel_format pf, GLT::asset::texture::color_space space, texture_format_desc& out);


    void expand_rgb_to_rgba(const std::byte* src, std::byte* dst, u64 pixel_count, u32 component_bytes, bool is_float);


    void image_barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
        vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
        vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access, u32 base_mip, u32 mip_count);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool describe_texture_format(GLT::asset::texture::pixel_format pf, GLT::asset::texture::color_space space, texture_format_desc& out) {

        using PF = GLT::asset::texture::pixel_format;
        using VF = vk::Format;
        const bool srgb = space == GLT::asset::texture::color_space::srgb;

        switch (pf) {
            case PF::u8_r:     out = { srgb ? VF::eR8Srgb : VF::eR8Unorm, 1 };                          return true;
            case PF::u8_rg:    out = { srgb ? VF::eR8G8Srgb : VF::eR8G8Unorm, 2 };                      return true;
            case PF::u8_rgb:   out = { srgb ? VF::eR8G8B8A8Srgb : VF::eR8G8B8A8Unorm, 4, 0, 3, 1 };     return true;
            case PF::u8_rgba:  out = { srgb ? VF::eR8G8B8A8Srgb : VF::eR8G8B8A8Unorm, 4 };              return true;
            case PF::u16_r:    out = { VF::eR16Unorm, 2 };                                              return true;
            case PF::u16_rg:   out = { VF::eR16G16Unorm, 4 };                                           return true;
            case PF::u16_rgb:  out = { VF::eR16G16B16A16Unorm, 8, 0, 3, 2 };                            return true;
            case PF::u16_rgba: out = { VF::eR16G16B16A16Unorm, 8 };                                     return true;
            case PF::f32_r:    out = { VF::eR32Sfloat, 4, 0, 0, 0, true };                              return true;
            case PF::f32_rg:   out = { VF::eR32G32Sfloat, 8, 0, 0, 0, true };                           return true;
            case PF::f32_rgb:  out = { VF::eR32G32B32A32Sfloat, 16, 0, 3, 4, true };                    return true;
            case PF::f32_rgba: out = { VF::eR32G32B32A32Sfloat, 16, 0, 0, 0, true };                    return true;
            case PF::bc1:      out = { srgb ? VF::eBc1RgbaSrgbBlock : VF::eBc1RgbaUnormBlock, 0, 8 };   return true;
            case PF::bc3:      out = { srgb ? VF::eBc3SrgbBlock : VF::eBc3UnormBlock, 0, 16 };          return true;
            case PF::bc4:      out = { VF::eBc4UnormBlock, 0, 8 };                                      return true;
            case PF::bc5:      out = { VF::eBc5UnormBlock, 0, 16 };                                     return true;
            case PF::bc6h:     out = { VF::eBc6HUfloatBlock, 0, 16 };                                   return true;
            case PF::bc7:      out = { srgb ? VF::eBc7SrgbBlock : VF::eBc7UnormBlock, 0, 16 };          return true;
            default: return false;
        }
    }


    void expand_rgb_to_rgba(const std::byte* src, std::byte* dst, u64 pixel_count, u32 component_bytes, bool is_float) {

        std::byte alpha[4]{};
        if (is_float) {
            const f32 one = 1.0f;
            std::memcpy(alpha, &one, sizeof(one));
        
        } else {

            for (u32 i = 0; i < component_bytes; i++)
                alpha[i] = std::byte{0xFF};   
        }
        for (u64 i = 0; i < pixel_count; i++) {
            std::memcpy(dst + i * 4 * component_bytes, src + i * 3 * component_bytes, 3 * component_bytes);
            std::memcpy(dst + i * 4 * component_bytes + 3 * component_bytes, alpha, component_bytes);
        }
    }


    void image_barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
        vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
        vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access, u32 base_mip, u32 mip_count) {

        vk::ImageMemoryBarrier2 b{};
        b.srcStageMask = src_stage;
        b.srcAccessMask = src_access;
        b.dstStageMask = dst_stage;
        b.dstAccessMask = dst_access;
        b.oldLayout = from;
        b.newLayout = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, base_mip, mip_count, 0, 1);

        vk::DependencyInfo di{};
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers = &b;
        cmd.pipelineBarrier2(di);
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    std::expected<shared_texture_id, upload_error> shared_resource_manager::ensure_texture(GLT::asset::handle handle) {

        if (handle == INVALID_HANDLE)
            return std::unexpected(upload_error::make(upload_error::code::not_found, "invalid handle"));

        if (const auto it = m_texture_slot_by_asset.find(handle); it != m_texture_slot_by_asset.end()) {
            m_gpu_textures[it->second].ref_count++;
            return it->second;
        }

        const u32 slot = acquire_texture_internal(handle);
        if (slot == TEXTURE_SLOT_WHITE && handle != INVALID_HANDLE)
            return std::unexpected(upload_error::make(upload_error::code::decode_failed, "texture upload failed"));
        return slot;
    }


    void shared_resource_manager::retain_texture(shared_texture_id id) {
        if (id < m_gpu_textures.size() && m_gpu_textures[id].alive) m_gpu_textures[id].ref_count++;
    }


    void shared_resource_manager::release_texture(shared_texture_id id) { release_texture_internal(id); }


    bool shared_resource_manager::is_texture_ready(shared_texture_id id) const {
        return id < m_gpu_textures.size() && m_gpu_textures[id].alive;
    }


    const util::accessible_image& shared_resource_manager::get_texture(shared_texture_id id) const {
        if (id >= m_texture_table.size())
            return m_texture_table[TEXTURE_SLOT_WHITE];
        return m_texture_table[id];
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void shared_resource_manager::create_fallback_textures() {

        const auto create = [&](u32 slot, std::array<std::byte, 4> rgba) {
            const texture_upload_mip mip{0, 1, 1};
            gpu_texture t{};
            const bool ok = create_gpu_texture(vk::Format::eR8G8B8A8Unorm, 1, 1, rgba.data(), rgba.size(),
                std::span(&mip, 1), false, t);
            ASSERT(ok, "", "Failed to create fallback texture");
            t.ref_count = 1;
            t.alive = true;
            m_gpu_textures[slot] = t;
            m_texture_table[slot].view = t.view;
            m_texture_table[slot].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        };

        create(TEXTURE_SLOT_WHITE,       { std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255} });
        create(TEXTURE_SLOT_FLAT_NORMAL, { std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255} });
    }


    bool shared_resource_manager::create_gpu_texture(vk::Format format, u32 width, u32 height,
        const std::byte* pixels, u64 pixel_bytes,
        std::span<const texture_upload_mip> mips, bool generate_mips, gpu_texture& out) {

        using Stage = vk::PipelineStageFlagBits2;
        using Access = vk::AccessFlagBits2;

        const auto props = m_physical_device.getFormatProperties(format);
        const auto sampled = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        if ((props.optimalTilingFeatures & sampled) != sampled) return false;

        const auto blit_features = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
        generate_mips = generate_mips && mips.size() == 1 && std::max(width, height) > 1
            && (props.optimalTilingFeatures & blit_features) == blit_features;

        const u32 mip_count = generate_mips
            ? static_cast<u32>(std::bit_width(std::max(width, height)))
            : static_cast<u32>(mips.size());

        vk::ImageCreateInfo ii{};
        ii.imageType = vk::ImageType::e2D;
        ii.format = format;
        ii.extent = vk::Extent3D(width, height, 1);
        ii.mipLevels = mip_count;
        ii.arrayLayers = 1;
        ii.samples = vk::SampleCountFlagBits::e1;
        ii.tiling = vk::ImageTiling::eOptimal;
        ii.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst
                         | (generate_mips ? vk::ImageUsageFlagBits::eTransferSrc : vk::ImageUsageFlags{});
        ii.sharingMode = vk::SharingMode::eExclusive;
        ii.initialLayout = vk::ImageLayout::eUndefined;

        out = {};
        out.image = m_dev->create_image(ii, 0);
        if (!out.image.image || !out.image.allocation) return false;

        vk::ImageViewCreateInfo vi{};
        vi.image = out.image.image;
        vi.viewType = vk::ImageViewType::e2D;
        vi.format = format;
        vi.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, mip_count, 0, 1);
        out.view = m_device.createImageView(vi);
        if (!out.view) { m_dev->destroy_image(out.image); out = {}; return false; }

        util::allocated_buffer staging = m_dev->create_buffer(pixel_bytes, vk::BufferUsageFlagBits::eTransferSrc,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        if (!staging.buffer || !staging.allocation) {
            m_device.destroyImageView(out.view); m_dev->destroy_image(out.image); out = {}; return false;
        }
        m_dev->update_buffer(staging, pixels, pixel_bytes);

        const vk::Image image = out.image.image;

        try {
            immediate_submit([&](vk::CommandBuffer cmd) {

                image_barrier(cmd, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                    Stage::eNone, {}, Stage::eAllTransfer, Access::eTransferWrite, 0, mip_count);

                std::vector<vk::BufferImageCopy> regions; regions.reserve(mips.size());
                for (u32 level = 0; level < mips.size(); ++level) {
                    vk::BufferImageCopy r{};
                    r.bufferOffset = mips[level].offset;
                    r.imageSubresource= vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, 1);
                    r.imageExtent = vk::Extent3D(mips[level].width, mips[level].height, 1);
                    regions.push_back(r);
                }
                cmd.copyBufferToImage(staging.buffer, image, vk::ImageLayout::eTransferDstOptimal, regions);

                if (generate_mips) {
                    i32 w = static_cast<i32>(width), h = static_cast<i32>(height);
                    for (u32 level = 1; level < mip_count; ++level) {
                        image_barrier(cmd, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                            Stage::eAllTransfer, Access::eTransferWrite, Stage::eAllTransfer, Access::eTransferRead, level - 1, 1);
                        const i32 nw = std::max(w / 2, 1), nh = std::max(h / 2, 1);
                        vk::ImageBlit blit{};
                        blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level - 1, 0, 1);
                        blit.srcOffsets[1] = vk::Offset3D(w, h, 1);
                        blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, 1);
                        blit.dstOffsets[1] = vk::Offset3D(nw, nh, 1);
                        cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal,
                            blit, vk::Filter::eLinear);
                        w = nw; h = nh;
                    }
                    if (mip_count > 1)
                        image_barrier(cmd, image, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                            Stage::eAllTransfer, Access::eTransferRead, Stage::eFragmentShader, Access::eShaderSampledRead, 0, mip_count - 1);
                    image_barrier(cmd, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                        Stage::eAllTransfer, Access::eTransferWrite, Stage::eFragmentShader, Access::eShaderSampledRead, mip_count - 1, 1);
                } else {
                    image_barrier(cmd, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                        Stage::eAllTransfer, Access::eTransferWrite, Stage::eFragmentShader, Access::eShaderSampledRead, 0, mip_count);
                }
            });
        } catch (const std::exception& e) {
            LOG(fatal, "create_gpu_texture: immediate_submit failed: {}", e.what());
            m_dev->destroy_buffer(staging);
            m_device.destroyImageView(out.view);
            m_dev->destroy_image(out.image);
            out = {};
            return false;
        }

        m_dev->destroy_buffer(staging);
        return true;
    }


    void shared_resource_manager::destroy_gpu_texture(gpu_texture& t) {
        if (t.view)          m_device.destroyImageView(t.view);
        if (t.image.image)   m_dev->destroy_image(t.image);
        t = {};
    }


    u32 shared_resource_manager::acquire_texture_internal(GLT::asset::handle handle) {

        if (handle == INVALID_HANDLE)
            return TEXTURE_SLOT_WHITE;

        if (const auto it = m_texture_slot_by_asset.find(handle); it != m_texture_slot_by_asset.end()) {
            m_gpu_textures[it->second].ref_count++;
            return it->second;
        }

        auto registry = GLT::asset::registry::get_ptr();
        if (!registry)
            return TEXTURE_SLOT_WHITE;

        const auto* asset = registry->data_as<GLT::asset::texture::texture_asset>(handle);
        if (!asset)
            return TEXTURE_SLOT_WHITE;

        texture_format_desc desc{};
        if (asset->format.kind != GLT::asset::texture::texture_kind::texture_2d
            || !describe_texture_format(asset->format.format, asset->format.space, desc)) {
            LOG(warn, "Unsupported texture in [{}]", registry->info(handle).name);
            return TEXTURE_SLOT_WHITE;
        }

        const u32 width = asset->width();
        const u32 height = asset->height();

        std::vector<GLT::asset::texture::mip_range> single;
        std::span<const GLT::asset::texture::mip_range> source = asset->mips;
        if (source.empty()) { single.push_back({0, asset->pixels.size(), width, height, 1}); source = single; }

        for (const auto& mip : source)
            if (mip.offset + mip.size > asset->pixels.size())
                return TEXTURE_SLOT_WHITE;

        std::vector<texture_upload_mip> mips;
        std::vector<std::byte> expanded;
        const std::byte* pixels = asset->pixels.data();
        u64 pixel_bytes = asset->pixels.size();

        if (desc.source_components == 3) {
            u64 total = 0;
            for (const auto& mip : source) total += static_cast<u64>(mip.width) * mip.height * desc.texel_bytes;
            expanded.resize(total);
            u64 off = 0;
            for (const auto& mip : source) {
                const u64 count = static_cast<u64>(mip.width) * mip.height;
                if (mip.offset + count * 3 * desc.component_bytes > asset->pixels.size())
                    return TEXTURE_SLOT_WHITE;
                expand_rgb_to_rgba(asset->pixels.data() + mip.offset, expanded.data() + off,
                    count, desc.component_bytes, desc.float_components);
                mips.push_back({off, mip.width, mip.height});
                off += count * desc.texel_bytes;
            }
            pixels = expanded.data();
            pixel_bytes = total;
        } else {
            for (const auto& mip : source) mips.push_back({mip.offset, mip.width, mip.height});
        }

        const bool generate_mips = source.size() == 1 && desc.block_bytes == 0 && asset->format.mip_levels <= 1;

        gpu_texture texture{};
        if (!create_gpu_texture(desc.format, width, height, pixels, pixel_bytes, mips, generate_mips, texture))
            return TEXTURE_SLOT_WHITE;

        u32 slot;
        if (!m_free_texture_slots.empty()) { slot = m_free_texture_slots.back(); m_free_texture_slots.pop_back(); }
        else {
            slot = static_cast<u32>(m_gpu_textures.size());
            if (slot >= MAX_MATERIALS * MATERIAL_TEXTURE_SLOTS) {
                destroy_gpu_texture(texture);
                return TEXTURE_SLOT_WHITE;
            }
            m_gpu_textures.emplace_back();
        }

        texture.asset = handle;
        texture.ref_count = 1;
        texture.alive = true;
        m_gpu_textures[slot] = texture;
        m_texture_slot_by_asset[handle] = slot;

        m_texture_table[slot].view = texture.view;
        m_texture_table[slot].sampler = nullptr;
        m_texture_table[slot].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        return slot;
    }


    void shared_resource_manager::release_texture_internal(u32 slot) {

        if (slot < FIRST_FREE_TEXTURE_SLOT || slot >= m_gpu_textures.size() || !m_gpu_textures[slot].alive)
            return;

        gpu_texture& t = m_gpu_textures[slot];
        if (--t.ref_count > 0)
            return;

        m_device.waitIdle();
        m_texture_slot_by_asset.erase(t.asset);
        destroy_gpu_texture(t);
        m_texture_table[slot] = m_texture_table[TEXTURE_SLOT_WHITE];
        m_free_texture_slots.push_back(slot);
    }

}

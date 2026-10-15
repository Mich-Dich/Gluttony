#pragma once

#include <asset/material.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // Must match [renderer_vert_attributes] in renderer_shading_strings.h and GLT::asset::mesh::vertex
    static constexpr vk::VertexInputBindingDescription vertex_binding = {
        /* binding   */ 0,
        /* stride    */ sizeof(GLT::asset::mesh::vertex),
        /* inputRate */ vk::VertexInputRate::eVertex,
    };

    static constexpr std::array<vk::VertexInputAttributeDescription, 4> vertex_attributes = {{
    // location, binding
        { 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, position)},
        { 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(GLT::asset::mesh::vertex, normal)},
        { 2, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(GLT::asset::mesh::vertex, tangent)},
        { 3, 0, vk::Format::eR32G32Sfloat, offsetof(GLT::asset::mesh::vertex, uv0)},
    }};

    constexpr const char*                       DEFAULT_MATERIAL_SLANG_SOURCE = R"slang(
import material_contract_v0;


struct material_params {
    float3  		base_color = float3(0.8, 0.8, 0.8);
    float   		roughness = 0.5;
};

surface_output surface_evaluate(surface_input s, material_params p, material_textures tex) {
    surface_output o = make_default_surface(s);
    o.base_color = p.base_color;
    o.roughness = p.roughness;
    return o;
}
)slang";

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct texture_format_desc {
        vk::Format  format = vk::Format::eUndefined;
        u32         texel_bytes = 0;            // uncompressed: bytes per texel as stored on the GPU
        u32         block_bytes = 0;            // block-compressed: bytes per 4x4 block
        u32         source_components = 0;     // 3 -> the source is RGB and gets expanded to RGBA (no portable RGB8 sampling)
        u32         component_bytes = 0;
        bool        float_components = false;
    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    bool describe_texture_format(GLT::asset::texture::pixel_format pixel_format, GLT::asset::texture::color_space space,
        texture_format_desc& out);


    void expand_rgb_to_rgba(const std::byte* src, std::byte* dst, u64 pixel_count, u32 component_bytes, bool is_float);


    void image_barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
        vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
        vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access, u32 base_mip, u32 mip_count);


    u32 fallback_texture_slot_for(u32 texture_slot_index);

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    bool describe_texture_format(GLT::asset::texture::pixel_format pixel_format, GLT::asset::texture::color_space space,
        texture_format_desc& out) {

        using PF = GLT::asset::texture::pixel_format;
        using VF = vk::Format;
        const bool srgb = space == GLT::asset::texture::color_space::srgb;

        switch (pixel_format) {
        case PF::u8_r:      out = { srgb ? VF::eR8Srgb : VF::eR8Unorm, 1 };                     return true;
        case PF::u8_rg:     out = { srgb ? VF::eR8G8Srgb : VF::eR8G8Unorm, 2 };                 return true;
        case PF::u8_rgb:    out = { srgb ? VF::eR8G8B8A8Srgb : VF::eR8G8B8A8Unorm, 4, 0, 3, 1 }; return true;
        case PF::u8_rgba:   out = { srgb ? VF::eR8G8B8A8Srgb : VF::eR8G8B8A8Unorm, 4 };         return true;
        case PF::u16_r:     out = { VF::eR16Unorm, 2 };                                         return true;
        case PF::u16_rg:    out = { VF::eR16G16Unorm, 4 };                                      return true;
        case PF::u16_rgb:   out = { VF::eR16G16B16A16Unorm, 8, 0, 3, 2 };                       return true;
        case PF::u16_rgba:  out = { VF::eR16G16B16A16Unorm, 8 };                                return true;
        case PF::f32_r:     out = { VF::eR32Sfloat, 4, 0, 0, 0, true };                         return true;
        case PF::f32_rg:    out = { VF::eR32G32Sfloat, 8, 0, 0, 0, true };                      return true;
        case PF::f32_rgb:   out = { VF::eR32G32B32A32Sfloat, 16, 0, 3, 4, true };               return true;
        case PF::f32_rgba:  out = { VF::eR32G32B32A32Sfloat, 16, 0, 0, 0, true };               return true;
        case PF::bc1:       out = { srgb ? VF::eBc1RgbaSrgbBlock : VF::eBc1RgbaUnormBlock, 0, 8 };  return true;
        case PF::bc3:       out = { srgb ? VF::eBc3SrgbBlock : VF::eBc3UnormBlock, 0, 16 };         return true;
        case PF::bc4:       out = { VF::eBc4UnormBlock, 0, 8 };                                     return true;
        case PF::bc5:       out = { VF::eBc5UnormBlock, 0, 16 };                                    return true;
        case PF::bc6h:      out = { VF::eBc6HUfloatBlock, 0, 16 };                                  return true;
        case PF::bc7:       out = { srgb ? VF::eBc7SrgbBlock : VF::eBc7UnormBlock, 0, 16 };         return true;
        default:            return false;   // etc1/etc2/astc: not sampleable on desktop GPUs
        }
    }


    void expand_rgb_to_rgba(const std::byte* src, std::byte* dst, u64 pixel_count, u32 component_bytes, bool is_float) {

        std::byte alpha[4]{};
        if (is_float) {
            const f32 one = 1.0f;
            std::memcpy(alpha, &one, sizeof(one));
        } else {
            for (u32 index = 0; index < component_bytes; ++index)
                alpha[index] = std::byte{ 0xFF };
        }

        for (u64 index = 0; index < pixel_count; ++index) {
            std::memcpy(dst + index * 4 * component_bytes, src + index * 3 * component_bytes, 3 * component_bytes);
            std::memcpy(dst + index * 4 * component_bytes + 3 * component_bytes, alpha, component_bytes);
        }
    }


    void image_barrier(vk::CommandBuffer cmd, vk::Image image, vk::ImageLayout from, vk::ImageLayout to,
        vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access,
        vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access, u32 base_mip, u32 mip_count) {

        vk::ImageMemoryBarrier2 barrier{};
        barrier.srcStageMask = src_stage;
        barrier.srcAccessMask = src_access;
        barrier.dstStageMask = dst_stage;
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, base_mip, mip_count, 0, 1);

        vk::DependencyInfo dependency{};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        cmd.pipelineBarrier2(dependency);
    }


    u32 fallback_texture_slot_for(u32 texture_slot_index) {

        return texture_slot_index == static_cast<u32>(GLT::asset::material::texture_slot::normal)
            ? TEXTURE_SLOT_FLAT_NORMAL : TEXTURE_SLOT_WHITE;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // textures --------------------------------------------------------------------------------------------------------

    bool renderer::create_gpu_texture(vk::Format format, u32 width, u32 height, const std::byte* pixels, u64 pixel_bytes,
        std::span<const texture_upload_mip> mips, bool generate_mips, gpu_texture& out) {

        using Stage = vk::PipelineStageFlagBits2;
        using Access = vk::AccessFlagBits2;

        const vk::FormatProperties props = m_physical_device.getFormatProperties(format);
        const auto sampled = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eSampledImageFilterLinear;
        if ((props.optimalTilingFeatures & sampled) != sampled) {
            LOG(warn, "Texture format {} is not sampleable on this device", vk::to_string(format));
            return false;
        }

        const auto blit_features = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
        generate_mips = generate_mips && mips.size() == 1 && std::max(width, height) > 1
            && (props.optimalTilingFeatures & blit_features) == blit_features;

        const u32 mip_count = generate_mips ? static_cast<u32>(std::bit_width(std::max(width, height))) : static_cast<u32>(mips.size());

        vk::ImageCreateInfo image_info{};
        image_info.imageType = vk::ImageType::e2D;
        image_info.format = format;
        image_info.extent = vk::Extent3D(width, height, 1);
        image_info.mipLevels = mip_count;
        image_info.arrayLayers = 1;
        image_info.samples = vk::SampleCountFlagBits::e1;
        image_info.tiling = vk::ImageTiling::eOptimal;
        image_info.usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst
                         | (generate_mips ? vk::ImageUsageFlagBits::eTransferSrc : vk::ImageUsageFlags{});
        image_info.sharingMode = vk::SharingMode::eExclusive;
        image_info.initialLayout = vk::ImageLayout::eUndefined;

        out.image = m_dev->create_image(image_info, 0);

        vk::ImageViewCreateInfo view_info{};
        view_info.image = out.image.image;
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = format;
        view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, mip_count, 0, 1);
        out.view = m_device.createImageView(view_info);

        util::allocated_buffer staging = m_dev->create_buffer(pixel_bytes, vk::BufferUsageFlagBits::eTransferSrc,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        std::memcpy(m_dev->map_buffer(staging), pixels, pixel_bytes);
        m_dev->unmap_buffer(staging);

        const vk::Image image = out.image.image;

        immediate_submit([&](VkCommandBuffer raw_cmd) {

            vk::CommandBuffer cmd(raw_cmd);

            image_barrier(cmd, image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                Stage::eNone, {}, Stage::eAllTransfer, Access::eTransferWrite, 0, mip_count);

            std::vector<vk::BufferImageCopy> regions;
            regions.reserve(mips.size());
            for (u32 level = 0; level < mips.size(); ++level) {
                vk::BufferImageCopy region{};
                region.bufferOffset = mips[level].offset;
                region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, 1);
                region.imageExtent = vk::Extent3D(mips[level].width, mips[level].height, 1);
                regions.push_back(region);
            }
            cmd.copyBufferToImage(staging.buffer, image, vk::ImageLayout::eTransferDstOptimal, regions);

            if (generate_mips) {

                i32 mip_width = static_cast<i32>(width);
                i32 mip_height = static_cast<i32>(height);

                for (u32 level = 1; level < mip_count; ++level) {

                    image_barrier(cmd, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                        Stage::eAllTransfer, Access::eTransferWrite, Stage::eAllTransfer, Access::eTransferRead, level - 1, 1);

                    const i32 next_width = std::max(mip_width / 2, 1);
                    const i32 next_height = std::max(mip_height / 2, 1);

                    vk::ImageBlit blit{};
                    blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level - 1, 0, 1);
                    blit.srcOffsets[1] = vk::Offset3D(mip_width, mip_height, 1);
                    blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0, 1);
                    blit.dstOffsets[1] = vk::Offset3D(next_width, next_height, 1);
                    cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal,
                        blit, vk::Filter::eLinear);

                    mip_width = next_width;
                    mip_height = next_height;
                }

                // Levels [0, n-2] are TRANSFER_SRC, the last one is still TRANSFER_DST
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

        m_dev->destroy_buffer(staging);     // immediate_submit has completed
        return true;
    }


    void renderer::destroy_gpu_texture(gpu_texture& texture) {

        if (texture.view)
            m_device.destroyImageView(texture.view);
        if (texture.image.image)
            m_dev->destroy_image(texture.image);
        texture = {};
    }


    u32 renderer::acquire_texture(GLT::asset::handle handle) {

        if (handle == INVALID_HANDLE)
            return TEXTURE_SLOT_WHITE;

        if (const auto it = m_texture_slot_by_asset.find(handle); it != m_texture_slot_by_asset.end()) {
            m_gpu_textures[it->second].ref_count++;
            return it->second;
        }

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return TEXTURE_SLOT_WHITE;

        // NOTE: data_as<> does not check the asset type. Add a type check here if the registry exposes one
        const auto* asset = registry->data_as<GLT::asset::texture::texture_asset>(handle);
        VALIDATE(asset, return TEXTURE_SLOT_WHITE, "", "Texture asset is not loaded");

        texture_format_desc desc{};
        if (asset->format.kind != GLT::asset::texture::texture_kind::texture_2d
            || !describe_texture_format(asset->format.format, asset->format.space, desc)) {
            LOG(warn, "Unsupported texture format/kind in [{}]", registry->info(handle).name);
            return TEXTURE_SLOT_WHITE;
        }

        const u32 width = asset->width();
        const u32 height = asset->height();

        // Source mips. A texture without a mip table is a single level covering the whole blob
        std::vector<GLT::asset::texture::mip_range> single_level;
        std::span<const GLT::asset::texture::mip_range> source_mips = asset->mips;
        if (source_mips.empty()) {
            single_level.push_back({ 0, asset->pixels.size(), width, height, 1 });
            source_mips = single_level;
        }

        for (const auto& mip : source_mips)
            VALIDATE(mip.offset + mip.size <= asset->pixels.size(), return TEXTURE_SLOT_WHITE, "", "Texture mip range out of bounds");

        std::vector<texture_upload_mip> mips;
        std::vector<std::byte> expanded;
        const std::byte* pixels = asset->pixels.data();
        u64 pixel_bytes = asset->pixels.size();

        if (desc.source_components == 3) {

            u64 expanded_bytes = 0;
            for (const auto& mip : source_mips)
                expanded_bytes += static_cast<u64>(mip.width) * mip.height * desc.texel_bytes;
            expanded.resize(expanded_bytes);

            u64 dst_offset = 0;
            for (const auto& mip : source_mips) {
                const u64 pixel_count = static_cast<u64>(mip.width) * mip.height;
                VALIDATE(mip.offset + pixel_count * 3 * desc.component_bytes <= asset->pixels.size(),
                    return TEXTURE_SLOT_WHITE, "", "Texture mip range out of bounds");
                expand_rgb_to_rgba(asset->pixels.data() + mip.offset, expanded.data() + dst_offset, pixel_count,
                    desc.component_bytes, desc.float_components);
                mips.push_back({ dst_offset, mip.width, mip.height });
                dst_offset += pixel_count * desc.texel_bytes;
            }
            pixels = expanded.data();
            pixel_bytes = expanded_bytes;

        } else {
            for (const auto& mip : source_mips)
                mips.push_back({ mip.offset, mip.width, mip.height });
        }

        const bool generate_mips = source_mips.size() == 1 && desc.block_bytes == 0 && asset->format.mip_levels <= 1;

        gpu_texture texture{};
        if (!create_gpu_texture(desc.format, width, height, pixels, pixel_bytes, mips, generate_mips, texture))
            return TEXTURE_SLOT_WHITE;

        u32 slot;
        if (!m_free_texture_slots.empty()) {
            slot = m_free_texture_slots.back();
            m_free_texture_slots.pop_back();
        } else {
            slot = static_cast<u32>(m_gpu_textures.size());
            if (slot >= MAX_MATERIALS * MATERIAL_TEXTURE_SLOTS) {
                LOG(warn, "Bindless texture table full ({} textures)", MAX_MATERIALS * MATERIAL_TEXTURE_SLOTS);
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


    void renderer::release_texture(u32 slot) {

        if (slot < FIRST_FREE_TEXTURE_SLOT || slot >= m_gpu_textures.size() || !m_gpu_textures[slot].alive)
            return;

        gpu_texture& texture = m_gpu_textures[slot];
        if (--texture.ref_count > 0)
            return;

        m_device.waitIdle();                // rare path: frames in flight may still reference it

        m_texture_slot_by_asset.erase(texture.asset);
        destroy_gpu_texture(texture);

        m_texture_table[slot] = m_texture_table[TEXTURE_SLOT_WHITE];   // freed slots hold the white fallback
        m_free_texture_slots.push_back(slot);
    }

    // materials -------------------------------------------------------------------------------------------------------

    u32 renderer::acquire_material(GLT::asset::handle handle) {

        if (handle == INVALID_HANDLE)
            return DEFAULT_MATERIAL_SLOT;

        if (const auto it = m_material_slot_by_asset.find(handle); it != m_material_slot_by_asset.end()) {
            m_material_slots[it->second].ref_count++;
            return it->second;
        }

        u32 slot;
        if (!m_free_material_slots.empty()) {
            slot = m_free_material_slots.back();
            m_free_material_slots.pop_back();
        } else {
            slot = static_cast<u32>(m_material_slots.size());
            if (slot >= MAX_MATERIALS) {
                LOG(warn, "Material table full ({} materials), using the default material", MAX_MATERIALS);
                return DEFAULT_MATERIAL_SLOT;
            }
            m_material_slots.emplace_back();
        }

        material_slot& entry = m_material_slots[slot];
        entry = {};
        entry.asset = handle;
        entry.ref_count = 1;
        entry.alive = true;
        entry.texture_assets.fill(INVALID_HANDLE);
        for (u32 index = 0; index < MATERIAL_TEXTURE_SLOTS; ++index)
            entry.texture_slots[index] = fallback_texture_slot_for(index);

        m_material_slot_by_asset[handle] = slot;
        pack_material(slot);

        // release the slot we just claimed and hand back the fallback instead
        // Used on every failure path below so we never hand out a slot with no pipeline
        const auto roll_back = [&]() -> u32 {

            m_material_slots[slot] = {};
            m_material_slot_by_asset.erase(handle);
            m_free_material_slots.push_back(slot);
            return DEFAULT_MATERIAL_SLOT;
        };

        auto registry = GLT::asset::registry::get_ref();
        if (!registry) {
            LOG(warn, "Material registry unavailable, using fallback");
            return roll_back();
        }

        const auto* asset = registry->data_as<GLT::asset::material::material_asset>(handle);
        if (!asset) {
            LOG(warn, "Material [{}] not loaded, using fallback", (u64)handle);
            return roll_back();
        }

        if (asset->bytecode.empty()) {
            LOG(warn, "Material [{}] has no compiled bytecode, using fallback", registry->info(handle).name);
            return roll_back();
        }

        // load and split the bytecode blob from the material asset
        std::vector<std::byte> vs_bytes, fs_bytes, ds_bytes;
        {
            const auto* blob = reinterpret_cast<const u64*>(asset->bytecode.data());
            u64 vertex_source_size = blob[0];
            u64 fragment_source_size = blob[1];
            u64 depth_source_size = blob[2];
            const std::byte* base = asset->bytecode.data() + 3 * sizeof(u64);
            vs_bytes.assign(base, base + vertex_source_size);
            fs_bytes.assign(base + vertex_source_size, base + vertex_source_size + fragment_source_size);
            ds_bytes.assign(base + vertex_source_size + fragment_source_size,
                base + vertex_source_size + fragment_source_size + depth_source_size);
        }

        entry.blend = asset->blend;

        if (!vs_bytes.empty()) {

            entry.vertex_module = m_dev->create_shader_module(std::span<const std::byte>(vs_bytes));
            entry.fragment_module = m_dev->create_shader_module(std::span<const std::byte>(fs_bytes));
            entry.depth_module = m_dev->create_shader_module(std::span<const std::byte>(ds_bytes));

            entry.pipeline = create_material_pipeline(entry, material_pipeline_kind::main);
            entry.depth_pipeline = create_material_pipeline(entry, material_pipeline_kind::depth);
            m_live_pipeline_count += 2;
        }

        return slot;
    }


    void renderer::release_material(u32 slot) {

        if (slot == DEFAULT_MATERIAL_SLOT || slot >= m_material_slots.size() || !m_material_slots[slot].alive)
            return;

        material_slot& entry = m_material_slots[slot];
        if (--entry.ref_count > 0)
            return;

        if (entry.pipeline)
            m_device.destroyPipeline(entry.pipeline);
        if (entry.depth_pipeline)
            m_device.destroyPipeline(entry.depth_pipeline);
        if (entry.vertex_module)
            m_device.destroyShaderModule(entry.vertex_module);
        if (entry.fragment_module)
            m_device.destroyShaderModule(entry.fragment_module);
        if (entry.depth_module)
            m_device.destroyShaderModule(entry.depth_module);

        // reflection-driven params storage
        if (entry.params_ubo.buffer) {
            m_dev->destroy_buffer(entry.params_ubo);
            entry.params_ubo = {};
            entry.params_size = 0;
        }

        for (u32 index = 0; index < MATERIAL_TEXTURE_SLOTS; ++index)
            if (entry.texture_assets[index] != INVALID_HANDLE)
                release_texture(entry.texture_slots[index]);

        m_material_slot_by_asset.erase(entry.asset);
        entry = {};
        m_free_material_slots.push_back(slot);
    }


    void renderer::pack_material(u32 slot_index) {

        material_slot& slot = m_material_slots[slot_index];
        if (slot.asset == INVALID_HANDLE || !slot.alive)
            return;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        const auto* asset = registry->data_as<GLT::asset::material::material_asset>(slot.asset);
        if (!asset)
            return;

        // refresh texture slots (unchanged from Phase 4)
        for (u32 index = 0; index < MATERIAL_TEXTURE_SLOTS; index++) {

            const auto wanted = asset->textures[index];
            if (wanted == slot.texture_assets[index])
                continue;

            if (slot.texture_assets[index] != INVALID_HANDLE)
                release_texture(slot.texture_slots[index]);

            slot.texture_assets[index] = wanted;
            slot.texture_slots[index] = (wanted == INVALID_HANDLE) ? fallback_texture_slot_for(index) : acquire_texture(wanted);
        }

        // refresh params UBO
        const u32 needed = static_cast<u32>(asset->packed_params.size());
        if (!slot.params_ubo.buffer || slot.params_size < needed) {

            if (slot.params_ubo.buffer)
                m_dev->destroy_buffer(slot.params_ubo);

            const u32 alloc_size = std::max<u32>(needed, 256);
            slot.params_ubo = m_dev->create_buffer(alloc_size, vk::BufferUsageFlagBits::eUniformBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            slot.params_size = alloc_size;
            m_live_buffer_count++;
        }
        if (needed > 0) {

            void* p = m_dev->map_buffer(slot.params_ubo);
            std::memcpy(p, asset->packed_params.data(), needed);
            m_dev->unmap_buffer(slot.params_ubo);
        }

        write_material_descriptors(slot_index);
    }


    void renderer::refresh_material(GLT::asset::handle handle) {

        if (const auto it = m_material_slot_by_asset.find(handle); it != m_material_slot_by_asset.end())
            pack_material(it->second);
    }


    // Catches edits made to loaded material assets without the editor having to call refresh_material()
    void renderer::poll_material_changes() {

        for (u32 slot = 1; slot < m_material_slots.size(); ++slot)
            if (m_material_slots[slot].alive)
                pack_material(slot);
    }


    void renderer::update_materials() {

        if ((++m_material_poll_counter % MATERIAL_POLL_INTERVAL) == 0)
            poll_material_changes();
    }


    void renderer::write_material_descriptors(u32 slot_index) {

        material_slot& slot = m_material_slots[slot_index];

        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; frame_index++) {

            std::array<util::accessible_image, 6> images{};
            for (u32 t = 0; t < 6; ++t) {
                images[t] = m_texture_table[slot.texture_slots[t]];
                images[t].sampler = nullptr;
            }

            std::vector<util::descriptor_item> items;
            items.emplace_back(0, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 1, &slot.params_ubo);
            for (u32 t = 0; t < 6; ++t)
                items.emplace_back(1 + t, vk::DescriptorType::eSampledImage, vk::ShaderStageFlagBits::eFragment, 1, &images[t]);

            util::accessible_image sampler_image{};
            sampler_image.sampler = m_material_sampler;
            items.emplace_back(7, vk::DescriptorType::eSampler, vk::ShaderStageFlagBits::eFragment, 1, &sampler_image);

            m_dev->compute_descriptor_binding_offsets(m_material_set_layout, items);
            m_dev->update_descriptor_buffer(m_material_desc_buffers[frame_index], items,
                util::descriptor_buffer_type::combined, slot_index);
        }
    }

    // resources -------------------------------------------------------------------------------------------------------

    void renderer::create_material_resources() {

        using VKDT = vk::DescriptorType;
        using VKSF = vk::ShaderStageFlagBits;

        // Sampler (set 0, binding 8). One state for now: trilinear, anisotropic, repeat
        const f32 max_anisotropy = m_physical_device.getProperties().limits.maxSamplerAnisotropy;
        vk::SamplerCreateInfo sampler_info{};
        sampler_info.magFilter = vk::Filter::eLinear;
        sampler_info.minFilter = vk::Filter::eLinear;
        sampler_info.mipmapMode = vk::SamplerMipmapMode::eLinear;
        sampler_info.addressModeU = vk::SamplerAddressMode::eRepeat;
        sampler_info.addressModeV = vk::SamplerAddressMode::eRepeat;
        sampler_info.addressModeW = vk::SamplerAddressMode::eRepeat;
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = std::min(8.0f, max_anisotropy);
        sampler_info.minLod = 0.0f;
        sampler_info.maxLod = 1000.0f;
        m_material_sampler = m_device.createSampler(sampler_info);

        // Set 1 layout: reflection UBO + 6 textures + sampler
        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eFragment, 1, nullptr);
        layout_items.emplace_back(1, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);   // base_color
        layout_items.emplace_back(2, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);   // metallic_roughness
        layout_items.emplace_back(3, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);   // normal
        layout_items.emplace_back(4, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);   // emissive
        layout_items.emplace_back(5, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);   // occlusion
        layout_items.emplace_back(6, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);   // height
        layout_items.emplace_back(7, VKDT::eSampler, VKSF::eFragment, 1, nullptr);
        m_material_set_layout = m_dev->create_descriptor_set_layout(layout_items);
        m_live_descriptor_set_count++;

        // CPU tables ---------------------------------------------------------------------------------------------------

        material_slot default_slot{};
        default_slot.ref_count = 1;
        default_slot.alive = true;
        default_slot.asset = INVALID_HANDLE;
        default_slot.texture_assets.fill(INVALID_HANDLE);
        for (u32 index = 0; index < MATERIAL_TEXTURE_SLOTS; ++index)
            default_slot.texture_slots[index] = fallback_texture_slot_for(index);
        m_material_slots.assign(1, default_slot);

        // Fallback textures --------------------------------------------------------------------------------------------
        m_gpu_textures.assign(FIRST_FREE_TEXTURE_SLOT, gpu_texture{});
        m_texture_table.assign(MAX_MATERIALS * MATERIAL_TEXTURE_SLOTS, util::accessible_image{});

        const auto create_fallback = [&](u32 slot, std::array<std::byte, 4> rgba) {
            const texture_upload_mip mip{ 0, 1, 1 };
            gpu_texture texture{};
            const bool ok = create_gpu_texture(vk::Format::eR8G8B8A8Unorm, 1, 1, rgba.data(), rgba.size(), std::span(&mip, 1), false, texture);
            ASSERT(ok, "", "Failed to create fallback texture");
            texture.ref_count = 1;
            texture.alive = true;
            m_gpu_textures[slot] = texture;
            m_texture_table[slot].view = texture.view;
            m_texture_table[slot].layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        };
        create_fallback(TEXTURE_SLOT_WHITE, { std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255} });
        create_fallback(TEXTURE_SLOT_FLAT_NORMAL, { std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255} });

        for (u32 index = FIRST_FREE_TEXTURE_SLOT; index < MAX_MATERIALS * MATERIAL_TEXTURE_SLOTS; index++)    // every unused slot holds the white texture
            m_texture_table[index] = m_texture_table[TEXTURE_SLOT_WHITE];

        // Per-frame buffers + descriptor sets ---------------------------------------------------------------------------
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {
            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eUniformBuffer, VKSF::eFragment, 1, nullptr);  // p_resources set per-slice
            items.emplace_back(1, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
            items.emplace_back(2, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
            items.emplace_back(3, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
            items.emplace_back(4, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
            items.emplace_back(5, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
            items.emplace_back(6, VKDT::eSampledImage, VKSF::eFragment, 1, nullptr);
            items.emplace_back(7, VKDT::eSampler, VKSF::eFragment, 1, nullptr);

            m_material_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_material_set_layout, items, 
                util::descriptor_buffer_type::combined, MAX_MATERIALS);
            m_live_buffer_count++;
        }
    }


    void renderer::destroy_material_resources() {

        m_device.waitIdle();

        for (auto& desc : m_material_desc_buffers) {
            if (desc.buffer.buffer)
                m_dev->destroy_buffer(desc.buffer);
            desc = {};
        }

        for (auto& texture : m_gpu_textures)
            if (texture.alive)
                destroy_gpu_texture(texture);

        for (auto& slot : m_material_slots) {

            if (slot.pipeline)
                m_device.destroyPipeline(slot.pipeline);
            if (slot.depth_pipeline)
                m_device.destroyPipeline(slot.depth_pipeline);
            if (slot.vertex_module)
                m_device.destroyShaderModule(slot.vertex_module);
            if (slot.fragment_module)
                m_device.destroyShaderModule(slot.fragment_module);
            if (slot.depth_module)
                m_device.destroyShaderModule(slot.depth_module);
            if (slot.params_ubo.buffer)
                m_dev->destroy_buffer(slot.params_ubo);
        }

        m_gpu_textures.clear();
        m_free_texture_slots.clear();
        m_texture_slot_by_asset.clear();
        m_texture_table.clear();
        m_material_slots.clear();
        m_free_material_slots.clear();
        m_material_slot_by_asset.clear();

        if (m_material_sampler) {
            m_device.destroySampler(m_material_sampler);
            m_material_sampler = nullptr;
        }
        if (m_material_set_layout) {
            m_device.destroyDescriptorSetLayout(m_material_set_layout);
            m_material_set_layout = nullptr;
        }
    }


    vk::Pipeline renderer::create_material_pipeline(const material_slot& slot, material_pipeline_kind kind) {

        vk::PipelineShaderStageCreateInfo stages[2]{};
        stages[0].stage = vk::ShaderStageFlagBits::eVertex;
        stages[0].module = slot.vertex_module;
        stages[0].pName = "main";
        stages[1].stage = vk::ShaderStageFlagBits::eFragment;
        stages[1].module = (kind == material_pipeline_kind::main) ? slot.fragment_module : slot.depth_module;
        stages[1].pName  = "main";

        vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        vertex_input_state.vertexBindingDescriptionCount = 1;
        vertex_input_state.pVertexBindingDescriptions = &vertex_binding;
        vertex_input_state.vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes.size());
        vertex_input_state.pVertexAttributeDescriptions = vertex_attributes.data();

        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{};
        input_assembly_state.topology = vk::PrimitiveTopology::eTriangleList;

        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        vk::PipelineRasterizationStateCreateInfo rasterization_state{};
        rasterization_state.polygonMode = vk::PolygonMode::eFill;
        rasterization_state.cullMode = vk::CullModeFlagBits::eBack;
        rasterization_state.frontFace = vk::FrontFace::eCounterClockwise;
        rasterization_state.lineWidth = 1.0f;

        vk::PipelineMultisampleStateCreateInfo multisample_state{};
        multisample_state.rasterizationSamples = vk::SampleCountFlagBits::e1;

        vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{};
        depth_stencil_state.depthTestEnable = VK_TRUE;
        depth_stencil_state.depthWriteEnable = (kind == material_pipeline_kind::depth);
        depth_stencil_state.depthCompareOp = (kind == material_pipeline_kind::main) ? vk::CompareOp::eEqual : vk::CompareOp::eLess;
        depth_stencil_state.depthBoundsTestEnable = VK_FALSE;
        depth_stencil_state.stencilTestEnable = VK_FALSE;

        vk::PipelineColorBlendAttachmentState cb_attachment{};
        cb_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | 
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA;

        vk::PipelineColorBlendStateCreateInfo color_blend_state{};
        if (kind == material_pipeline_kind::main) {
            color_blend_state.attachmentCount = 1;
            color_blend_state.pAttachments = &cb_attachment;
        } else {
            color_blend_state.attachmentCount = 0;
            color_blend_state.pAttachments = nullptr;
        }

        vk::DynamicState dynamic_states[] = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
        vk::PipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.dynamicStateCount = 2;
        dynamic_state.pDynamicStates = dynamic_states;

        vk::PipelineRenderingCreateInfo ri{};
        const vk::Format color_format = vk::Format::eR8G8B8A8Unorm;
        const vk::Format depth_format = vk::Format::eD32Sfloat;
        if (kind == material_pipeline_kind::main) {
            ri.colorAttachmentCount = 1;
            ri.pColorAttachmentFormats = &color_format;
        } else {
            ri.colorAttachmentCount = 0;
            ri.pColorAttachmentFormats = nullptr;
        }
        ri.depthAttachmentFormat = depth_format;

        vk::GraphicsPipelineCreateInfo info{};
        info.pNext = &ri;
        // info.flags = vk::PipelineCreateFlagBits::eDescriptorBufferEXT;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertex_input_state;
        info.pInputAssemblyState = &input_assembly_state;
        info.pViewportState = &viewport_state;
        info.pRasterizationState = &rasterization_state;
        info.pMultisampleState = &multisample_state;
        info.pDepthStencilState = &depth_stencil_state;
        info.pColorBlendState = &color_blend_state;
        info.pDynamicState = &dynamic_state;
        info.layout = m_mesh_pipeline_layout;
        info.renderPass = nullptr;
        info.subpass = 0;

        auto [result, pipeline] = m_device.createGraphicsPipeline(nullptr, info);
        VALIDATE(result == vk::Result::eSuccess, return nullptr, "", "material pipeline creation failed")
        return pipeline;
    }


    void renderer::install_fallback_material() {

        // Compile the default material of the renderer and save to DEFAULT_MATERIAL_SLOT. [acquire_material()] hands this slot
        // out for any submesh whose material is INVALID_HANDLE, or whose material asset failed to load
        auto compiled = compile(GLT::asset::material::DEFAULT_SLANG_SOURCE, GLT::asset::material::blend_mode::opaque);
        ASSERT(compiled, "", "Failed to compile default material: {}", (compiled.error().message))     // MUST EXIST or meshes will not be drawn

        material_slot& fallback = m_material_slots[DEFAULT_MATERIAL_SLOT];

        // Split the packed bytecode blob
        std::vector<std::byte> vs_bytes, fs_bytes, ds_bytes;
        {
            ASSERT(compiled->bytecode.size() >= 3 * sizeof(u64), "", "Fallback bytecode blob too small");
            const u64 vertex_source_size = GLT::util::get_bytes<u64>(compiled->bytecode, 0);
            const u64 fragment_source_size = GLT::util::get_bytes<u64>(compiled->bytecode, sizeof(u64));
            const u64 depth_source_size = GLT::util::get_bytes<u64>(compiled->bytecode, 2 * sizeof(u64));
            ASSERT(3 * sizeof(u64) + vertex_source_size + fragment_source_size + depth_source_size <= compiled->bytecode.size(),
                "", "Fallback bytecode sizes inconsistent")

            const std::byte* base = compiled->bytecode.data() + 3 * sizeof(u64);
            vs_bytes.assign(base, base + vertex_source_size);
            fs_bytes.assign(base + vertex_source_size, base + vertex_source_size + fragment_source_size);
            if (depth_source_size > 0)
                ds_bytes.assign(base + vertex_source_size + fragment_source_size,
                    base + vertex_source_size + fragment_source_size + depth_source_size);
        }

        fallback.vertex_module = m_dev->create_shader_module(std::span<const std::byte>(vs_bytes));
        fallback.fragment_module = m_dev->create_shader_module(std::span<const std::byte>(fs_bytes));
        if (!ds_bytes.empty())
            fallback.depth_module = m_dev->create_shader_module(std::span<const std::byte>(ds_bytes));

        // Default parameter UBO
        const u32 params_bytes = static_cast<u32>(compiled->default_params.size());
        const u32 alloc_size = std::max<u32>(params_bytes, 256);

        fallback.params_ubo = m_dev->create_buffer(alloc_size, vk::BufferUsageFlagBits::eUniformBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        fallback.params_size = alloc_size;
        m_live_buffer_count++;

        if (params_bytes > 0) {
            std::memcpy(m_dev->map_buffer(fallback.params_ubo), compiled->default_params.data(), params_bytes);
            m_dev->unmap_buffer(fallback.params_ubo);
        }

        fallback.blend = GLT::asset::material::blend_mode::opaque;
        fallback.pipeline = create_material_pipeline(fallback, material_pipeline_kind::main);
        fallback.depth_pipeline = create_material_pipeline(fallback, material_pipeline_kind::depth);

        if (fallback.pipeline)
            m_live_pipeline_count++;
        if (fallback.depth_pipeline)
            m_live_pipeline_count++;

        write_material_descriptors(DEFAULT_MATERIAL_SLOT);
    }

}

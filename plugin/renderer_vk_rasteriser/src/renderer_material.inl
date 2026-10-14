#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

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
            for (u32 i = 0; i < component_bytes; ++i)
                alpha[i] = std::byte{ 0xFF };
        }

        for (u64 i = 0; i < pixel_count; ++i) {
            std::memcpy(dst + i * 4 * component_bytes, src + i * 3 * component_bytes, 3 * component_bytes);
            std::memcpy(dst + i * 4 * component_bytes + 3 * component_bytes, alpha, component_bytes);
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
            if (slot >= MAX_TEXTURES) {
                LOG(warn, "Bindless texture table full ({} textures)", MAX_TEXTURES);
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

        m_texture_high_water = std::max(m_texture_high_water, slot + 1);
        m_texture_version++;
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
        m_texture_version++;
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
        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i)
            entry.texture_slots[i] = fallback_texture_slot_for(i);

        m_material_slot_by_asset[handle] = slot;
        m_material_cpu[slot] = m_material_cpu[DEFAULT_MATERIAL_SLOT];
        pack_material(slot);
        m_material_version++;
        return slot;
    }


    void renderer::release_material(u32 slot) {

        if (slot == DEFAULT_MATERIAL_SLOT || slot >= m_material_slots.size() || !m_material_slots[slot].alive)
            return;

        material_slot& entry = m_material_slots[slot];
        if (--entry.ref_count > 0)
            return;

        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i)
            if (entry.texture_assets[i] != INVALID_HANDLE)
                release_texture(entry.texture_slots[i]);

        m_material_slot_by_asset.erase(entry.asset);
        entry = {};
        m_material_cpu[slot] = m_material_cpu[DEFAULT_MATERIAL_SLOT];   // stale references render with the default material
        m_free_material_slots.push_back(slot);
        m_material_version++;
    }


    void renderer::pack_material(u32 slot_index) {

        material_slot& slot = m_material_slots[slot_index];

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return;

        const auto* asset = registry->data_as<GLT::asset::material::material_asset>(slot.asset);
        if (!asset)
            return;                         // the asset went away: keep the last packed values

        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i) {

            const GLT::asset::handle wanted = asset->textures[i];
            if (wanted == slot.texture_assets[i])
                continue;

            if (slot.texture_assets[i] != INVALID_HANDLE)
                release_texture(slot.texture_slots[i]);

            slot.texture_assets[i] = wanted;
            slot.texture_slots[i] = (wanted == INVALID_HANDLE) ? fallback_texture_slot_for(i) : acquire_texture(wanted);
        }

        using TS = GLT::asset::material::texture_slot;
        const auto tex = [&](TS s) { return slot.texture_slots[static_cast<u32>(s)]; };

        gpu_material packed{};
        packed.base_color ={};
        packed.emissive_roughness = glm::vec4(0.f);
        packed.metallic_reflectance = glm::vec4(0.f);
        packed.textures_a = glm::uvec4(tex(TS::base_color), tex(TS::metallic_roughness), tex(TS::normal), tex(TS::emissive));
        packed.textures_b = glm::uvec4(tex(TS::occlusion), tex(TS::height), 0,
            (0 & MATERIAL_TEMPLATE_MASK) >> MATERIAL_TEMPLATE_SHIFT);

        if (std::memcmp(&m_material_cpu[slot_index], &packed, sizeof(gpu_material)) != 0) {
            m_material_cpu[slot_index] = packed;
            m_material_version++;
        }
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

        const u32 frame_index = m_current_frame;

        // The fence wait at the top of begin_frame() guarantees this frame slot's buffers are no longer in use
        if (m_material_uploaded_version[frame_index] != m_material_version) {

            std::memcpy(m_dev->map_buffer(m_material_buffers[frame_index]), m_material_cpu.data(),
                m_material_slots.size() * sizeof(gpu_material));
            m_dev->unmap_buffer(m_material_buffers[frame_index]);
            m_material_uploaded_version[frame_index] = m_material_version;
        }

        if (m_texture_uploaded_version[frame_index] != m_texture_version) {

            std::vector<util::descriptor_item> items;
            items.emplace_back(0, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1, &m_material_buffers[frame_index]);
            items.emplace_back(1, vk::DescriptorType::eSampledImage, vk::ShaderStageFlagBits::eFragment, MAX_TEXTURES, m_texture_table.data());
            m_dev->compute_descriptor_binding_offsets(m_material_set_layout, items);

            void* mapped = m_dev->map_buffer(m_material_desc_buffers[frame_index].buffer);
            for (u32 i = 0; i < m_texture_high_water; ++i)
                m_dev->update_descriptor_buffer(m_material_desc_buffers[frame_index], items[1], i,
                    util::descriptor_buffer_type::resource, 0, mapped);
            m_dev->unmap_buffer(m_material_desc_buffers[frame_index].buffer);

            m_texture_uploaded_version[frame_index] = m_texture_version;
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

        // Set 1 layout: material SSBO + bindless sampled images
        std::vector<util::descriptor_item> layout_items;
        layout_items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eFragment, 1, nullptr);
        layout_items.emplace_back(1, VKDT::eSampledImage, VKSF::eFragment, MAX_TEXTURES, nullptr);
        m_material_set_layout = m_dev->create_descriptor_set_layout(layout_items);
        m_live_descriptor_set_count++;

        // CPU tables ---------------------------------------------------------------------------------------------------
        gpu_material default_material{};
        default_material.base_color = glm::vec4(0.75f, 0.72f, 0.68f, 1.0f);
        default_material.emissive_roughness = glm::vec4(0.0f, 0.0f, 0.0f, 0.6f);
        default_material.metallic_reflectance = glm::vec4(0.0f, 0.5f, 1.0f, 1.0f);
        default_material.textures_a = glm::uvec4(TEXTURE_SLOT_WHITE, TEXTURE_SLOT_WHITE, TEXTURE_SLOT_FLAT_NORMAL, TEXTURE_SLOT_WHITE);
        default_material.textures_b = glm::uvec4(TEXTURE_SLOT_WHITE, TEXTURE_SLOT_WHITE, 0u, 0u);
        m_material_cpu.assign(MAX_MATERIALS, default_material);

        material_slot default_slot{};
        default_slot.ref_count = 1;
        default_slot.alive = true;
        default_slot.asset = INVALID_HANDLE;
        default_slot.texture_assets.fill(INVALID_HANDLE);
        for (u32 i = 0; i < MATERIAL_TEXTURE_SLOTS; ++i)
            default_slot.texture_slots[i] = fallback_texture_slot_for(i);
        m_material_slots.assign(1, default_slot);

        // Fallback textures --------------------------------------------------------------------------------------------
        m_gpu_textures.assign(FIRST_FREE_TEXTURE_SLOT, gpu_texture{});
        m_texture_table.assign(MAX_TEXTURES, util::accessible_image{});

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

        for (u32 i = FIRST_FREE_TEXTURE_SLOT; i < MAX_TEXTURES; ++i)    // every unused slot holds the white texture
            m_texture_table[i] = m_texture_table[TEXTURE_SLOT_WHITE];
        m_texture_high_water = FIRST_FREE_TEXTURE_SLOT;

        // Per-frame buffers + descriptor sets ---------------------------------------------------------------------------
        for (u32 frame_index = 0; frame_index < MAX_CONCURRENT_FRAMES; ++frame_index) {

            m_material_buffers[frame_index] = m_dev->create_buffer(MAX_MATERIALS * sizeof(gpu_material),
                vk::BufferUsageFlagBits::eStorageBuffer, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            m_live_buffer_count++;

            std::memcpy(m_dev->map_buffer(m_material_buffers[frame_index]), m_material_cpu.data(), MAX_MATERIALS * sizeof(gpu_material));
            m_dev->unmap_buffer(m_material_buffers[frame_index]);

            std::vector<util::descriptor_item> items;
            items.emplace_back(0, VKDT::eStorageBuffer, VKSF::eFragment, 1, &m_material_buffers[frame_index]);
            items.emplace_back(1, VKDT::eSampledImage, VKSF::eFragment, MAX_TEXTURES, m_texture_table.data());

            m_material_desc_buffers[frame_index] = m_dev->create_descriptor_buffer(m_material_set_layout, items,
                util::descriptor_buffer_type::resource, 1);
            m_live_buffer_count++;

            m_dev->compute_descriptor_binding_offsets(m_material_set_layout, items);
            m_dev->update_descriptor_buffer(m_material_desc_buffers[frame_index], items, util::descriptor_buffer_type::resource);   // all 4096 slots

            m_material_uploaded_version[frame_index] = m_material_version;
            m_texture_uploaded_version[frame_index] = m_texture_version;
        }
    }


    void renderer::destroy_material_resources() {

        m_device.waitIdle();

        for (auto& desc : m_material_desc_buffers) {
            if (desc.buffer.buffer) m_dev->destroy_buffer(desc.buffer);
            desc = {};
        }
        for (auto& buffer : m_material_buffers) {
            if (buffer.buffer) m_dev->destroy_buffer(buffer);
            buffer = {};
        }
        for (auto& texture : m_gpu_textures)
            if (texture.alive)
                destroy_gpu_texture(texture);

        m_gpu_textures.clear();
        m_free_texture_slots.clear();
        m_texture_slot_by_asset.clear();
        m_texture_table.clear();
        m_material_slots.clear();
        m_free_material_slots.clear();
        m_material_slot_by_asset.clear();
        m_material_cpu.clear();

        if (m_material_sampler) {
            m_device.destroySampler(m_material_sampler);
            m_material_sampler = nullptr;
        }
        if (m_material_set_layout) {
            m_device.destroyDescriptorSetLayout(m_material_set_layout);
            m_material_set_layout = nullptr;
        }
    }

}

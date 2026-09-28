#pragma once

#include <asset/material.h>
#include <asset/texture.h>


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Maps an on-disk texture format + color space to a vk::Format.
    vk::Format to_vk_format(const GLT::asset::texture::texture_format& fmt);

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    vk::Format to_vk_format(const GLT::asset::texture::texture_format& fmt) {

        using pf = GLT::asset::texture::pixel_format;
        using cs = GLT::asset::texture::color_space;
        const bool srgb = (fmt.space == cs::srgb);

        switch (fmt.format) {
            case pf::u8_r:          return vk::Format::eR8Unorm;
            case pf::u8_rg:         return vk::Format::eR8G8Unorm;
            case pf::u8_rgb:        return srgb ? vk::Format::eR8G8B8Srgb : vk::Format::eR8G8B8Unorm;
            case pf::u8_rgba:       return srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
            case pf::u16_r:         return vk::Format::eR16Unorm;
            case pf::u16_rg:        return vk::Format::eR16G16Unorm;
            case pf::u16_rgb:       return vk::Format::eR16G16B16Unorm;
            case pf::u16_rgba:      return vk::Format::eR16G16B16A16Unorm;
            case pf::f32_r:         return vk::Format::eR32Sfloat;
            case pf::f32_rg:        return vk::Format::eR32G32Sfloat;
            case pf::f32_rgb:       return vk::Format::eR32G32B32Sfloat;
            case pf::f32_rgba:      return vk::Format::eR32G32B32A32Sfloat;
            default:                return vk::Format::eUndefined;
        }
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // default texture + samplers --------------------------------------------------------------------------------------

    void renderer::create_default_texture() {

        // samplers ----------------------------------------------------------------------------------------------------
        {
            vk::SamplerCreateInfo si{};
            si.magFilter = vk::Filter::eLinear;
            si.minFilter = vk::Filter::eLinear;
            si.mipmapMode = vk::SamplerMipmapMode::eLinear;
            si.addressModeU = vk::SamplerAddressMode::eRepeat;
            si.addressModeV = vk::SamplerAddressMode::eRepeat;
            si.addressModeW = vk::SamplerAddressMode::eRepeat;
            si.minLod = 0.0f;
            si.maxLod = VK_LOD_CLAMP_NONE;
            si.maxAnisotropy = 1.0f;
            m_default_sampler_linear = m_device.createSampler(si);

            // Slot 0 is a 2x2 checkerboard. Nearest keeps the four texels crisp; bilinear would smear them into a single mid-gray,
            // which is the exact opposite of the point. Repeat lets the pattern tile cleanly for UVs outside [0, 1].
            si.magFilter = vk::Filter::eNearest;
            si.minFilter = vk::Filter::eNearest;
            si.mipmapMode = vk::SamplerMipmapMode::eNearest;
            m_default_sampler_nearest = m_device.createSampler(si);
        }

        // 2x2 dark-gray checkerboard ----------------------------------------------------------------------------------
        // Two alternating shades, tiled by the sampler. Classic missing-texture look but dimmed so it doesn't dominate lit geometry.
        //
        //   pixel(0,0) = A   pixel(1,0) = B
        //   pixel(0,1) = B   pixel(1,1) = A
        constexpr u8 A = 48;    // gray
        constexpr u8 B = 80;    // slightly lighter dark gray

        const u8 checker_pixels[16] = {
            A, A, A, 255,   // (0,0)
            B, B, B, 255,   // (1,0)
            B, B, B, 255,   // (0,1)
            A, A, A, 255,   // (1,1)
        };

        vk::ImageCreateInfo img_info = vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(vk::Format::eR8G8B8A8Unorm)
            .setExtent({2, 2, 1})                        // was {1, 1, 1}
            .setMipLevels(1)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        vr::allocated_image gpu_img = m_vr_dev->create_image(img_info, 0);

        auto staging = m_vr_dev->create_buffer(sizeof(checker_pixels),
            vk::BufferUsageFlagBits::eTransferSrc,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        std::memcpy(m_vr_dev->map_buffer(staging), checker_pixels, sizeof(checker_pixels));
        m_vr_dev->unmap_buffer(staging);

        immediate_submit([&](vk::CommandBuffer cmd) {

            const auto range = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);

            m_vr_dev->transition_image_layout(cmd, gpu_img.image,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, range,
                vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer);

            vk::BufferImageCopy region{};
            region.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            region.imageOffset = vk::Offset3D( 0, 0, 0 );
            region.imageExtent = vk::Extent3D( 2, 2, 1 );        // was {1, 1, 1}
            cmd.copyBufferToImage(staging.buffer, gpu_img.image,
                vk::ImageLayout::eTransferDstOptimal, 1, &region);

            m_vr_dev->transition_image_layout(cmd, gpu_img.image,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, range,
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader);
        });

        m_vr_dev->destroy_buffer(staging);

        vk::ImageViewCreateInfo view_info{};
        view_info.image    = gpu_img.image;
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format   = vk::Format::eR8G8B8A8Unorm;
        view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
        vk::ImageView default_view = m_device.createImageView(view_info);

        // slot 0 + free list ------------------------------------------------------------------------------------------
        m_texture_slots.resize(BINDLESS_TEXTURE_MAX);
        m_free_texture_slots.reserve(BINDLESS_TEXTURE_MAX - 1);
        for (u32 i = BINDLESS_TEXTURE_MAX; i-- > 1; )
            m_free_texture_slots.push_back(i);

        texture_slot& slot = m_texture_slots[0];
        slot.asset          = INVALID_HANDLE;
        slot.image          = gpu_img.image;
        slot.allocation     = gpu_img.allocation;
        slot.view           = default_view;
        slot.sampler        = m_default_sampler_nearest;    // was m_default_sampler_linear
        slot.ref_count      = 1;                            // permanent
        slot.bindless_index = 0;
        slot.alive          = true;

        // every descriptor defaults to the checkerboard --------------------------------------------------------------
        const vr::accessible_image default_desc{
            default_view, m_default_sampler_nearest, vk::ImageLayout::eShaderReadOnlyOptimal, nullptr
        };
        for (u32 i = 0; i < BINDLESS_TEXTURE_MAX; ++i)
            m_texture_descriptors[i] = default_desc;
    }

    // material --------------------------------------------------------------------------------------------------------

    bool renderer::reserve_material_space(u32 count) {

        const u32 need = m_material_used + count;
        if (need <= m_material_capacity)
            return false;

        m_material_capacity = std::max(need, m_material_capacity + std::max(m_material_capacity / 2, MATERIAL_HEADROOM_MIN));

        auto new_mb = m_vr_dev->create_buffer(m_material_capacity * sizeof(gpu_material),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        std::memset(m_vr_dev->map_buffer(new_mb), 0, m_material_capacity * sizeof(gpu_material));
        m_vr_dev->unmap_buffer(new_mb);

        if (m_material_used) {
            const void* src = m_vr_dev->map_buffer(m_material_buffer);
            void* dst = m_vr_dev->map_buffer(new_mb);
            std::memcpy(dst, src, m_material_used * sizeof(gpu_material));
            m_vr_dev->unmap_buffer(m_material_buffer);
            m_vr_dev->unmap_buffer(new_mb);
        }

        std::swap(m_material_buffer, new_mb);
        m_device.waitIdle();
        if (new_mb.buffer) m_vr_dev->destroy_buffer(new_mb);

        update_descriptor_set();
        return true;
    }


    renderer::material_slot* renderer::find_material_slot(GLT::asset::handle h) noexcept {
        for (auto& s : m_material_slots)
            if (s.alive && s.asset == h)
                return &s;
        return nullptr;
    }


    const renderer::material_slot* renderer::find_material_slot(GLT::asset::handle h) const noexcept {

        return const_cast<renderer*>(this)->find_material_slot(h);
    }


    bool renderer::load_material(GLT::asset::handle handle) {

        if (handle == INVALID_HANDLE)
            return false;

        if (find_material_slot(handle))
            return true;

        auto registry = GLT::asset::registry::get_ref();
        auto* mat = registry->data_as<GLT::asset::material::material_asset>(handle);
        VALIDATE(mat, return false, "", "Asset [{}] is not a material_asset", registry->info(handle).name);

        reserve_material_space(1);

        u32 slot_idx;
        if (!m_free_material_slots.empty()) {
            slot_idx = m_free_material_slots.back();
            m_free_material_slots.pop_back();
        } else {
            slot_idx = static_cast<u32>(m_material_slots.size());
            m_material_slots.emplace_back();
        }

        material_slot& slot = m_material_slots[slot_idx];
        slot.asset = handle;
        slot.gpu_index = m_material_used++;
        slot.alive = true;
        slot.textures.fill(INVALID_HANDLE);

        gpu_material gpu{};
        gpu.base_color = mat->params.base_color;
        gpu.emissive = mat->params.emissive;
        gpu.roughness = mat->params.roughness;
        gpu.metallic = mat->params.metallic;
        gpu.reflectance = mat->params.reflectance;
        gpu.normal_scale = mat->params.normal_scale;
        gpu.occlusion_strength = mat->params.occlusion_strength;
        gpu.flags = mat->params.flags;
        gpu.textures.fill(0);   // white fallback by default

        for (size_t i = 0; i < TEXTURE_SLOT_COUNT; ++i) {
            const GLT::asset::handle tex = mat->textures[i];
            if (tex == INVALID_HANDLE)
                continue;
            gpu.textures[i]  = load_texture(tex);
            slot.textures[i] = tex;
        }

        auto* dst = static_cast<gpu_material*>(m_vr_dev->map_buffer(m_material_buffer));
        dst[slot.gpu_index] = gpu;
        m_vr_dev->unmap_buffer(m_material_buffer);

        return true;
    }


    void renderer::unload_material(GLT::asset::handle handle) {

        material_slot* slot = find_material_slot(handle);
        if (!slot)
            return;

        for (auto tex : slot->textures)
            if (tex != INVALID_HANDLE)
                unload_texture(tex);

        slot->alive = false;
        slot->asset = INVALID_HANDLE;
        slot->textures.fill(INVALID_HANDLE);

        m_free_material_slots.push_back(static_cast<u32>(slot - m_material_slots.data()));
    }

    // geometry buffer -------------------------------------------------------------------------------------------------

    bool renderer::reserve_geometry_space(u32 count) {

        const u32 need = m_geometry_used + count;
        if (need <= m_geometry_capacity)
            return false;

        m_geometry_capacity = std::max(need, m_geometry_capacity + std::max(m_geometry_capacity / 2, 4096u));

        auto new_gb = m_vr_dev->create_buffer(m_geometry_capacity * sizeof(gpu_geometry),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        std::memset(m_vr_dev->map_buffer(new_gb), 0, m_geometry_capacity * sizeof(gpu_geometry));
        m_vr_dev->unmap_buffer(new_gb);

        if (m_geometry_used) {
            const void* src = m_vr_dev->map_buffer(m_geometry_buffer);
            void* dst = m_vr_dev->map_buffer(new_gb);
            std::memcpy(dst, src, m_geometry_used * sizeof(gpu_geometry));
            m_vr_dev->unmap_buffer(m_geometry_buffer);
            m_vr_dev->unmap_buffer(new_gb);
        }

        std::swap(m_geometry_buffer, new_gb);
        m_device.waitIdle();
        if (new_gb.buffer) m_vr_dev->destroy_buffer(new_gb);

        update_descriptor_set();
        return true;
    }

    // texture ---------------------------------------------------------------------------------------------------------

    u32 renderer::load_texture(GLT::asset::handle handle) {

        if (handle == INVALID_HANDLE)
            return 0;

        if (const u32 existing = find_texture_index(handle); existing != UINT32_MAX) {
            ++m_texture_slots[existing].ref_count;
            return existing;
        }

        auto registry = GLT::asset::registry::get_ref();
        auto* tex = registry->data_as<GLT::asset::texture::texture_asset>(handle);
        VALIDATE(tex, return 0, "", "Asset [{}] is not a texture_asset", registry->info(handle).name);
        VALIDATE(!m_free_texture_slots.empty(), return 0, "", "bindless texture slots exhausted");

        const u32 idx = m_free_texture_slots.back();
        m_free_texture_slots.pop_back();

        const vk::Format fmt = to_vk_format(tex->format);
        if (fmt == vk::Format::eUndefined) {
            LOG(error, "unsupported pixel format for [{}]", registry->info(handle).name);
            m_free_texture_slots.push_back(idx);
            return 0;
        }

        const u32 mip_levels = std::max<u32>(1, tex->format.mip_levels);
        const u32 width = tex->format.width;
        const u32 height = tex->format.height;

        vk::ImageCreateInfo img_info = vk::ImageCreateInfo()
            .setImageType(vk::ImageType::e2D)
            .setFormat(fmt)
            .setExtent({ width, height, 1 })
            .setMipLevels(mip_levels)
            .setArrayLayers(1)
            .setSamples(vk::SampleCountFlagBits::e1)
            .setTiling(vk::ImageTiling::eOptimal)
            .setUsage(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled)
            .setSharingMode(vk::SharingMode::eExclusive)
            .setInitialLayout(vk::ImageLayout::eUndefined);

        vr::allocated_image gpu_img = m_vr_dev->create_image(img_info, 0);

        // staging buffer ----------------------------------------------------------------------------------------------
        const u64 total_bytes = static_cast<u64>(tex->pixels.size());
        VALIDATE(total_bytes > 0, m_vr_dev->destroy_image(gpu_img); m_free_texture_slots.push_back(idx); return 0, "",
            "texture [{}] has no pixel data", registry->info(handle).name);

        auto staging = m_vr_dev->create_buffer(total_bytes, vk::BufferUsageFlagBits::eTransferSrc,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        std::memcpy(m_vr_dev->map_buffer(staging), tex->pixels.data(), total_bytes);
        m_vr_dev->unmap_buffer(staging);

        // upload ------------------------------------------------------------------------------------------------------
        immediate_submit([&](vk::CommandBuffer cmd) {

            const auto range = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, mip_levels, 0, 1);

            m_vr_dev->transition_image_layout(cmd, gpu_img.image,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, range,
                vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer);

            std::vector<vk::BufferImageCopy> regions;

            if (!tex->mips.empty()) {
                for (u32 m = 0; m < mip_levels && m < tex->mips.size(); ++m) {
                    const auto& mr = tex->mips[m];
                    vk::BufferImageCopy r{};
                    r.bufferOffset = mr.offset;
                    r.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, m, 0, 1);
                    r.imageOffset = vk::Offset3D( 0, 0, 0 );
                    r.imageExtent = vk::Extent3D( mr.width, mr.height, mr.depth ? mr.depth : 1u );
                    regions.push_back(r);
                }
            } else {
                vk::BufferImageCopy r{};
                r.imageSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
                r.imageOffset = vk::Offset3D( 0, 0, 0 );
                r.imageExtent = vk::Extent3D( width, height, 1 );
                regions.push_back(r);
            }

            cmd.copyBufferToImage(staging.buffer, gpu_img.image,
                vk::ImageLayout::eTransferDstOptimal,
                static_cast<u32>(regions.size()), regions.data());

            m_vr_dev->transition_image_layout(cmd, gpu_img.image,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, range,
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader);
        });

        m_vr_dev->destroy_buffer(staging);

        // view --------------------------------------------------------------------------------------------------------
        vk::ImageViewCreateInfo view_info{};
        view_info.image = gpu_img.image;
        view_info.viewType = vk::ImageViewType::e2D;
        view_info.format = fmt;
        view_info.subresourceRange = vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, mip_levels, 0, 1);
        vk::ImageView view = m_device.createImageView(view_info);

        // slot --------------------------------------------------------------------------------------------------------
        texture_slot slot{};
        slot.asset = handle;
        slot.image = gpu_img.image;
        slot.allocation = gpu_img.allocation;
        slot.view = view;
        slot.sampler = m_default_sampler_linear;
        slot.ref_count = 1;
        slot.bindless_index = idx;
        slot.alive = true;
        m_texture_slots[idx] = slot;

        m_texture_descriptors[idx] = {
            view, m_default_sampler_linear, vk::ImageLayout::eShaderReadOnlyOptimal, nullptr
        };

        // per-element descriptor update -------------------------------------------------------------------------------
        // Find the binding 6 descriptor_item and update just this slot.
        for (const auto& item : m_resource_bindings) {
            if (item.binding != 6)
                continue;
            m_vr_dev->update_descriptor_buffer(m_resource_desc_buffer, item, idx, vr::descriptor_buffer_type::combined);
            break;
        }

        // Mirror into the preview descriptor buffer if it exists.
        if (m_preview_ready) {
            for (const auto& item : m_preview_bindings) {
                if (item.binding != 6)
                    continue;
                m_vr_dev->update_descriptor_buffer(m_preview_desc_buffer, item, idx, vr::descriptor_buffer_type::combined);
                break;
            }
        }

        return idx;
    }


    void renderer::unload_texture(GLT::asset::handle handle) {

        const u32 idx = find_texture_index(handle);
        if (idx == UINT32_MAX)
            return;

        texture_slot& slot = m_texture_slots[idx];
        if (slot.ref_count > 1) {
            --slot.ref_count;
            return;
        }

        m_device.waitIdle();

        // Reset the descriptor to the white fallback before destroying anything.
        const auto& white = m_texture_slots[0];
        m_texture_descriptors[idx] = {
            white.view, white.sampler, vk::ImageLayout::eShaderReadOnlyOptimal, nullptr
        };

        for (const auto& item : m_resource_bindings) {
            if (item.binding != 6)
                continue;
            m_vr_dev->update_descriptor_buffer(
                m_resource_desc_buffer, item, idx,
                vr::descriptor_buffer_type::combined);
            break;
        }

        if (slot.view)
            m_device.destroyImageView(slot.view);

        vr::allocated_image img{};
        img.image      = slot.image;
        img.allocation = slot.allocation;
        if (img.image)
            m_vr_dev->destroy_image(img);

        slot = texture_slot{};
        m_free_texture_slots.push_back(idx);
    }


    u32 renderer::find_texture_index(GLT::asset::handle h) const noexcept {

        if (h == INVALID_HANDLE)
            return UINT32_MAX;

        for (const auto& s : m_texture_slots)
            if (s.alive && s.asset == h)
                return s.bindless_index;

        return UINT32_MAX;
    }

}

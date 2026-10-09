
#include "util/pch.h"
#include "render_graph.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::graph {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    render_graph::render_graph(util::device* device) : m_device(device) {}


    render_graph::~render_graph() {

        for (auto& texture : m_textures)
            destroy_texture(texture);
        for (auto& buffer : m_buffers)
            destroy_buffer(buffer);
    }

    // CLASS PUBLIC ====================================================================================================

    void render_graph::begin_frame() {

        // Non-persistent resources are per-frame. Transient resources own their memory; imported ones are references to
        // caller-owned memory. Either way the slot is reset so the next create_*() reuses it
        for (auto& texture : m_textures) {
            if (!texture.desc.persistent) {
                destroy_texture(texture);             // imported: just nulls pointers
                texture = texture_resource{};         // wipe the slot so it can be reused
            }
        }

        for (auto& buffer : m_buffers) {
            if (!buffer.desc.persistent) {
                destroy_buffer(buffer);
                buffer = buffer_resource{};
            }
        }

        m_passes.clear();
    }


    void render_graph::end_frame() { /* Nothing else to do here. Persistent resources survive */ }

    // Resource creation -----------------------------------------------------------------------------------------------

    texture_handle render_graph::create_texture(const texture_desc& desc) {

        // Persistent resources are re-used by name across frames
        if (desc.persistent) {

            auto it = m_persistent_textures.find(desc.name);
            if (it != m_persistent_textures.end()) {

                // caller may have resized (or otherwise recreated) the backing image since last frame e.g. renderer::set_render_size()
                // rebuilds m_output_image at the new panel size and re-imports it. The resource's desc must reflect the current size,
                // because texture_extent() / texture_format() drive the viewport + scissor the graph sets in begin_rendering()
                // If we don't refresh it, a resized persistent target keeps reporting its old size and the pass renders into a
                // stale sub-rectangle of the real attachment
                auto& texture = m_textures[it->second.id];

                texture.desc.format = desc.format;
                texture.desc.width = desc.width;
                texture.desc.height = desc.height;
                texture.desc.depth = desc.depth;
                texture.desc.mip_levels = desc.mip_levels;
                texture.desc.array_layers = desc.array_layers;
                texture.desc.samples = desc.samples;
                texture.desc.usage = desc.usage;
                // persistent flag and name stay as-is

                return it->second;
            }
        }

        u32 texture_index = UINT32_MAX;
        for (u32 index = 0; index < m_textures.size(); ++index) {
            if (m_textures[index].desc.name.empty() && m_textures[index].image == nullptr) {
                texture_index = index;
                break;
            }
        }
        if (texture_index == UINT32_MAX) {
            texture_index = static_cast<u32>(m_textures.size());
            m_textures.emplace_back();
        }

        auto& texture = m_textures[texture_index];
        texture = texture_resource{};
        texture.desc = desc;
        texture.state = {vk::ImageLayout::eUndefined, {}, vk::PipelineStageFlagBits2::eNone};

        // Derive usage flags if the user didn't supply them
        vk::ImageUsageFlags usage = desc.usage;
        if (!usage) {
            usage = vk::ImageUsageFlagBits::eSampled
                | vk::ImageUsageFlagBits::eStorage
                | vk::ImageUsageFlagBits::eColorAttachment
                | vk::ImageUsageFlagBits::eTransferSrc
                | vk::ImageUsageFlagBits::eTransferDst;
        }

        vk::ImageCreateInfo image_ci{};
        image_ci.imageType = vk::ImageType::e2D;
        image_ci.format = desc.format;
        image_ci.extent = vk::Extent3D(desc.width, desc.height, desc.depth);
        image_ci.mipLevels = desc.mip_levels;
        image_ci.arrayLayers = desc.array_layers;
        image_ci.samples = desc.samples;
        image_ci.tiling = vk::ImageTiling::eOptimal;
        image_ci.usage = usage;
        image_ci.sharingMode = vk::SharingMode::eExclusive;
        image_ci.initialLayout = vk::ImageLayout::eUndefined;

        texture.allocated = m_device->create_image(image_ci, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
        texture.image = texture.allocated.image;

        vk::ImageViewCreateInfo image_view_ci{};
        image_view_ci.image = texture.image;
        image_view_ci.viewType = (desc.array_layers > 1) ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
        image_view_ci.format = desc.format;
        image_view_ci.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, desc.mip_levels, 0, desc.array_layers};
        texture.default_view = m_device->get_device().createImageView(image_view_ci);

        texture_handle h{texture_index};
        if (desc.persistent)
            m_persistent_textures[desc.name] = h;
        return h;
    }


    buffer_handle render_graph::create_buffer(const buffer_desc& desc) {

        if (desc.persistent) {
            auto it = m_persistent_buffers.find(desc.name);
            if (it != m_persistent_buffers.end())
                return it->second;
        }

        u32 buffer_index = UINT32_MAX;
        for (u32 index = 0; index < m_buffers.size(); ++index) {
            if (m_buffers[index].desc.name.empty() && m_buffers[index].allocated.buffer == nullptr) {

                buffer_index = index;
                break;
            }
        }

        if (buffer_index == UINT32_MAX) {
            buffer_index = static_cast<u32>(m_buffers.size());
            m_buffers.emplace_back();
        }

        auto& buffer = m_buffers[buffer_index];
        buffer = buffer_resource{};
        buffer.desc = desc;
        buffer.allocated = m_device->create_buffer(desc.size, desc.usage, desc.vma_flags);
        buffer.state = {vk::ImageLayout::eUndefined, {}, vk::PipelineStageFlagBits2::eNone};

        buffer_handle h{buffer_index};
        if (desc.persistent)
            m_persistent_buffers[desc.name] = h;
        return h;
    }


    void render_graph::import_texture(texture_handle h, vk::Image image, vk::ImageView view, resource_state s) {

        auto& texture = m_textures[h.id];

        // The slot was created via create_texture(), which allocated a graph-owned VMA image and a default view
        // Now the caller is handing us an external image instead. Destroy what create_texture() made, otherwise it leaks:
        // destroy_texture() short-circuits on imported resources, so it would never clean this up either
        if (!texture.imported) {

            for (auto& [_, image_view] : texture.view_cache)
                if (image_view)
                    m_device->get_device().destroyImageView(image_view);
            texture.view_cache.clear();

            if (texture.default_view) {
                m_device->get_device().destroyImageView(texture.default_view);
                texture.default_view = nullptr;
            }

            if (texture.allocated.image)
                m_device->destroy_image(texture.allocated);
        }

        texture.imported = true;
        texture.image = image;
        texture.default_view = view;
        texture.state = s;
        texture.allocated = {};       // owned by the caller now
        texture.view_cache.clear();
    }


    void render_graph::import_buffer(buffer_handle h, const util::allocated_buffer& buf) {

        auto& buffer = m_buffers[h.id];
        buffer.imported = true;
        buffer.allocated = buf;
    }

    // Compile ---------------------------------------------------------------------------------------------------------

    void render_graph::compile() {

        for (u32 index = 0; index < m_passes.size(); ++index) 
            m_passes[index].index = index;

        cull();
        compute_lifetimes();

        m_pass_names.clear();
        for (auto& pass : m_passes)
            if (!pass.culled)
                m_pass_names.push_back(pass.name);
    }

    // Execute ---------------------------------------------------------------------------------------------------------

    void render_graph::execute(vk::CommandBuffer cmd) {

        // Reset tracked state for transient images (their old layouts mean nothing after we destroyed them last frame)
        // Persistent and imported images keep whatever state they had
        for (auto& texture : m_textures) {
            if (!texture.desc.persistent && !texture.imported)
                texture.state = {vk::ImageLayout::eUndefined, {}, vk::PipelineStageFlagBits2::eNone};
        }

        for (auto& pass : m_passes) {
            if (pass.culled)
                continue;

            emit_barriers(cmd, pass);

            pass_context ctx{};
            ctx.cmd = cmd;
            ctx.graph = this;
            ctx.render_area = vk::Extent2D(m_textures.empty() ? 0u : m_textures[0].desc.width, 
                m_textures.empty() ? 0u : m_textures[0].desc.height);

            // Prefer the first colour attachment's extent, if there is one
            if (!pass.builder.m_color_attachments.empty())
                ctx.render_area = texture_extent(pass.builder.m_color_attachments[0].handle);
            else if (pass.builder.m_depth_attachment)
                ctx.render_area = texture_extent(pass.builder.m_depth_attachment->handle);

            ctx.set_attachments(&pass.builder);
            pass.execute(ctx);
        }
    }


    std::string render_graph::dump() const {

        std::string string = "render graph passes:\n";
        for (auto& pass : m_passes)
            string += "  [" + std::to_string(pass.index) + "] " + pass.name + (pass.culled ? "  (culled)" : "") + "\n";
        return string;
    }


    // Resource lookup (used by pass_context) --------------------------------------------------------------------------

    vk::Image render_graph::texture_image(texture_handle h) const { return m_textures[h.id].image; }


    vk::ImageView render_graph::texture_view(texture_handle h, u32 mip, u32 layer) {

        auto& texture = m_textures[h.id];
        if (mip == 0 && layer == 0 && texture.desc.mip_levels <= 1 && texture.desc.array_layers <= 1)
            return texture.default_view;

        u64 key = (u64(mip) << 32) | u64(layer);
        auto it = texture.view_cache.find(key);
        if (it != texture.view_cache.end())
            return it->second;

        vk::ImageViewCreateInfo image_view_ci{};
        image_view_ci.image = texture.image;
        image_view_ci.viewType = vk::ImageViewType::e2D;
        image_view_ci.format = texture.desc.format;
        image_view_ci.subresourceRange = {vk::ImageAspectFlagBits::eColor, mip, 1, layer, 1};

        vk::ImageView image_view = m_device->get_device().createImageView(image_view_ci);
        texture.view_cache[key] = image_view;
        return image_view;
    }


    vk::Extent2D render_graph::texture_extent(texture_handle h) const { return {m_textures[h.id].desc.width, m_textures[h.id].desc.height}; }


    vk::Format render_graph::texture_format(texture_handle h) const { return m_textures[h.id].desc.format; }


    util::allocated_buffer* render_graph::buffer_allocation(buffer_handle h) const {

        auto& buffer = m_buffers[h.id];
        return buffer.allocated.buffer ? &const_cast<util::allocated_buffer&>(buffer.allocated) : nullptr;
    }


    vk::ImageLayout render_graph::texture_layout(texture_handle h) const { return m_textures[h.id].state.layout; }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    // Barriers --------------------------------------------------------------------------------------------------------

    resource_state render_graph::target_texture_state(const pass& p, texture_handle h) const {

        std::optional<resource_state> state;
        auto combine = [&](texture_usage u) {

            resource_state derived_sate = derive_state(u);
            if (!state) { 

                state = derived_sate;
                return;
            }
            resource_state merged{};
            merged.access = state->access | derived_sate.access;
            merged.stage = state->stage | derived_sate.stage;

            // Prefer General / attachment layouts when either side demands them
            auto layout_rank = [](vk::ImageLayout layout) {
                switch (layout) {
                    case vk::ImageLayout::eUndefined:                       return 0;
                    case vk::ImageLayout::eShaderReadOnlyOptimal:           return 1;
                    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:     return 1;
                    case vk::ImageLayout::eTransferSrcOptimal:              return 2;
                    case vk::ImageLayout::eTransferDstOptimal:              return 2;
                    case vk::ImageLayout::eColorAttachmentOptimal:          return 3;
                    case vk::ImageLayout::eDepthStencilAttachmentOptimal:   return 3;
                    case vk::ImageLayout::eGeneral:                         return 4;
                    case vk::ImageLayout::ePresentSrcKHR:                   return 5;
                    default:                                                return 2;
                }
            };
            merged.layout = layout_rank(state->layout) >= layout_rank(derived_sate.layout) ? state->layout : derived_sate.layout;
            state = merged;
        };

        for (auto& [handle, usage] : p.builder.m_tex_reads)
            if (handle.id == h.id)
                combine(usage);

        for (auto& [handle, usage] : p.builder.m_tex_writes)
            if (handle.id == h.id)
                combine(usage);

        return state.value_or(resource_state{});
    }


    resource_state render_graph::target_buffer_state(const pass& p, buffer_handle h) const {

        std::optional<resource_state> state;
        auto combine = [&](buffer_usage u) {

            resource_state d_state = derive_state(u);
            if (!state) {
                state = d_state;
                return;
            }
            state->access |= d_state.access;
            state->stage  |= d_state.stage;
        };

        for (auto& [hh, u] : p.builder.m_buf_reads)
            if (hh.id == h.id)
                combine(u);

        for (auto& [hh, u] : p.builder.m_buf_writes)
            if (hh.id == h.id)
                combine(u);

        return state.value_or(resource_state{});
    }


    void render_graph::emit_barriers(vk::CommandBuffer cmd, const pass& p) {

        // Gather all distinct resources touched by this pass
        std::vector<vk::ImageMemoryBarrier2> image_barriers;
        std::vector<vk::BufferMemoryBarrier2> buffer_barriers;

        auto add_image_barrier = [&](texture_resource& texture_resource, resource_state target) {

            if (texture_resource.state == target)
                return;

            vk::ImageMemoryBarrier2 image_memory_barrier{};
            image_memory_barrier.srcStageMask = texture_resource.state.stage;
            image_memory_barrier.srcAccessMask = texture_resource.state.access;
            image_memory_barrier.dstStageMask = target.stage;
            image_memory_barrier.dstAccessMask = target.access;
            image_memory_barrier.oldLayout = texture_resource.state.layout;
            image_memory_barrier.newLayout = target.layout;
            image_memory_barrier.image = texture_resource.image;
            image_memory_barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, texture_resource.desc.mip_levels, 0, texture_resource.desc.array_layers};
            image_barriers.push_back(image_memory_barrier);
            texture_resource.state = target;
        };

        auto add_buffer_barrier = [&](buffer_resource& buffer_resource, resource_state target) {

            if (buffer_resource.state == target)
                return;
            if (!buffer_resource.allocated.buffer)
                return;

            vk::BufferMemoryBarrier2 bar{};
            bar.srcStageMask = buffer_resource.state.stage;
            bar.srcAccessMask = buffer_resource.state.access;
            bar.dstStageMask = target.stage;
            bar.dstAccessMask = target.access;
            bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bar.buffer = buffer_resource.allocated.buffer;
            bar.offset = 0;
            bar.size = VK_WHOLE_SIZE;
            buffer_barriers.push_back(bar);
            buffer_resource.state = target;
        };

        // Which textures does this pass touch?
        std::vector<u32> tex_ids;
        for (auto& [handle, _] : p.builder.m_tex_reads)
            tex_ids.push_back(handle.id);

        for (auto& [handle, _] : p.builder.m_tex_writes)
            tex_ids.push_back(handle.id);

        std::sort(tex_ids.begin(), tex_ids.end());
        tex_ids.erase(std::unique(tex_ids.begin(), tex_ids.end()), tex_ids.end());

        for (u32 id : tex_ids) {
            texture_handle h{id};
            add_image_barrier(m_textures[id], target_texture_state(p, h));
        }

        std::vector<u32> buf_ids;
        for (auto& [handle, _] : p.builder.m_buf_reads)
            buf_ids.push_back(handle.id);

        for (auto& [handle, _] : p.builder.m_buf_writes)
            buf_ids.push_back(handle.id);

        std::sort(buf_ids.begin(), buf_ids.end());
        buf_ids.erase(std::unique(buf_ids.begin(), buf_ids.end()), buf_ids.end());

        for (u32 id : buf_ids) {
            buffer_handle h{id};
            add_buffer_barrier(m_buffers[id], target_buffer_state(p, h));
        }

        if (image_barriers.empty() && buffer_barriers.empty())
            return;

        vk::DependencyInfo dependency_info{};
        dependency_info.imageMemoryBarrierCount = static_cast<u32>(image_barriers.size());
        dependency_info.pImageMemoryBarriers = image_barriers.empty() ? nullptr : image_barriers.data();
        dependency_info.bufferMemoryBarrierCount = static_cast<u32>(buffer_barriers.size());
        dependency_info.pBufferMemoryBarriers = buffer_barriers.empty() ? nullptr : buffer_barriers.data();
        cmd.pipelineBarrier2(dependency_info);
    }

    // Compile ---------------------------------------------------------------------------------------------------------

    void render_graph::cull() {

        // A pass is live if it has side effects, or if at least one of its written resources is read (or written) by a 
        // live pass downstream. Start from side-effect / present passes and walk backwards
        //
        // Simplest correct implementation: iterate to a fixpoint. The pass count is small
        for (auto& pass : m_passes)
            pass.culled = true;

        std::vector<bool> used_by_live(m_textures.size() + m_buffers.size(), false);

        bool changed = true;
        while (changed) {
            changed = false;
            for (i32 index = static_cast<i32>(m_passes.size()) - 1; index >= 0; --index) {
                auto& pass = m_passes[index];
                bool live = pass.builder.m_side_effects;
                if (!live) {
                    for (auto& [handle, _] : pass.builder.m_tex_writes)
                        if (used_by_live[handle.id]) { 
                            live = true;
                            break;
                        }
                }

                if (!live)
                    continue;

                if (!pass.culled)
                    continue;

                pass.culled = false;
                changed = true;

                // Any resource this live pass reads or writes is now considered live
                for (auto& [handle, _] : pass.builder.m_tex_reads)
                    used_by_live[handle.id] = true;

                for (auto& [handle, _] : pass.builder.m_tex_writes)
                    used_by_live[handle.id] = true;
            }
        }
    }


    void render_graph::compute_lifetimes() {

        for (auto& texture : m_textures) {
            texture.first_use = UINT32_MAX; 
            texture.last_use = 0; 
        }

        for (auto& buffer : m_buffers)  {
            buffer.first_use = UINT32_MAX; 
            buffer.last_use = 0; 
        }

        for (auto& pass : m_passes) {

            if (pass.culled)
                continue;

            auto touch_tex = [&](texture_handle h) {
                auto& texture = m_textures[h.id];
                texture.first_use = std::min(texture.first_use, pass.index);
                texture.last_use = std::max(texture.last_use,  pass.index);
            };
            auto touch_buf = [&](buffer_handle h) {
                auto& buffer = m_buffers[h.id];
                buffer.first_use = std::min(buffer.first_use, pass.index);
                buffer.last_use = std::max(buffer.last_use,  pass.index);
            };

            for (auto& [handle, _] : pass.builder.m_tex_reads)
                touch_tex(handle);

            for (auto& [handle, _] : pass.builder.m_tex_writes)
                touch_tex(handle);

            for (auto& [handle, _] : pass.builder.m_buf_reads)
                touch_buf(handle);

            for (auto& [handle, _] : pass.builder.m_buf_writes)
                touch_buf(handle);
        }

        // Aliasing would use this information to overlay allocations whose [first_use, last_use] ranges don't overlap
        // Left as future work
    }

    // Destruction -----------------------------------------------------------------------------------------------------

    void render_graph::destroy_texture(texture_resource& texture_resource) {

        if (texture_resource.imported) {

            // Do not destroy imported views or images — user owns them
            texture_resource.image = nullptr;
            texture_resource.default_view = nullptr;
            return;
        }

        for (auto& [_, image_view] : texture_resource.view_cache)
            if (image_view)
                m_device->get_device().destroyImageView(image_view);

        texture_resource.view_cache.clear();

        if (texture_resource.default_view)
            m_device->get_device().destroyImageView(texture_resource.default_view);

        if (texture_resource.allocated.image)
            m_device->destroy_image(texture_resource.allocated);

        texture_resource.image = nullptr;
        texture_resource.default_view = nullptr;
    }


    void render_graph::destroy_buffer(buffer_resource& buffer_resource) {

        if (buffer_resource.imported) { 
            buffer_resource.allocated = {};
            return;
        }
        if (buffer_resource.allocated.buffer)
            m_device->destroy_buffer(buffer_resource.allocated);
    }

}

#include "util/pch.h"
#include <debug/trace.h>
#include "util/util_pch.h"
#include "shared_resource_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // Virtual geometry cluster records go to the GPU unchanged: this is ClusterData in virtual_geometry_cull.comp.glsl
    static_assert(sizeof(GLT::asset::mesh::cluster) == 80, "cluster layout mismatch (ClusterData in virtual_geometry_cull.comp.glsl)");
    static_assert(offsetof(GLT::asset::mesh::cluster, lod_error) == 48 && offsetof(GLT::asset::mesh::cluster, first_index) == 56
        && offsetof(GLT::asset::mesh::cluster, submesh) == 64 && offsetof(GLT::asset::mesh::cluster, cone_axis) == 72,
        "cluster layout mismatch (ClusterData in virtual_geometry_cull.comp.glsl)");

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

    // CLASS PUBLIC ====================================================================================================

    std::expected<shared_mesh_id, upload_error> shared_resource_manager::ensure_mesh(GLT::asset::handle handle) {
        GLT_PROFILE_SCOPE("shared_resource_manager::ensure_mesh");

        if (handle == INVALID_HANDLE)
            return std::unexpected(upload_error::make(upload_error::code::not_found, "invalid handle"));

        if (const auto it = m_mesh_id_by_asset.find(handle); it != m_mesh_id_by_asset.end()) {
            if (it->second < m_mesh_slots.size() && m_mesh_slots[it->second].alive) {
                m_mesh_slots[it->second].ref_count++;
                return it->second;
            }
        }

        const shared_mesh_id id = load_mesh_internal(handle);
        if (id == INVALID_SHARED_MESH) {
            if (m_failed_mesh_loads.contains(handle))
                return std::unexpected(upload_error::make(upload_error::code::decode_failed, "mesh load failed"));
            return std::unexpected(upload_error::make(upload_error::code::not_found, "mesh not loadable"));
        }
        return id;
    }


    void shared_resource_manager::retain_mesh(shared_mesh_id /*id*/) {
        GLT_PROFILE_SCOPE("shared_resource_manager::retain_mesh");

        // v1: ref counting handled at the scene-renderer level
    }


    void shared_resource_manager::release_mesh(shared_mesh_id id) {
        GLT_PROFILE_SCOPE("shared_resource_manager::release_mesh");

        if (id >= m_mesh_slots.size() || !m_mesh_slots[id].alive)
            return;

        if (--m_mesh_slots[id].ref_count > 0)
            return;

        mesh_slot& slot = m_mesh_slots[id];

        // In-flight frames still draw from these ranges (and read the mesh-table entry). Only hand the ranges and the slot
        // back once the GPU is done, otherwise the next upload overwrites geometry that is still being rendered
        retire_slot_ranges(slot);
        retire([this, id]() { m_free_slots.push_back(id); });

        // Release material slots owned by the submeshes
        for (u32 ms : slot.material_slots)
            release_material(ms);
        slot.material_slots.clear();

        // NOTE: the table entry is intentionally NOT cleared here. In-flight frames may still reference this slot. The slot
        // only becomes reusable (m_free_slots) once the retire callback above has run

        m_mesh_id_by_asset.erase(slot.asset);
        slot = {};
    }


    bool shared_resource_manager::is_mesh_ready(shared_mesh_id id) const { return id < m_mesh_slots.size() && m_mesh_slots[id].alive; }


    const mesh_slot* shared_resource_manager::get_mesh(shared_mesh_id id) const {
        if (id >= m_mesh_slots.size() || !m_mesh_slots[id].alive)
            return nullptr;
        return &m_mesh_slots[id];
    }


    bool shared_resource_manager::refresh_mesh(shared_mesh_id id) {
        GLT_PROFILE_SCOPE("shared_resource_manager::refresh_mesh");

        if (id >= m_mesh_slots.size() || !m_mesh_slots[id].alive)
            return false;

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return false;
        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(m_mesh_slots[id].asset);
        if (!mesh)
            return false;
        if (mesh == m_mesh_slots[id].source && mesh->revision == m_mesh_slots[id].source_revision)
            return false;

        // The editor also bumps the revision for settings-only edits (LOD switch sizes, bias: every frame while a slider
        // is dragged). The renderer does not use those, so only re-upload when the geometry arrays were replaced/resized
        const u64 geometry = geometry_signature(*mesh);
        if (mesh == m_mesh_slots[id].source && geometry == m_mesh_slots[id].source_geometry) {
            // Settings only: the switch sizes may have changed. Patch them into the mesh table entry (an in-place write:
            // the upload waits for the frames in flight that read the old entry)
            mesh_slot& slot = m_mesh_slots[id];
            slot.source_revision = mesh->revision;
            slot.lod_screen = lod_screen_sizes(*mesh, slot.lod_count);
            if (id < m_mesh_table_capacity) {
                std::copy(slot.lod_screen.begin(), slot.lod_screen.end(), m_mesh_table[id].lod_screen);
                mark_mesh_table_dirty(id, true);
            }
            write_mesh_cluster_entry(id, slot, mesh->virtual_geometry.error_scale, true);    // live: detail scale
            return false;
        }

        const bool uploaded = reupload_slot(id, *mesh);

        mesh_slot& slot = m_mesh_slots[id];
        slot.source = mesh;
        slot.source_revision = mesh->revision;     // also on failure: don't retry every frame
        slot.source_geometry = geometry;
        return uploaded;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void shared_resource_manager::init_pools() {
        GLT_PROFILE_SCOPE("shared_resource_manager::init_pools");

        m_vertex_pool = {
            .buffer = &m_vertex_buffer,
            .name = "vertices",
            .usage = vk::BufferUsageFlagBits::eVertexBuffer,
            .element_size = sizeof(GLT::asset::mesh::vertex),
            .min_headroom = VERTEX_HEADROOM_MIN,
            .growable = true,
        };

        m_index_pool = {
            .buffer = &m_index_buffer,
            .name = "indices",
            .usage = vk::BufferUsageFlagBits::eIndexBuffer,
            .element_size = sizeof(u32),
            .min_headroom = INDEX_HEADROOM_MIN,
            .growable = true,
        };

        m_submesh_pool = {
            .buffer = &m_submesh_data_buffer,
            .name = "submeshes",
            .element_size = sizeof(gpu_submesh_data),
            .min_headroom = SUBMESH_HEADROOM_MIN,
            .growable = true,
        };

        m_cluster_pool = {
            .buffer = &m_cluster_data_buffer,
            .name = "clusters",
            .element_size = sizeof(GLT::asset::mesh::cluster),
            .min_headroom = CLUSTER_HEADROOM_MIN,
            .growable = true,
        };
    }


    bool shared_resource_manager::pool_can_fit(const linear_pool& p, u32 count) const {

        if (count == 0)
            return true;

        if (static_cast<u64>(p.high_water) + count <= p.capacity)
            return true;

        for (const auto& r : p.free_ranges)
            if (r.size >= count)
                return true;
        return false;
    }


    std::optional<u32> shared_resource_manager::pool_try_allocate(linear_pool& p, u32 count) {
        GLT_PROFILE_SCOPE("shared_resource_manager::pool_try_allocate");

        if (count == 0)
            return p.high_water;    // no-op allocation for degenerate assets

        // First-fit from free list
        for (auto it = p.free_ranges.begin(); it != p.free_ranges.end(); ++it) {

            if (it->size < count)
                continue;

            const u32 offset = it->offset;
            if (it->size == count)
                p.free_ranges.erase(it);
            else {
                it->offset += count;
                it->size -= count;
            }
            p.live += count;
            return offset;
        }

        // Bump past the high water mark
        if (static_cast<u64>(p.high_water) + count <= p.capacity) {
            const u32 offset = p.high_water;
            p.high_water += count;
            p.live += count;
            return offset;
        }
        return std::nullopt;
    }


    void shared_resource_manager::pool_free(linear_pool& p, u32 offset, u32 count) {
        GLT_PROFILE_SCOPE("shared_resource_manager::pool_free");

        if (count == 0)
            return;
        p.live -= count;

        auto it = std::lower_bound(p.free_ranges.begin(), p.free_ranges.end(), offset,
            [](const free_range& r, u32 off) { return r.offset < off; });

        // Coalesce with preceding range
        if (it != p.free_ranges.begin()) {
            auto prev = std::prev(it);
            if (prev->offset + prev->size == offset) {
                offset = prev->offset;
                count  += prev->size;
                it = p.free_ranges.erase(prev);
            }
        }

        // Coalesce with following range
        if (it != p.free_ranges.end() && offset + count == it->offset) {
            count += it->size;
            it = p.free_ranges.erase(it);
        }

        p.free_ranges.insert(it, { offset, count });

        // Tail reclaim — pull the bump pointer back over any trailing free ranges
        while (!p.free_ranges.empty()) {
            const auto& last = p.free_ranges.back();
            if (last.offset + last.size == p.high_water) {
                p.high_water = last.offset;
                p.free_ranges.pop_back();
            } else break;
        }
    }


    void shared_resource_manager::pool_free_deferred(linear_pool& p, u32 generation, u32 offset, u32 count) {

        // Compaction rebuilt the pool from the live slots only: a range that was pending release is not in it any more
        if (p.generation != generation)
            return;
        pool_free(p, offset, count);
    }


    util::allocated_buffer shared_resource_manager::create_pool_buffer(const linear_pool& p, u32 capacity) {

        // Device-local, no host access: VRAM on a discrete GPU. Written through staging copies, grown / compacted with
        // buffer-to-buffer copies (hence transfer src + dst)
        const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc
            | vk::BufferUsageFlagBits::eTransferDst | p.usage;

        util::allocated_buffer buffer = m_dev->create_buffer(static_cast<vk::DeviceSize>(capacity) * p.element_size, usage, 0);
        if (buffer.buffer)
            log_buffer_memory(p.name, buffer);
        return buffer;
    }


    bool shared_resource_manager::pool_resize(linear_pool& p, u32 new_capacity) {
        GLT_PROFILE_SCOPE("shared_resource_manager::pool_resize");

        if (new_capacity == p.capacity)
            return true;
        if (!p.growable && new_capacity > p.capacity)
            return false;
        if (new_capacity < p.high_water || new_capacity == 0)
            return false;

        // Clamp to device limit
        const u64 max_bytes = m_physical_device.getProperties().limits.maxStorageBufferRange;
        if (static_cast<u64>(new_capacity) * p.element_size > max_bytes) {
            new_capacity = static_cast<u32>(max_bytes / p.element_size);
            if (new_capacity < p.high_water || new_capacity == p.capacity)
                return false;
        }

        util::allocated_buffer new_buf = create_pool_buffer(p, new_capacity);
        if (!new_buf.buffer || !new_buf.allocation)
            return false;

        // Copy live data ([0, high_water)) on the GPU. Writes staged into the old buffer earlier in this recording land
        // first; writes staged into the new buffer later come after the copy
        if (p.high_water > 0 && p.buffer->buffer) {
            upload_transfer_barrier();
            vk::BufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = static_cast<vk::DeviceSize>(p.high_water) * p.element_size;
            upload_cmd().copyBuffer(p.buffer->buffer, new_buf.buffer, region);
            upload_transfer_barrier();
        }

        // In-flight frames (and the copy above) still use the old buffer: retire it instead of stalling the GPU
        std::swap(*p.buffer, new_buf);
        if (new_buf.buffer)
            retire_buffer(new_buf);

        p.capacity = new_capacity;
        return true;
    }

    // compaction ------------------------------------------------------------------------------------------------------

    bool shared_resource_manager::can_compact() const { return m_frames_since_compact >= 10; }


    bool shared_resource_manager::should_opportunistic_compact() const {
        GLT_PROFILE_SCOPE("shared_resource_manager::should_opportunistic_compact");

        // Worth it only when the pool is well above what compaction would size it to (live + slack, see try_compact).
        // Comparing against live alone would fire after every compaction in small scenes, where the slack dominates
        auto oversized = [](const linear_pool& p) {
            const u64 target = static_cast<u64>(p.live) + std::max<u64>(p.live / 2, p.min_headroom);
            return p.capacity > 0 && p.capacity > target * 2;
        };
        return oversized(m_vertex_pool) || oversized(m_index_pool);
    }


    // Moves every live range to the front of a fresh, right-sized buffer (GPU copies in the upload command buffer) and
    // retires the old buffers. No waitIdle and no CPU access to GPU memory: frames in flight keep drawing from the old
    // buffers with the old table entries; the rewritten table entries are an in-place write (upload_war_barrier)
    void shared_resource_manager::try_compact() {
        GLT_PROFILE_SCOPE("shared_resource_manager::try_compact");

        // Gather live mesh indices
        std::vector<u32> live;
        live.reserve(m_mesh_slots.size());
        for (u32 i = 0; i < m_mesh_slots.size(); ++i)
            if (m_mesh_slots[i].alive)
                live.push_back(i);

        const u64 max_bytes = m_physical_device.getProperties().limits.maxStorageBufferRange;

        auto compact_pool = [this, &live, max_bytes](linear_pool& p, auto get_offset, auto get_size, auto set_offset) {

            if (!p.buffer->buffer)
                return;

            u64 live_elements = 0;
            for (u32 idx : live)
                live_elements += get_size(idx);

            u64 capacity = live_elements + std::max<u64>(live_elements / 2, p.min_headroom);
            capacity = std::min<u64>(capacity, max_bytes / p.element_size);
            capacity = std::min<u64>(capacity, UINT32_MAX);
            if (capacity < live_elements || capacity == 0) {
                LOG(warn, "compaction of the {} pool skipped: {} live elements do not fit a buffer", p.name, live_elements)
                return;
            }

            util::allocated_buffer new_buf = create_pool_buffer(p, static_cast<u32>(capacity));
            if (!new_buf.buffer) {
                LOG(warn, "compaction of the {} pool skipped: no memory for the new buffer", p.name)
                return;
            }

            std::vector<u32> order = live;
            std::sort(order.begin(), order.end(), [&](u32 a, u32 b) { return get_offset(a) < get_offset(b); });

            std::vector<vk::BufferCopy> regions;
            regions.reserve(order.size());
            u32 cursor = 0;
            for (u32 idx : order) {
                const u32 size = get_size(idx);
                if (size > 0) {
                    vk::BufferCopy region{};
                    region.srcOffset = static_cast<vk::DeviceSize>(get_offset(idx)) * p.element_size;
                    region.dstOffset = static_cast<vk::DeviceSize>(cursor) * p.element_size;
                    region.size = static_cast<vk::DeviceSize>(size) * p.element_size;
                    regions.push_back(region);
                }
                set_offset(idx, cursor);
                cursor += size;
            }

            if (!regions.empty()) {
                upload_transfer_barrier();
                upload_cmd().copyBuffer(p.buffer->buffer, new_buf.buffer, regions);
                upload_transfer_barrier();
            }

            std::swap(*p.buffer, new_buf);
            retire_buffer(new_buf);

            p.capacity = static_cast<u32>(capacity);
            p.high_water = cursor;
            p.live = cursor;
            p.free_ranges.clear();
            p.generation++;             // frees still pending in the retire queue refer to the old layout
        };

        compact_pool(m_vertex_pool,
            [this](u32 i) { return m_mesh_slots[i].vertex_offset; },
            [this](u32 i) { return m_mesh_slots[i].vertex_count; },
            [this](u32 i, u32 o) { m_mesh_slots[i].vertex_offset = o; });

        compact_pool(m_index_pool,
            [this](u32 i) { return m_mesh_slots[i].index_offset; },
            [this](u32 i) { return m_mesh_slots[i].index_count; },
            [this](u32 i, u32 o) { m_mesh_slots[i].index_offset = o; });

        compact_pool(m_submesh_pool,
            [this](u32 i) { return m_mesh_slots[i].submesh_offset; },
            [this](u32 i) { return m_mesh_slots[i].submesh_count * m_mesh_slots[i].lod_count; },
            [this](u32 i, u32 o) { m_mesh_slots[i].submesh_offset = o; });

        compact_pool(m_cluster_pool,
            [this](u32 i) { return m_mesh_slots[i].cluster_offset; },
            [this](u32 i) { return m_mesh_slots[i].cluster_count; },
            [this](u32 i, u32 o) { m_mesh_slots[i].cluster_offset = o; });

        // Table entries whose offsets just changed (cluster index ranges are relative to the mesh's index offset: nothing
        // to patch there)
        for (u32 idx : live) {
            if (idx >= m_mesh_table_capacity)
                continue;
            const auto& slot = m_mesh_slots[idx];
            m_mesh_table[idx].vertex_offset = slot.vertex_offset;
            m_mesh_table[idx].index_offset = slot.index_offset;
            m_mesh_table[idx].first_submesh = slot.submesh_offset;
            m_mesh_cluster_table[idx].first_cluster = slot.cluster_offset;
            mark_mesh_table_dirty(idx, true);
        }

        m_frames_since_compact = 0;
    }

    // mesh allocation transaction -------------------------------------------------------------------------------------

    // [c]: virtual geometry cluster records (0 for meshes without them: a zero-sized allocation always succeeds)
    std::optional<shared_resource_manager::mesh_alloc> shared_resource_manager::allocate_mesh_ranges(u32 v, u32 i, u32 s, u32 c) {
        GLT_PROFILE_SCOPE("shared_resource_manager::allocate_mesh_ranges");

        auto try_all = [&]() -> std::optional<mesh_alloc> {
            auto v_off = pool_try_allocate(m_vertex_pool,  v);
            auto i_off = pool_try_allocate(m_index_pool,   i);
            auto s_off = pool_try_allocate(m_submesh_pool, s);
            auto c_off = pool_try_allocate(m_cluster_pool, c);
            if (v_off && i_off && s_off && c_off)
                return mesh_alloc{ *v_off, *i_off, *s_off, *c_off };
            if (v_off) pool_free(m_vertex_pool,  *v_off, v);
            if (i_off) pool_free(m_index_pool,   *i_off, i);
            if (s_off) pool_free(m_submesh_pool, *s_off, s);
            if (c_off) pool_free(m_cluster_pool, *c_off, c);
            return std::nullopt;
        };

        if (auto r = try_all())
            return r;

        // Consolidate before growing — this is the "free list cannot satisfy and tail would need to grow" path. Only when
        // the holes add up to the request in some pool that cannot fit it: otherwise compaction would just copy every pool
        // and growth would follow anyway (e.g. the first big load into near-empty pools)
        auto fragmented = [this](const linear_pool& p, u32 count) {
            if (count == 0 || pool_can_fit(p, count))
                return false;
            u64 holes = 0;
            for (const auto& r : p.free_ranges)
                holes += r.size;
            return holes + (static_cast<u64>(p.capacity) - p.high_water) >= count;
        };
        const bool compaction_helps = fragmented(m_vertex_pool, v) || fragmented(m_index_pool, i)
            || fragmented(m_submesh_pool, s) || fragmented(m_cluster_pool, c);

        if (compaction_helps && can_compact()) {
            try_compact();
            if (auto r = try_all())
                return r;
        }

        auto grow = [this](linear_pool& p, u32 min_additional) -> bool {
            if (!p.growable) return false;
            const u64 need = static_cast<u64>(p.high_water) + min_additional;
            if (need <= p.capacity) return true;
            const u64 new_cap = std::max<u64>(need,
                static_cast<u64>(p.capacity) + std::max<u64>(p.capacity / 2, p.min_headroom));
            return pool_resize(p, static_cast<u32>(std::min<u64>(new_cap, UINT32_MAX)));
        };

        if (!grow(m_vertex_pool, v)) return std::nullopt;
        if (!grow(m_index_pool,  i)) return std::nullopt;
        if (!grow(m_submesh_pool, s)) return std::nullopt;
        if (c > 0 && !grow(m_cluster_pool, c)) return std::nullopt;

        return try_all();
    }

    // buffer creation / destruction -----------------------------------------------------------------------------------

    void shared_resource_manager::create_mesh_buffers() {
        GLT_PROFILE_SCOPE("shared_resource_manager::create_mesh_buffers");

        init_pools();

        if (!ensure_mesh_table_capacity(0))
            LOG(fatal, "Failed to create the mesh tables")

        // Kick off the initial pool buffers
        if (!pool_resize(m_vertex_pool, VERTEX_HEADROOM_MIN))
            LOG(fatal, "Failed to create initial vertex buffer")
        if (!pool_resize(m_index_pool,  INDEX_HEADROOM_MIN))
            LOG(fatal, "Failed to create initial index buffer")
        if (!pool_resize(m_submesh_pool, SUBMESH_HEADROOM_MIN))
            LOG(fatal, "Failed to create initial submesh buffer")
        if (!pool_resize(m_cluster_pool, CLUSTER_HEADROOM_MIN))
            LOG(error, "Failed to create the cluster buffer: virtual geometry is unavailable, meshes use their LODs")
    }


    void shared_resource_manager::destroy_mesh_buffers() {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy_mesh_buffers");

        // Device is idle (destroy())
        for (util::allocated_buffer* buffer : { &m_vertex_buffer, &m_index_buffer, &m_mesh_data_buffer, &m_submesh_data_buffer,
            &m_mesh_cluster_buffer, &m_cluster_data_buffer })
            if (buffer->buffer)
                m_dev->destroy_buffer(*buffer);

        // Clear allocator state — buffers are gone
        m_vertex_pool = {};
        m_index_pool = {};
        m_submesh_pool = {};
        m_cluster_pool = {};

        m_mesh_table_capacity = 0;
        m_mesh_table.clear();
        m_mesh_cluster_table.clear();
        m_mesh_table_dirty.clear();
        m_mesh_table_dirty_flag.clear();
    }

    // mesh tables -----------------------------------------------------------------------------------------------------

    bool shared_resource_manager::ensure_mesh_table_capacity(u32 mesh_index) {

        if (mesh_index < m_mesh_table_capacity)
            return true;
        if (mesh_index >= MESH_SLOT_LIMIT) {
            LOG(error, "mesh slot {} exceeds the sanity limit of {} resident meshes", mesh_index, MESH_SLOT_LIMIT)
            return false;
        }

        u32 new_capacity = std::max(INITIAL_MESH_TABLE_CAPACITY, m_mesh_table_capacity);
        while (new_capacity <= mesh_index)
            new_capacity *= 2;

        const vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc
            | vk::BufferUsageFlagBits::eTransferDst;
        util::allocated_buffer mesh_table = m_dev->create_buffer(static_cast<vk::DeviceSize>(new_capacity) * sizeof(gpu_mesh_data), usage, 0);
        util::allocated_buffer cluster_table = m_dev->create_buffer(static_cast<vk::DeviceSize>(new_capacity) * sizeof(gpu_mesh_cluster_data), usage, 0);
        if (!mesh_table.buffer || !cluster_table.buffer) {
            m_dev->destroy_buffer(mesh_table);
            m_dev->destroy_buffer(cluster_table);
            LOG(error, "could not grow the mesh tables to {} entries", new_capacity)
            return false;
        }
        log_buffer_memory("mesh table", mesh_table);
        log_buffer_memory("mesh cluster table", cluster_table);

        // Zero the new tables; the live entries follow from the CPU copies at the next flush (emit_mesh_table_uploads)
        const vk::CommandBuffer cmd = upload_cmd();
        cmd.fillBuffer(mesh_table.buffer, 0, VK_WHOLE_SIZE, 0u);
        cmd.fillBuffer(cluster_table.buffer, 0, VK_WHOLE_SIZE, 0u);
        upload_transfer_barrier();

        // Frames in flight keep reading the old tables
        std::swap(m_mesh_data_buffer, mesh_table);
        std::swap(m_mesh_cluster_buffer, cluster_table);
        retire_buffer(mesh_table);
        retire_buffer(cluster_table);

        const u32 old_capacity = m_mesh_table_capacity;
        m_mesh_table.resize(new_capacity);
        m_mesh_cluster_table.resize(new_capacity);
        m_mesh_table_dirty_flag.resize(new_capacity, 0u);
        m_mesh_table_capacity = new_capacity;

        for (u32 i = 0; i < old_capacity && i < m_mesh_slots.size(); ++i)
            if (m_mesh_slots[i].alive)
                mark_mesh_table_dirty(i, false);        // new buffer: nobody reads it yet

        if (old_capacity > 0)
            LOG(trace, "mesh tables grown: {} -> {} entries", old_capacity, new_capacity)
        return true;
    }


    void shared_resource_manager::mark_mesh_table_dirty(u32 mesh_index, bool in_place) {

        if (mesh_index >= m_mesh_table_capacity)
            return;

        (void)upload_cmd();                             // the entry goes out with the next flush
        if (in_place)
            m_upload_war_pending = true;
        if (!m_mesh_table_dirty_flag[mesh_index]) {
            m_mesh_table_dirty_flag[mesh_index] = 1u;
            m_mesh_table_dirty.push_back(mesh_index);
        }
    }


    void shared_resource_manager::emit_mesh_table_uploads() {

        if (m_mesh_table_dirty.empty())
            return;

        // Rewriting entries that frames in flight may read: wait for them first
        if (m_upload_war_pending) {
            upload_war_barrier();
            m_upload_war_pending = false;
        }

        std::sort(m_mesh_table_dirty.begin(), m_mesh_table_dirty.end());

        // Entries that could not be staged stay dirty for the next flush: a skipped entry would leave the GPU table pointing
        // into retired or compacted ranges
        std::vector<u32> failed;

        // One copy per contiguous run of dirty entries, for each of the two tables
        size_t run_start = 0;
        while (run_start < m_mesh_table_dirty.size()) {

            size_t run_end = run_start + 1;
            while (run_end < m_mesh_table_dirty.size() && m_mesh_table_dirty[run_end] == m_mesh_table_dirty[run_end - 1] + 1)
                ++run_end;

            const u32 first = m_mesh_table_dirty[run_start];
            const u32 count = static_cast<u32>(run_end - run_start);

            std::byte* mesh_dst = stage(m_mesh_data_buffer, static_cast<vk::DeviceSize>(first) * sizeof(gpu_mesh_data),
                static_cast<vk::DeviceSize>(count) * sizeof(gpu_mesh_data));
            if (mesh_dst)
                std::memcpy(mesh_dst, &m_mesh_table[first], static_cast<size_t>(count) * sizeof(gpu_mesh_data));

            std::byte* cluster_dst = stage(m_mesh_cluster_buffer, static_cast<vk::DeviceSize>(first) * sizeof(gpu_mesh_cluster_data),
                static_cast<vk::DeviceSize>(count) * sizeof(gpu_mesh_cluster_data));
            if (cluster_dst)
                std::memcpy(cluster_dst, &m_mesh_cluster_table[first], static_cast<size_t>(count) * sizeof(gpu_mesh_cluster_data));

            if (!mesh_dst || !cluster_dst)
                failed.insert(failed.end(), m_mesh_table_dirty.begin() + static_cast<std::ptrdiff_t>(run_start),
                    m_mesh_table_dirty.begin() + static_cast<std::ptrdiff_t>(run_end));

            run_start = run_end;
        }

        for (u32 index : m_mesh_table_dirty)
            m_mesh_table_dirty_flag[index] = 0u;
        m_mesh_table_dirty.clear();
        for (u32 index : failed) {
            m_mesh_table_dirty_flag[index] = 1u;
            m_mesh_table_dirty.push_back(index);
        }
    }

    // mesh upload -----------------------------------------------------------------------------------------------------

    mesh_slot* shared_resource_manager::find_slot(GLT::asset::handle handle) {

        for (auto& s : m_mesh_slots)
            if (s.alive && s.asset == handle)
                return &s;
        return nullptr;
    }


    const mesh_slot* shared_resource_manager::find_slot(GLT::asset::handle handle) const {

        return const_cast<shared_resource_manager*>(this)->find_slot(handle);
    }


    void shared_resource_manager::retire_slot_ranges(const mesh_slot& slot) {

        retire([this,
            vertex_generation = m_vertex_pool.generation, vertex_offset = slot.vertex_offset, vertex_count = slot.vertex_count,
            index_generation = m_index_pool.generation, index_offset = slot.index_offset, index_count = slot.index_count,
            submesh_generation = m_submesh_pool.generation, submesh_offset = slot.submesh_offset,
            submesh_count = slot.submesh_count * slot.lod_count,
            cluster_generation = m_cluster_pool.generation, cluster_offset = slot.cluster_offset, cluster_count = slot.cluster_count]() {
            pool_free_deferred(m_vertex_pool, vertex_generation, vertex_offset, vertex_count);
            pool_free_deferred(m_index_pool, index_generation, index_offset, index_count);
            pool_free_deferred(m_submesh_pool, submesh_generation, submesh_offset, submesh_count);
            pool_free_deferred(m_cluster_pool, cluster_generation, cluster_offset, cluster_count);
        });
    }


    void shared_resource_manager::upload_mesh_slice(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh) {
        GLT_PROFILE_SCOPE("shared_resource_manager::upload_mesh_slice");

        using GLT::asset::mesh::vertex;

        // NOTE: the stage() pointers point into staging memory that may be uncached / write-combined: write only, never read

        // [vertices ++ lod_vertices ++ cluster_vertices]: the two optional parts hold the recomputed normals of the LOD
        // levels / of the simplified clusters. Together they fill exactly slot.vertex_count vertices
        if (auto* dst = reinterpret_cast<vertex*>(stage(m_vertex_buffer, static_cast<vk::DeviceSize>(slot.vertex_offset) * sizeof(vertex),
            static_cast<vk::DeviceSize>(slot.vertex_count) * sizeof(vertex)))) {

            const size_t source_vertices = std::min<size_t>(mesh.vertices.size(), slot.vertex_count);
            std::memcpy(dst, mesh.vertices.data(), source_vertices * sizeof(vertex));
            const size_t lod_vertex_end = slot.cluster_vertex_count > 0 ? slot.cluster_vertex_base : slot.vertex_count;
            if (lod_vertex_end > mesh.vertices.size())
                std::memcpy(dst + mesh.vertices.size(), mesh.lod_vertices.data(),
                    (lod_vertex_end - mesh.vertices.size()) * sizeof(vertex));
            if (slot.cluster_vertex_count > 0)
                std::memcpy(dst + slot.cluster_vertex_base, mesh.cluster_vertices.data(),
                    static_cast<size_t>(slot.cluster_vertex_count) * sizeof(vertex));
        }

        // [indices ++ lod_indices ++ cluster_indices]: the LOD ranges address the first two as one stream (asset/mesh.h
        // lod_range), the virtual geometry clusters the third part (starting at slot.cluster_index_base)
        if (u32* dst = reinterpret_cast<u32*>(stage(m_index_buffer, static_cast<vk::DeviceSize>(slot.index_offset) * sizeof(u32),
            static_cast<vk::DeviceSize>(slot.index_count) * sizeof(u32)))) {

            const size_t base_count = mesh.indices.size();
            const size_t lod_end = slot.cluster_count > 0 ? slot.cluster_index_base : slot.index_count;
            const size_t lod_index_count = (slot.lod_count > 1 && lod_end > base_count) ? lod_end - base_count : 0;
            const size_t cluster_index_count = slot.cluster_count > 0 ? slot.index_count - slot.cluster_index_base : 0;

            std::memcpy(dst, mesh.indices.data(), base_count * sizeof(u32));
            if (lod_index_count > 0)
                std::memcpy(dst + base_count, mesh.lod_indices.data(), lod_index_count * sizeof(u32));
            // Cluster indices address [vertices ++ cluster_vertices] in the asset. In the vertex range the cluster
            // vertices sit behind the LOD vertices, so indices at or above the source vertex count are moved up
            if (cluster_index_count > 0) {
                const u32 source_vertices = static_cast<u32>(mesh.vertices.size());
                const u32 shift = slot.cluster_vertex_base - source_vertices;
                u32* out = dst + slot.cluster_index_base;
                for (size_t i = 0; i < cluster_index_count; ++i) {
                    const u32 index = mesh.cluster_indices[i];
                    out[i] = index < source_vertices ? index : index + shift;
                }
            }

            // Never let the GPU fetch past this mesh's vertices (another mesh's data / uninitialised pool memory: huge
            // NaN-shaded triangles). Out-of-range triangles become degenerate. Checks the CPU copy and only patches the
            // staged triangles that are bad (writes only)
            u32 bad_triangles = 0;
            const u32 checked_count = static_cast<u32>(base_count + lod_index_count);   // cluster indices: checked below
            const u32 lod_vertex_limit = slot.cluster_vertex_count > 0 ? slot.cluster_vertex_base : slot.vertex_count;  // [vertices ++ lod_vertices]
            auto source_index = [&](u32 i) { return i < base_count ? mesh.indices[i] : mesh.lod_indices[i - base_count]; };
            for (u32 t = 0; t + 2 < checked_count; t += 3) {
                if (source_index(t) < lod_vertex_limit && source_index(t + 1) < lod_vertex_limit && source_index(t + 2) < lod_vertex_limit)
                    continue;
                dst[t] = dst[t + 1] = dst[t + 2] = 0u;
                ++bad_triangles;
            }

            // Cluster triangles use the source vertices and the cluster vertices (asset/mesh_cluster.h). Clusters are whole
            // triangle lists stored back to back, so triples of the stream are triangles here too
            const size_t cluster_stream_vertices = mesh.vertices.size() + slot.cluster_vertex_count;
            for (size_t t = 0; t + 2 < cluster_index_count; t += 3) {
                const u32* tri = &mesh.cluster_indices[t];
                if (tri[0] < cluster_stream_vertices && tri[1] < cluster_stream_vertices && tri[2] < cluster_stream_vertices)
                    continue;
                u32* out = dst + slot.cluster_index_base + t;
                out[0] = out[1] = out[2] = 0u;
                ++bad_triangles;
            }
            if (bad_triangles > 0)
                LOG(warn, "mesh slot: {} triangle(s) with out-of-range vertex indices dropped (corrupt mesh data)", bad_triangles);
        }

        // Virtual geometry cluster records, as they are in the asset
        if (slot.cluster_count > 0 && m_cluster_data_buffer.buffer)
            if (std::byte* dst = stage(m_cluster_data_buffer, static_cast<vk::DeviceSize>(slot.cluster_offset) * sizeof(GLT::asset::mesh::cluster),
                static_cast<vk::DeviceSize>(slot.cluster_count) * sizeof(GLT::asset::mesh::cluster)))
                std::memcpy(dst, mesh.clusters.data(), static_cast<size_t>(slot.cluster_count) * sizeof(GLT::asset::mesh::cluster));
    }


    void shared_resource_manager::pack_mesh_into_ssbo(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh, bool in_place) {
        GLT_PROFILE_SCOPE("shared_resource_manager::pack_mesh_into_ssbo");

        const u32 mesh_index = static_cast<u32>(&slot - m_mesh_slots.data());
        VALIDATE(mesh_index < m_mesh_table_capacity, return, "", "Mesh table capacity exceeded")

        // Culling box. Procedural meshes: the store's bounds (validated, cover every LOD's vertices, or what the generator
        // passed). Assets: LOD 0's vertices, as before
        glm::vec3 local_min(std::numeric_limits<f32>::max());
        glm::vec3 local_max(std::numeric_limits<f32>::lowest());
        if (mesh.asset_type == GLT::asset::core_types::procedural_mesh) {
            local_min = mesh.bounds.min;
            local_max = mesh.bounds.max;
        } else {
            for (const auto& v : mesh.vertices) {
                local_min = glm::min(local_min, v.position);
                local_max = glm::max(local_max, v.position);
            }
        }

        const u32 submesh_count = slot.submesh_count;
        const u32 lod_count = std::max(slot.lod_count, 1u);

        // Acquire the new materials before releasing the old ones (a refresh usually keeps them: no unload/reload)
        std::vector<u32> material_slots(submesh_count);
        for (u32 i = 0; i < submesh_count; ++i)
            material_slots[i] = acquire_material(mesh.submeshes[i].material);
        for (u32 old_slot : slot.material_slots)
            release_material(old_slot);
        slot.material_slots = std::move(material_slots);

        // [lod][submesh]. Ranges outside the uploaded index count (corrupt asset) are dropped, never drawn
        slot.submesh_ranges.assign(static_cast<size_t>(submesh_count) * lod_count, glm::uvec2(0u));
        for (u32 level = 0; level < lod_count; ++level)
            for (u32 i = 0; i < submesh_count; ++i) {
                const GLT::asset::mesh::lod_range r = (level == 0)
                    ? GLT::asset::mesh::lod_range{ mesh.submeshes[i].first_index, mesh.submeshes[i].index_count }
                    : mesh.range(level, i);
                const u32 lod_stream_count = slot.cluster_count > 0 ? slot.cluster_index_base : slot.index_count;
                const bool valid = static_cast<u64>(r.first_index) + r.index_count <= lod_stream_count;
                slot.submesh_ranges[static_cast<size_t>(level) * submesh_count + i] =
                    valid ? glm::uvec2(r.first_index, r.index_count) : glm::uvec2(0u);
            }

        // Submesh table — at the pool-allocated offset (a fresh range: never read by a frame in flight)
        const u32 submesh_entries = submesh_count * lod_count;
        if (auto* sub = reinterpret_cast<gpu_submesh_data*>(stage(m_submesh_data_buffer,
            static_cast<vk::DeviceSize>(slot.submesh_offset) * sizeof(gpu_submesh_data),
            static_cast<vk::DeviceSize>(submesh_entries) * sizeof(gpu_submesh_data)))) {
            for (u32 i = 0; i < submesh_entries; ++i) {
                gpu_submesh_data entry{};
                entry.first_index = slot.submesh_ranges[i].x;
                entry.index_count = slot.submesh_ranges[i].y;
                std::memcpy(sub + i, &entry, sizeof(entry));
            }
        }

        // Mesh table
        {
            gpu_mesh_data md{};
            md.local_min = local_min;
            md.local_max = local_max;
            md.first_submesh = slot.submesh_offset;
            md.submesh_count = submesh_count;
            md.vertex_offset = slot.vertex_offset;
            md.index_offset = slot.index_offset;
            md.lod_count = lod_count;
            slot.lod_screen = lod_screen_sizes(mesh, lod_count);
            std::copy(slot.lod_screen.begin(), slot.lod_screen.end(), md.lod_screen);

            m_mesh_table[mesh_index] = md;
            mark_mesh_table_dirty(mesh_index, in_place);
        }

        // Virtual geometry: draw bound per submesh (CPU, sizes the indirect ranges) + table entry (GPU). Written for
        // every mesh, so a slot that lost its clusters cannot keep a stale entry
        slot.cluster_max_visible.clear();
        if (slot.cluster_count > 0) {

            // Never less than the source (level 0) clusters of the submesh, never more than all of its clusters, whatever
            // the asset says: too small would drop cluster draws (holes), too large only costs indirect-buffer memory
            std::vector<u32> source_clusters(submesh_count, 0u);
            std::vector<u32> all_clusters(submesh_count, 0u);
            for (u32 c = 0; c < slot.cluster_count && c < mesh.clusters.size(); ++c) {
                const GLT::asset::mesh::cluster& record = mesh.clusters[c];
                if (record.submesh >= submesh_count)
                    continue;
                all_clusters[record.submesh]++;
                if (record.depth == 0)
                    source_clusters[record.submesh]++;
            }

            slot.cluster_max_visible.assign(submesh_count, 0u);
            for (u32 i = 0; i < submesh_count; ++i) {
                const u32 stored = i < mesh.cluster_submeshes.size() ? mesh.cluster_submeshes[i].max_visible : 0u;
                slot.cluster_max_visible[i] = std::min(std::max(stored, source_clusters[i]), all_clusters[i]);
            }
        }
        write_mesh_cluster_entry(mesh_index, slot, mesh.virtual_geometry.error_scale, in_place);
    }


    void shared_resource_manager::write_mesh_cluster_entry(u32 mesh_index, const mesh_slot& slot, f32 error_scale, bool in_place) {

        if (mesh_index >= m_mesh_table_capacity)
            return;

        gpu_mesh_cluster_data entry{};
        entry.first_cluster = slot.cluster_offset;
        entry.cluster_count = slot.cluster_count;
        entry.index_base = slot.cluster_index_base;
        entry.error_scale = std::isfinite(error_scale) ? std::clamp(error_scale, 0.05f, 20.0f) : 1.0f;

        m_mesh_cluster_table[mesh_index] = entry;
        mark_mesh_table_dirty(mesh_index, in_place);
    }


    shared_mesh_id shared_resource_manager::load_mesh_internal(GLT::asset::handle handle) {
        GLT_PROFILE_SCOPE("shared_resource_manager::load_mesh_internal");

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return INVALID_SHARED_MESH;
        if (find_slot(handle))
            return INVALID_SHARED_MESH;
        if (m_failed_mesh_loads.contains(handle))
            return INVALID_SHARED_MESH;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(handle);
        if (!mesh) {
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }

        const shared_mesh_id id = upload_new_slot(*mesh, handle);
        if (id == INVALID_SHARED_MESH) {
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }

        mesh_slot& slot = m_mesh_slots[id];
        slot.source = mesh;
        slot.source_revision = mesh->revision;
        slot.source_geometry = geometry_signature(*mesh);

        m_mesh_id_by_asset[handle] = id;
        return id;
    }


    shared_mesh_id shared_resource_manager::upload_new_slot(const GLT::asset::mesh::mesh_asset& mesh, GLT::asset::handle key) {
        GLT_PROFILE_SCOPE("shared_resource_manager::upload_new_slot");

        if (mesh.vertices.size() > UINT32_MAX || mesh.indices.size() > UINT32_MAX)
            return INVALID_SHARED_MESH;

        mesh_upload_layout layout = mesh_layout_of(mesh);
        const u32 submesh_count = layout.submesh_count;

        // Reserve a slot index up front. Recycled slots only reach m_free_slots after the GPU finished with them (see
        // release_mesh), so their table entries can be rewritten without waiting
        u32 slot_idx;
        if (!m_free_slots.empty()) {
            slot_idx = m_free_slots.back();
            m_free_slots.pop_back();
        } else {
            if (m_mesh_slots.size() >= MESH_SLOT_LIMIT)
                return INVALID_SHARED_MESH;
            slot_idx = static_cast<u32>(m_mesh_slots.size());
            m_mesh_slots.emplace_back();
        }

        if (!ensure_mesh_table_capacity(slot_idx)) {
            m_mesh_slots[slot_idx] = {};
            m_free_slots.push_back(slot_idx);
            return INVALID_SHARED_MESH;
        }

        // Allocate buffer ranges (may compact / grow / fail). When the pools are full the optional parts are given up
        // one by one: first the virtual geometry clusters, then the LOD levels (LOD 0 only)
        auto alloc = allocate_mesh_ranges(layout.vertex_count, layout.index_count, submesh_count * layout.lod_count, layout.cluster_count);
        while (!alloc && reduce_layout(layout, mesh))
            alloc = allocate_mesh_ranges(layout.vertex_count, layout.index_count, submesh_count * layout.lod_count, layout.cluster_count);
        if (!alloc) {
            // Return the slot — no ranges were allocated
            m_mesh_slots[slot_idx] = {};
            m_free_slots.push_back(slot_idx);
            return INVALID_SHARED_MESH;
        }

        mesh_slot& slot = m_mesh_slots[slot_idx];
        slot = {};
        slot.asset = key;
        slot.vertex_offset = alloc->vertex_offset;
        slot.vertex_count = layout.vertex_count;
        slot.index_offset = alloc->index_offset;
        slot.index_count = layout.index_count;
        slot.submesh_offset = alloc->submesh_offset;
        slot.submesh_count = submesh_count;
        slot.lod_count = layout.lod_count;
        slot.cluster_offset = alloc->cluster_offset;
        slot.cluster_count = layout.cluster_count;
        slot.cluster_index_base = layout.cluster_index_base;
        slot.cluster_vertex_base = layout.cluster_vertex_base;
        slot.cluster_vertex_count = layout.cluster_vertex_count;
        slot.alive = true;
        slot.local_min = mesh.bounds.min;
        slot.local_max = mesh.bounds.max;
        slot.ref_count = 1;

        upload_mesh_slice(slot, mesh);
        pack_mesh_into_ssbo(slot, mesh, false);
        return slot_idx;
    }


    bool shared_resource_manager::reupload_slot(shared_mesh_id id, const GLT::asset::mesh::mesh_asset& mesh) {
        GLT_PROFILE_SCOPE("shared_resource_manager::reupload_slot");

        if (id >= m_mesh_slots.size() || !m_mesh_slots[id].alive)
            return false;
        if (mesh.vertices.size() > UINT32_MAX || mesh.indices.size() > UINT32_MAX)
            return false;

        mesh_upload_layout layout = mesh_layout_of(mesh);
        auto alloc = allocate_mesh_ranges(layout.vertex_count, layout.index_count, layout.submesh_count * layout.lod_count, layout.cluster_count);
        while (!alloc && reduce_layout(layout, mesh))
            alloc = allocate_mesh_ranges(layout.vertex_count, layout.index_count, layout.submesh_count * layout.lod_count, layout.cluster_count);

        mesh_slot& slot = m_mesh_slots[id];        // after allocate: compaction may have moved this slot's ranges
        if (!alloc) {
            LOG(warn, "reupload_slot: no room to re-upload mesh slot {}, keeping the previous geometry", id);
            return false;
        }

        // In-flight frames still draw from the old ranges: free them once the GPU is done. The new data goes to fresh
        // ranges; the table entry is rewritten in place, behind a wait for the frames that read the old one
        retire_slot_ranges(slot);

        slot.vertex_offset = alloc->vertex_offset;
        slot.vertex_count = layout.vertex_count;
        slot.index_offset = alloc->index_offset;
        slot.index_count = layout.index_count;
        slot.submesh_offset = alloc->submesh_offset;
        slot.submesh_count = layout.submesh_count;
        slot.lod_count = layout.lod_count;
        slot.cluster_offset = alloc->cluster_offset;
        slot.cluster_count = layout.cluster_count;
        slot.cluster_index_base = layout.cluster_index_base;
        slot.cluster_vertex_base = layout.cluster_vertex_base;
        slot.cluster_vertex_count = layout.cluster_vertex_count;
        slot.local_min = mesh.bounds.min;
        slot.local_max = mesh.bounds.max;

        upload_mesh_slice(slot, mesh);
        pack_mesh_into_ssbo(slot, mesh, true);
        LOG(trace, "reupload_slot: slot {} re-uploaded ({} LOD levels, {} clusters, {} indices)", id, slot.lod_count, slot.cluster_count,
            slot.index_count);
        return true;
    }


    shared_resource_manager::mesh_upload_layout shared_resource_manager::mesh_layout_of(const GLT::asset::mesh::mesh_asset& mesh) {

        mesh_upload_layout l{};
        l.vertex_count = static_cast<u32>(mesh.vertices.size());
        l.index_count = static_cast<u32>(mesh.indices.size());
        l.submesh_count = static_cast<u32>(std::min<size_t>(mesh.submeshes.size(), MAX_SUBMESHES_PER_MESH));
        l.lod_count = std::min(mesh.lod_count(), GLT::asset::mesh::MAX_LODS);
        if (l.lod_count > 1) {
            const u64 total = static_cast<u64>(mesh.indices.size()) + mesh.lod_indices.size();
            const u64 vertices = static_cast<u64>(mesh.vertices.size()) + mesh.lod_vertices.size();
            if (total > UINT32_MAX || vertices > UINT32_MAX)
                l.lod_count = 1;
            else {
                l.index_count = static_cast<u32>(total);
                l.vertex_count = static_cast<u32>(vertices);       // [vertices ++ lod_vertices] (recomputed LOD normals)
            }
        }

        // Virtual geometry: cluster triangle lists behind everything else. Only for meshes that opted in, and only when
        // every submesh is addressable (the instance material list holds MAX_SUBMESHES_PER_MESH entries at most)
        if (mesh.uses_virtual_geometry() && mesh.submeshes.size() <= MAX_SUBMESHES_PER_MESH) {
            const u64 total = static_cast<u64>(l.index_count) + mesh.cluster_indices.size();
            const u64 vertices = static_cast<u64>(l.vertex_count) + mesh.cluster_vertices.size();
            if (total <= UINT32_MAX && vertices <= UINT32_MAX && mesh.clusters.size() <= UINT32_MAX) {
                l.cluster_count = static_cast<u32>(mesh.clusters.size());
                l.cluster_index_base = l.index_count;
                l.index_count = static_cast<u32>(total);
                l.cluster_vertex_base = l.vertex_count;             // behind [vertices ++ lod_vertices]
                l.cluster_vertex_count = static_cast<u32>(mesh.cluster_vertices.size());
                l.vertex_count = static_cast<u32>(vertices);
            }
        }
        return l;
    }


    bool shared_resource_manager::reduce_layout(mesh_upload_layout& layout, const GLT::asset::mesh::mesh_asset& mesh) {

        if (layout.cluster_count > 0) {
            layout.index_count = layout.cluster_index_base;         // drop the cluster triangle lists
            layout.vertex_count -= layout.cluster_vertex_count;     // ... and their vertices (the last part of the range)
            layout.cluster_count = 0;
            layout.cluster_index_base = 0;
            layout.cluster_vertex_base = 0;
            layout.cluster_vertex_count = 0;
            return true;
        }
        if (layout.lod_count > 1) {
            layout.lod_count = 1;
            layout.index_count = static_cast<u32>(mesh.indices.size());
            layout.vertex_count = static_cast<u32>(mesh.vertices.size());
            return true;
        }
        return false;
    }


    u64 shared_resource_manager::geometry_signature(const GLT::asset::mesh::mesh_asset& mesh) {

        u64 h = 0xcbf29ce484222325ull;
        auto mix = [&h](u64 v) { h = (h ^ v) * 0x100000001b3ull; h ^= h >> 29; };
        mix(reinterpret_cast<std::uintptr_t>(mesh.vertices.data()));    mix(mesh.vertices.size());
        mix(reinterpret_cast<std::uintptr_t>(mesh.indices.data()));     mix(mesh.indices.size());
        mix(reinterpret_cast<std::uintptr_t>(mesh.submeshes.data()));   mix(mesh.submeshes.size());
        mix(reinterpret_cast<std::uintptr_t>(mesh.lod_ranges.data()));  mix(mesh.lod_ranges.size());
        mix(reinterpret_cast<std::uintptr_t>(mesh.lod_indices.data())); mix(mesh.lod_indices.size());
        mix(mesh.lods.size());
        mix(reinterpret_cast<std::uintptr_t>(mesh.lod_vertices.data())); mix(mesh.lod_vertices.size());
        // Virtual geometry: the cluster arrays, and whether the mesh uses them (switching the path on / off uploads or
        // drops the cluster data)
        mix(reinterpret_cast<std::uintptr_t>(mesh.clusters.data()));        mix(mesh.clusters.size());
        mix(reinterpret_cast<std::uintptr_t>(mesh.cluster_indices.data())); mix(mesh.cluster_indices.size());
        mix(reinterpret_cast<std::uintptr_t>(mesh.cluster_vertices.data())); mix(mesh.cluster_vertices.size());
        mix(mesh.uses_virtual_geometry() ? 1u : 0u);
        return h;
    }


    std::array<f32, 8> shared_resource_manager::lod_screen_sizes(const GLT::asset::mesh::mesh_asset& mesh, u32 lod_count) {

        // [0] cull size, [n] = size below which LOD n is used. Unused levels get 0 (never selected)
        std::array<f32, 8> sizes{};
        sizes[0] = std::isfinite(mesh.lod.cull_screen_size) ? std::max(mesh.lod.cull_screen_size, 0.0f) : 0.0f;
        for (u32 level = 1; level < std::min<u32>(lod_count, 8u); ++level) {
            const f32 s = mesh.lod.levels[level].screen_size;
            sizes[level] = std::isfinite(s) ? std::max(s, 0.0f) : 0.0f;
        }
        return sizes;
    }

}

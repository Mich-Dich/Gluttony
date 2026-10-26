#include "util/pch.h"
#include "util/util_pch.h"
#include "shared_resource_manager.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {

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

    // CLASS PUBLIC ====================================================================================================

    std::expected<shared_mesh_id, upload_error> shared_resource_manager::ensure_mesh(GLT::asset::handle handle) {

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

        // v1: ref counting handled at the scene-renderer level
    }


    void shared_resource_manager::release_mesh(shared_mesh_id id) {

        if (id >= m_mesh_slots.size() || !m_mesh_slots[id].alive)
            return;

        if (--m_mesh_slots[id].ref_count > 0)
            return;

        mesh_slot& slot = m_mesh_slots[id];

        // Return ranges to their pools (CPU-only, does not touch GPU memory)
        pool_free(m_vertex_pool, slot.vertex_offset, slot.vertex_count);
        pool_free(m_index_pool, slot.index_offset, slot.index_count);
        pool_free(m_submesh_pool, slot.submesh_offset, slot.submesh_count);

        // Release material slots owned by the submeshes
        for (u32 ms : slot.material_slots)
            release_material(ms);
        slot.material_slots.clear();

        // NOTE: SSBO entry is intentionally NOT zeroed here. In-flight frames may still reference this slot
        // with the old instance transforms; leaving stale data lets them render the old mesh unchanged.
        // load_mesh_internal() waits for the GPU before overwriting a recycled slot.

        m_mesh_id_by_asset.erase(slot.asset);
        slot = {};
        m_free_slots.push_back(id);
    }


    bool shared_resource_manager::is_mesh_ready(shared_mesh_id id) const { return id < m_mesh_slots.size() && m_mesh_slots[id].alive; }


    const mesh_slot* shared_resource_manager::get_mesh(shared_mesh_id id) const {
        if (id >= m_mesh_slots.size() || !m_mesh_slots[id].alive)
            return nullptr;
        return &m_mesh_slots[id];
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void shared_resource_manager::init_pools() {

        m_vertex_pool = {
            .buffer = &m_vertex_buffer,
            .element_size = sizeof(GLT::asset::mesh::vertex),
            .min_headroom = VERTEX_HEADROOM_MIN,
            .growable = true,
        };

        m_index_pool = {
            .buffer = &m_index_buffer,
            .element_size = sizeof(u32),
            .min_headroom = INDEX_HEADROOM_MIN,
            .growable = true,
        };

        m_submesh_pool = {
            .buffer = &m_submesh_data_buffer,
            .element_size = sizeof(gpu_submesh_data),
            .capacity = MAX_SUBMESHES,   // fixed size
            .min_headroom = 0,
            .growable = false,
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


    bool shared_resource_manager::pool_resize(linear_pool& p, u32 new_capacity) {

        if (new_capacity == p.capacity)
            return true;
        if (!p.growable && new_capacity > p.capacity)
            return false;
        if (new_capacity < p.high_water)
            return false;

        // Clamp to device limit
        const u64 max_bytes = m_physical_device.getProperties().limits.maxStorageBufferRange;
        if (static_cast<u64>(new_capacity) * p.element_size > max_bytes) {
            new_capacity = static_cast<u32>(max_bytes / p.element_size);
            if (new_capacity < p.high_water)
                return false;
        }

        // Usage bits depend on which pool — only three call sites exist
        vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eStorageBuffer;
        if (&p == &m_vertex_pool)
            usage |= vk::BufferUsageFlagBits::eVertexBuffer;
        else if (&p == &m_index_pool)
            usage |= vk::BufferUsageFlagBits::eIndexBuffer;

        util::allocated_buffer new_buf = m_dev->create_buffer(static_cast<vk::DeviceSize>(new_capacity) * p.element_size,
            usage, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        if (!new_buf.buffer || !new_buf.allocation)
            return false;

        // Copy live data ([0, high_water))
        if (p.high_water > 0 && p.buffer->buffer) {
            const void* src = m_dev->map_buffer(*p.buffer);
            void* dst = m_dev->map_buffer(new_buf);
            std::memcpy(dst, src, static_cast<size_t>(p.high_water) * p.element_size);
            m_dev->unmap_buffer(new_buf);
            m_dev->unmap_buffer(*p.buffer);
        }

        m_device.waitIdle();
        std::swap(*p.buffer, new_buf);
        if (new_buf.buffer)
            m_dev->destroy_buffer(new_buf);

        p.capacity = new_capacity;
        return true;
    }

    // compaction ------------------------------------------------------------------------------------------------------

    bool shared_resource_manager::can_compact() const { return m_frames_since_compact >= 10; }


    bool shared_resource_manager::should_opportunistic_compact() const {

        const bool v = m_vertex_pool.capacity > 0 && static_cast<u64>(m_vertex_pool.live) * 2 < m_vertex_pool.capacity;
        const bool i = m_index_pool.capacity > 0 && static_cast<u64>(m_index_pool.live) * 2 < m_index_pool.capacity;
        return v || i;
    }


    void shared_resource_manager::try_compact() {

        m_device.waitIdle();

        // Gather live mesh indices
        std::vector<u32> live;
        live.reserve(m_mesh_slots.size());
        for (u32 i = 0; i < m_mesh_slots.size(); ++i)
            if (m_mesh_slots[i].alive)
                live.push_back(i);

        // Compact one pool: sort by current offset, memmove each live range to a contiguous span from 0
        auto compact_pool = [this, &live](linear_pool& p,
            auto get_offset, auto get_size, auto set_offset) {

            std::vector<u32> order = live;
            std::sort(order.begin(), order.end(),
                [&](u32 a, u32 b) { return get_offset(a) < get_offset(b); });

            void* mapped = m_dev->map_buffer(*p.buffer);
            u32 cursor = 0;

            for (u32 idx : order) {
                const u32 old_off = get_offset(idx);
                const u32 sz = get_size(idx);
                if (old_off != cursor && sz > 0) {
                    std::memmove(
                        static_cast<std::byte*>(mapped) + static_cast<size_t>(cursor)  * p.element_size,
                        static_cast<std::byte*>(mapped) + static_cast<size_t>(old_off) * p.element_size,
                        static_cast<size_t>(sz) * p.element_size);
                    set_offset(idx, cursor);
                }
                cursor += sz;
            }
            m_dev->unmap_buffer(*p.buffer);

            p.high_water = cursor;
            p.free_ranges.clear();
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
            [this](u32 i) { return m_mesh_slots[i].submesh_count; },
            [this](u32 i, u32 o) { m_mesh_slots[i].submesh_offset = o; });

        // Rewrite the SSBO entries whose offsets just changed
        if (!live.empty()) {
            auto* m = static_cast<gpu_mesh_data*>(m_dev->map_buffer(m_mesh_data_buffer));
            for (u32 idx : live) {
                const auto& slot = m_mesh_slots[idx];
                m[idx].vertex_offset = slot.vertex_offset;
                m[idx].index_offset = slot.index_offset;
                m[idx].first_submesh = slot.submesh_offset;
            }
            m_dev->unmap_buffer(m_mesh_data_buffer);
        }

        m_frames_since_compact = 0;
    }


    void shared_resource_manager::shrink_pools() {

        // Only the growable pools ever need shrinking. After compaction, high_water == live, so we target
        // live + max(live/2, headroom) — matches the growth step, so shrink is stable (won't immediately
        // re-grow on the next allocation).
        for (auto* p : { &m_vertex_pool, &m_index_pool }) {

            if (!p->growable)
                continue;

            const u64 target64 = static_cast<u64>(p->high_water) +
                std::max<u64>(p->high_water / 2, p->min_headroom);

            if (target64 >= p->capacity)
                continue;

            if (!pool_resize(*p, static_cast<u32>(target64)))
                LOG(warn, "pool shrink failed");
        }
    }

    // mesh allocation transaction -------------------------------------------------------------------------------------

    std::optional<shared_resource_manager::mesh_alloc> shared_resource_manager::allocate_mesh_ranges(u32 v, u32 i, u32 s) {

        auto try_all = [&]() -> std::optional<mesh_alloc> {
            auto v_off = pool_try_allocate(m_vertex_pool,  v);
            auto i_off = pool_try_allocate(m_index_pool,   i);
            auto s_off = pool_try_allocate(m_submesh_pool, s);
            if (v_off && i_off && s_off)
                return mesh_alloc{ *v_off, *i_off, *s_off };
            if (v_off) pool_free(m_vertex_pool,  *v_off, v);
            if (i_off) pool_free(m_index_pool,   *i_off, i);
            if (s_off) pool_free(m_submesh_pool, *s_off, s);
            return std::nullopt;
        };

        if (auto r = try_all())
            return r;

        // Consolidate before growing — this is the "free list cannot satisfy and tail would need to grow" path
        if (can_compact()) {
            try_compact();
            if (auto r = try_all())
                return r;
        }

        // Growth (only vertex and index are growable — submesh is capped at MAX_SUBMESHES)
        auto grow = [this](linear_pool& p, u32 min_additional) -> bool {
            if (!p.growable) return false;
            const u64 need = static_cast<u64>(p.high_water) + min_additional;
            if (need <= p.capacity) return true;
            const u64 new_cap = std::max<u64>(need,
                static_cast<u64>(p.capacity) + std::max<u64>(p.capacity / 2, p.min_headroom));
            return pool_resize(p, static_cast<u32>(new_cap));
        };

        if (!grow(m_vertex_pool, v)) return std::nullopt;
        if (!grow(m_index_pool,  i)) return std::nullopt;

        return try_all();
    }

    // buffer creation / destruction -----------------------------------------------------------------------------------

    void shared_resource_manager::create_mesh_buffers() {

        m_mesh_data_buffer = m_dev->create_buffer(MAX_MESHES * sizeof(gpu_mesh_data),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        m_submesh_data_buffer = m_dev->create_buffer(MAX_SUBMESHES * sizeof(gpu_submesh_data),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        std::memset(m_dev->map_buffer(m_mesh_data_buffer),    0, MAX_MESHES   * sizeof(gpu_mesh_data));
        m_dev->unmap_buffer(m_mesh_data_buffer);
        std::memset(m_dev->map_buffer(m_submesh_data_buffer), 0, MAX_SUBMESHES * sizeof(gpu_submesh_data));
        m_dev->unmap_buffer(m_submesh_data_buffer);

        init_pools();

        // Kick off the initial vertex / index buffers
        if (!pool_resize(m_vertex_pool, VERTEX_HEADROOM_MIN))
            LOG(fatal, "Failed to create initial vertex buffer");
        if (!pool_resize(m_index_pool,  INDEX_HEADROOM_MIN))
            LOG(fatal, "Failed to create initial index buffer");
    }


    void shared_resource_manager::destroy_mesh_buffers() {

        if (m_mesh_data_buffer.buffer)    { m_dev->destroy_buffer(m_mesh_data_buffer);    m_mesh_data_buffer = {}; }
        if (m_submesh_data_buffer.buffer) { m_dev->destroy_buffer(m_submesh_data_buffer); m_submesh_data_buffer = {}; }

        // Clear allocator state — buffers are gone
        m_vertex_pool = {};
        m_index_pool = {};
        m_submesh_pool = {};
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


    void shared_resource_manager::upload_mesh_slice(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh) {
        {
            auto* dst = static_cast<GLT::asset::mesh::vertex*>(m_dev->map_buffer(m_vertex_buffer));
            std::memcpy(dst + slot.vertex_offset, mesh.vertices.data(), mesh.vertices.size() * sizeof(GLT::asset::mesh::vertex));
            m_dev->unmap_buffer(m_vertex_buffer);
        }
        {
            auto* dst = static_cast<u32*>(m_dev->map_buffer(m_index_buffer));
            std::memcpy(dst + slot.index_offset, mesh.indices.data(), mesh.indices.size() * sizeof(u32));
            m_dev->unmap_buffer(m_index_buffer);
        }
    }


    void shared_resource_manager::pack_mesh_into_ssbo(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh) {

        const u32 mesh_index = static_cast<u32>(&slot - m_mesh_slots.data());
        VALIDATE(mesh_index < MAX_MESHES, return, "", "Mesh SSBO capacity exceeded")

        glm::vec3 local_min(std::numeric_limits<f32>::max());
        glm::vec3 local_max(std::numeric_limits<f32>::lowest());
        for (const auto& v : mesh.vertices) {
            local_min = glm::min(local_min, v.position);
            local_max = glm::max(local_max, v.position);
        }

        const u32 submesh_count = slot.submesh_count;
        std::vector<u32> material_slots(submesh_count);
        for (u32 i = 0; i < submesh_count; ++i)
            material_slots[i] = acquire_material(mesh.submeshes[i].material);
        slot.material_slots = std::move(material_slots);

        // Submesh SSBO — write at the pool-allocated offset
        {
            auto* sub = static_cast<gpu_submesh_data*>(m_dev->map_buffer(m_submesh_data_buffer));
            for (u32 i = 0; i < submesh_count; ++i) {
                sub[slot.submesh_offset + i].first_index = mesh.submeshes[i].first_index;
                sub[slot.submesh_offset + i].index_count = mesh.submeshes[i].index_count;
            }
            m_dev->unmap_buffer(m_submesh_data_buffer);
        }

        // Mesh SSBO
        {
            gpu_mesh_data md{};
            md.local_min = local_min;
            md.local_max = local_max;
            md.first_submesh = slot.submesh_offset;
            md.submesh_count = submesh_count;
            md.vertex_offset = slot.vertex_offset;
            md.index_offset = slot.index_offset;

            auto* m = static_cast<gpu_mesh_data*>(m_dev->map_buffer(m_mesh_data_buffer));
            m[mesh_index] = md;
            m_dev->unmap_buffer(m_mesh_data_buffer);
        }
    }


    shared_mesh_id shared_resource_manager::load_mesh_internal(GLT::asset::handle handle) {

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
        if (mesh->vertices.size() > UINT32_MAX || mesh->indices.size() > UINT32_MAX) {
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }

        const u32 vertex_count = static_cast<u32>(mesh->vertices.size());
        const u32 index_count = static_cast<u32>(mesh->indices.size());
        const u32 submesh_count = static_cast<u32>(std::min<size_t>(mesh->submeshes.size(), MAX_SUBMESHES_PER_MESH));

        // Reserve a slot index up front so we know whether we're reusing
        bool reusing_slot = false;
        u32  slot_idx;

        if (!m_free_slots.empty()) {
            slot_idx = m_free_slots.back();
            m_free_slots.pop_back();
            reusing_slot = true;
        } else {
            if (m_mesh_slots.size() >= MAX_MESHES) {
                m_failed_mesh_loads.insert(handle);
                return INVALID_SHARED_MESH;
            }
            slot_idx = static_cast<u32>(m_mesh_slots.size());
            m_mesh_slots.emplace_back();
        }

        // Allocate buffer ranges (may compact / grow / fail)
        auto alloc = allocate_mesh_ranges(vertex_count, index_count, submesh_count);
        if (!alloc) {
            // Return the slot — no ranges were allocated
            m_mesh_slots[slot_idx] = {};
            m_free_slots.push_back(slot_idx);
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }

        mesh_slot& slot = m_mesh_slots[slot_idx];
        slot = {};
        slot.asset = handle;
        slot.vertex_offset = alloc->vertex_offset;
        slot.vertex_count = vertex_count;
        slot.index_offset = alloc->index_offset;
        slot.index_count = index_count;
        slot.submesh_offset = alloc->submesh_offset;
        slot.submesh_count = submesh_count;
        slot.alive = true;
        slot.local_min = mesh->bounds.min;
        slot.local_max = mesh->bounds.max;
        slot.ref_count = 1;

        upload_mesh_slice(slot, *mesh);

        // If we recycled a slot, its SSBO entry may still be read by in-flight frames. Wait before overwriting.
        if (reusing_slot)
            m_device.waitIdle();

        pack_mesh_into_ssbo(slot, *mesh);

        m_mesh_id_by_asset[handle] = slot_idx;
        return slot_idx;
    }

}

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
        m_retire.retire([this, id,
            vertex_offset = slot.vertex_offset, vertex_count = slot.vertex_count,
            index_offset = slot.index_offset, index_count = slot.index_count,
            submesh_offset = slot.submesh_offset, submesh_count = slot.submesh_count * slot.lod_count,
            cluster_offset = slot.cluster_offset, cluster_count = slot.cluster_count]() {
            pool_free(m_vertex_pool, vertex_offset, vertex_count);
            pool_free(m_index_pool, index_offset, index_count);
            pool_free(m_submesh_pool, submesh_offset, submesh_count);
            pool_free(m_cluster_pool, cluster_offset, cluster_count);
            m_free_slots.push_back(id);
        });

        // Release material slots owned by the submeshes
        for (u32 ms : slot.material_slots)
            release_material(ms);
        slot.material_slots.clear();

        // NOTE: SSBO entry is intentionally NOT zeroed here. In-flight frames may still reference this slot. The slot only
        // becomes reusable (m_free_slots) once the retire callback above has run

        m_mesh_id_by_asset.erase(slot.asset);
        slot = {};
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
        GLT_PROFILE_SCOPE("shared_resource_manager::init_pools");

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

        m_cluster_pool = {
            .buffer = &m_cluster_data_buffer,
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


    bool shared_resource_manager::pool_resize(linear_pool& p, u32 new_capacity) {
        GLT_PROFILE_SCOPE("shared_resource_manager::pool_resize");

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

        // Usage bits depend on which pool (the cluster pool is a plain storage buffer)
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

        // In-flight frames have the old buffer bound: retire it instead of stalling the GPU
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

        const bool v = m_vertex_pool.capacity > 0 && static_cast<u64>(m_vertex_pool.live) * 2 < m_vertex_pool.capacity;
        const bool i = m_index_pool.capacity > 0 && static_cast<u64>(m_index_pool.live) * 2 < m_index_pool.capacity;
        return v || i;
    }


    void shared_resource_manager::try_compact() {
        GLT_PROFILE_SCOPE("shared_resource_manager::try_compact");

        m_device.waitIdle();
        m_retire.flush();           // pending range frees must land before compaction rewrites the pools (device is idle)

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
            [this](u32 i) { return m_mesh_slots[i].submesh_count * m_mesh_slots[i].lod_count; },
            [this](u32 i, u32 o) { m_mesh_slots[i].submesh_offset = o; });

        if (m_cluster_data_buffer.buffer)
            compact_pool(m_cluster_pool,
                [this](u32 i) { return m_mesh_slots[i].cluster_offset; },
                [this](u32 i) { return m_mesh_slots[i].cluster_count; },
                [this](u32 i, u32 o) { m_mesh_slots[i].cluster_offset = o; });

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

            // Cluster records moved too (their index ranges are relative to the mesh's index offset: nothing to patch there)
            if (m_mesh_cluster_buffer.buffer) {
                auto* c = static_cast<gpu_mesh_cluster_data*>(m_dev->map_buffer(m_mesh_cluster_buffer));
                for (u32 idx : live)
                    c[idx].first_cluster = m_mesh_slots[idx].cluster_offset;
                m_dev->unmap_buffer(m_mesh_cluster_buffer);
            }
        }

        m_frames_since_compact = 0;
    }


    void shared_resource_manager::shrink_pools() {
        GLT_PROFILE_SCOPE("shared_resource_manager::shrink_pools");

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
        if (c > 0 && !grow(m_cluster_pool, c)) return std::nullopt;

        return try_all();
    }

    // buffer creation / destruction -----------------------------------------------------------------------------------

    void shared_resource_manager::create_mesh_buffers() {
        GLT_PROFILE_SCOPE("shared_resource_manager::create_mesh_buffers");

        m_mesh_data_buffer = m_dev->create_buffer(MAX_MESHES * sizeof(gpu_mesh_data),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        m_submesh_data_buffer = m_dev->create_buffer(MAX_SUBMESHES * sizeof(gpu_submesh_data),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        // Virtual geometry: one table entry per mesh slot (zero = no clusters), cluster records in a growable pool
        m_mesh_cluster_buffer = m_dev->create_buffer(MAX_MESHES * sizeof(gpu_mesh_cluster_data),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;
        std::memset(m_dev->map_buffer(m_mesh_cluster_buffer), 0, MAX_MESHES * sizeof(gpu_mesh_cluster_data));
        m_dev->unmap_buffer(m_mesh_cluster_buffer);

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
        if (!pool_resize(m_cluster_pool, CLUSTER_HEADROOM_MIN))
            LOG(error, "Failed to create the cluster buffer: virtual geometry is unavailable, meshes use their LODs");
    }


    void shared_resource_manager::destroy_mesh_buffers() {
        GLT_PROFILE_SCOPE("shared_resource_manager::destroy_mesh_buffers");

        if (m_mesh_data_buffer.buffer)    { m_dev->destroy_buffer(m_mesh_data_buffer);    m_mesh_data_buffer = {}; }
        if (m_submesh_data_buffer.buffer) { m_dev->destroy_buffer(m_submesh_data_buffer); m_submesh_data_buffer = {}; }
        if (m_mesh_cluster_buffer.buffer) { m_dev->destroy_buffer(m_mesh_cluster_buffer); m_mesh_cluster_buffer = {}; }
        if (m_cluster_data_buffer.buffer) { m_dev->destroy_buffer(m_cluster_data_buffer); m_cluster_data_buffer = {}; }

        // Clear allocator state — buffers are gone
        m_vertex_pool = {};
        m_index_pool = {};
        m_submesh_pool = {};
        m_cluster_pool = {};
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
        GLT_PROFILE_SCOPE("shared_resource_manager::upload_mesh_slice");
        {
            auto* dst = static_cast<GLT::asset::mesh::vertex*>(m_dev->map_buffer(m_vertex_buffer));
            std::memcpy(dst + slot.vertex_offset, mesh.vertices.data(), mesh.vertices.size() * sizeof(GLT::asset::mesh::vertex));
            if (slot.vertex_count > mesh.vertices.size())           // LOD vertices (recomputed normals) right behind
                std::memcpy(dst + slot.vertex_offset + mesh.vertices.size(), mesh.lod_vertices.data(),
                    (slot.vertex_count - mesh.vertices.size()) * sizeof(GLT::asset::mesh::vertex));
            m_dev->unmap_buffer(m_vertex_buffer);
        }
        {
            // [indices ++ lod_indices ++ cluster_indices]: the LOD ranges address the first two as one stream (asset/mesh.h
            // lod_range), the virtual geometry clusters the third part (starting at slot.cluster_index_base)
            const size_t base_count = mesh.indices.size();
            const size_t lod_end = slot.cluster_count > 0 ? slot.cluster_index_base : slot.index_count;
            const size_t lod_index_count = (slot.lod_count > 1 && lod_end > base_count) ? lod_end - base_count : 0;
            const size_t cluster_index_count = slot.cluster_count > 0 ? slot.index_count - slot.cluster_index_base : 0;

            auto* dst = static_cast<u32*>(m_dev->map_buffer(m_index_buffer));
            std::memcpy(dst + slot.index_offset, mesh.indices.data(), mesh.indices.size() * sizeof(u32));
            if (lod_index_count > 0)
                std::memcpy(dst + slot.index_offset + base_count, mesh.lod_indices.data(), lod_index_count * sizeof(u32));
            if (cluster_index_count > 0)
                std::memcpy(dst + slot.index_offset + slot.cluster_index_base, mesh.cluster_indices.data(),
                    cluster_index_count * sizeof(u32));

            // Never let the GPU fetch past this mesh's vertices (another mesh's data / uninitialised pool memory: huge
            // NaN-shaded triangles). Out-of-range triangles become degenerate. Checks the CPU copy (the mapped buffer is
            // write-combined: reading it back would be slow) and only patches the uploaded triangles that are bad
            u32 bad_triangles = 0;
            u32* uploaded = dst + slot.index_offset;
            const u32 checked_count = static_cast<u32>(base_count + lod_index_count);   // cluster indices: checked below
            auto source_index = [&](u32 i) { return i < base_count ? mesh.indices[i] : mesh.lod_indices[i - base_count]; };
            for (u32 t = 0; t + 2 < checked_count; t += 3) {
                if (source_index(t) < slot.vertex_count && source_index(t + 1) < slot.vertex_count && source_index(t + 2) < slot.vertex_count)
                    continue;
                uploaded[t] = uploaded[t + 1] = uploaded[t + 2] = 0u;
                ++bad_triangles;
            }

            // Cluster triangles only use the source vertices (asset/mesh_cluster.h). Clusters are whole triangle lists
            // stored back to back, so triples of the stream are triangles here too
            const size_t source_vertices = mesh.vertices.size();
            for (size_t t = 0; t + 2 < cluster_index_count; t += 3) {
                const u32* tri = &mesh.cluster_indices[t];
                if (tri[0] < source_vertices && tri[1] < source_vertices && tri[2] < source_vertices)
                    continue;
                u32* out = uploaded + slot.cluster_index_base + t;
                out[0] = out[1] = out[2] = 0u;
                ++bad_triangles;
            }
            if (bad_triangles > 0)
                LOG(warn, "mesh slot: {} triangle(s) with out-of-range vertex indices dropped (corrupt mesh data)", bad_triangles);
            m_dev->unmap_buffer(m_index_buffer);
        }

        // Virtual geometry cluster records, as they are in the asset
        if (slot.cluster_count > 0 && m_cluster_data_buffer.buffer) {
            auto* dst = static_cast<GLT::asset::mesh::cluster*>(m_dev->map_buffer(m_cluster_data_buffer));
            std::memcpy(dst + slot.cluster_offset, mesh.clusters.data(), static_cast<size_t>(slot.cluster_count) * sizeof(GLT::asset::mesh::cluster));
            m_dev->unmap_buffer(m_cluster_data_buffer);
        }
    }


    void shared_resource_manager::pack_mesh_into_ssbo(mesh_slot& slot, const GLT::asset::mesh::mesh_asset& mesh) {
        GLT_PROFILE_SCOPE("shared_resource_manager::pack_mesh_into_ssbo");

        const u32 mesh_index = static_cast<u32>(&slot - m_mesh_slots.data());
        VALIDATE(mesh_index < MAX_MESHES, return, "", "Mesh SSBO capacity exceeded")

        glm::vec3 local_min(std::numeric_limits<f32>::max());
        glm::vec3 local_max(std::numeric_limits<f32>::lowest());
        for (const auto& v : mesh.vertices) {
            local_min = glm::min(local_min, v.position);
            local_max = glm::max(local_max, v.position);
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

        // Submesh SSBO — write at the pool-allocated offset
        {
            auto* sub = static_cast<gpu_submesh_data*>(m_dev->map_buffer(m_submesh_data_buffer));
            for (u32 i = 0; i < submesh_count * lod_count; ++i) {
                sub[slot.submesh_offset + i].first_index = slot.submesh_ranges[i].x;
                sub[slot.submesh_offset + i].index_count = slot.submesh_ranges[i].y;
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
            md.lod_count = lod_count;
            slot.lod_screen = lod_screen_sizes(mesh, lod_count);
            std::copy(slot.lod_screen.begin(), slot.lod_screen.end(), md.lod_screen);

            auto* m = static_cast<gpu_mesh_data*>(m_dev->map_buffer(m_mesh_data_buffer));
            m[mesh_index] = md;
            m_dev->unmap_buffer(m_mesh_data_buffer);
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
        write_mesh_cluster_entry(mesh_index, slot, mesh.virtual_geometry.error_scale);
    }


    void shared_resource_manager::write_mesh_cluster_entry(u32 mesh_index, const mesh_slot& slot, f32 error_scale) {

        if (!m_mesh_cluster_buffer.buffer || mesh_index >= MAX_MESHES)
            return;

        gpu_mesh_cluster_data entry{};
        entry.first_cluster = slot.cluster_offset;
        entry.cluster_count = slot.cluster_count;
        entry.index_base = slot.cluster_index_base;
        entry.error_scale = std::isfinite(error_scale) ? std::clamp(error_scale, 0.05f, 20.0f) : 1.0f;

        auto* table = static_cast<gpu_mesh_cluster_data*>(m_dev->map_buffer(m_mesh_cluster_buffer));
        table[mesh_index] = entry;
        m_dev->unmap_buffer(m_mesh_cluster_buffer);
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
        if (mesh->vertices.size() > UINT32_MAX || mesh->indices.size() > UINT32_MAX) {
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }

        mesh_upload_layout layout = mesh_layout_of(*mesh);
        const u32 submesh_count = layout.submesh_count;

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

        // Allocate buffer ranges (may compact / grow / fail). When the pools are full the optional parts are given up
        // one by one: first the virtual geometry clusters, then the LOD levels (LOD 0 only)
        auto alloc = allocate_mesh_ranges(layout.vertex_count, layout.index_count, submesh_count * layout.lod_count, layout.cluster_count);
        while (!alloc && reduce_layout(layout, *mesh))
            alloc = allocate_mesh_ranges(layout.vertex_count, layout.index_count, submesh_count * layout.lod_count, layout.cluster_count);
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
        slot.vertex_count = layout.vertex_count;
        slot.index_offset = alloc->index_offset;
        slot.index_count = layout.index_count;
        slot.submesh_offset = alloc->submesh_offset;
        slot.submesh_count = submesh_count;
        slot.lod_count = layout.lod_count;
        slot.cluster_offset = alloc->cluster_offset;
        slot.cluster_count = layout.cluster_count;
        slot.cluster_index_base = layout.cluster_index_base;
        slot.source = mesh;
        slot.source_revision = mesh->revision;
        slot.source_geometry = geometry_signature(*mesh);
        slot.alive = true;
        slot.local_min = mesh->bounds.min;
        slot.local_max = mesh->bounds.max;
        slot.ref_count = 1;

        upload_mesh_slice(slot, *mesh);

        // Recycled slots only reach m_free_slots after the GPU finished with them (see release_mesh), no wait needed
        (void)reusing_slot;

        pack_mesh_into_ssbo(slot, *mesh);

        m_mesh_id_by_asset[handle] = slot_idx;
        return slot_idx;
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
            if (total <= UINT32_MAX && mesh.clusters.size() <= UINT32_MAX) {
                l.cluster_count = static_cast<u32>(mesh.clusters.size());
                l.cluster_index_base = l.index_count;
                l.index_count = static_cast<u32>(total);
            }
        }
        return l;
    }


    bool shared_resource_manager::reduce_layout(mesh_upload_layout& layout, const GLT::asset::mesh::mesh_asset& mesh) {

        if (layout.cluster_count > 0) {
            layout.index_count = layout.cluster_index_base;         // drop the cluster triangle lists
            layout.cluster_count = 0;
            layout.cluster_index_base = 0;
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
            // Settings only: the switch sizes may have changed. Patch them into the mesh table entry (a few floats; an
            // in-flight frame sees either the old or the new sizes, both valid)
            mesh_slot& slot = m_mesh_slots[id];
            slot.source_revision = mesh->revision;
            slot.lod_screen = lod_screen_sizes(*mesh, slot.lod_count);
            auto* m = static_cast<gpu_mesh_data*>(m_dev->map_buffer(m_mesh_data_buffer));
            std::copy(slot.lod_screen.begin(), slot.lod_screen.end(), m[id].lod_screen);
            m_dev->unmap_buffer(m_mesh_data_buffer);
            write_mesh_cluster_entry(id, slot, mesh->virtual_geometry.error_scale);    // live: detail scale
            return false;
        }

        if (mesh->vertices.size() > UINT32_MAX || mesh->indices.size() > UINT32_MAX)
            return false;

        mesh_upload_layout layout = mesh_layout_of(*mesh);
        auto alloc = allocate_mesh_ranges(layout.vertex_count, layout.index_count, layout.submesh_count * layout.lod_count, layout.cluster_count);
        while (!alloc && reduce_layout(layout, *mesh))
            alloc = allocate_mesh_ranges(layout.vertex_count, layout.index_count, layout.submesh_count * layout.lod_count, layout.cluster_count);

        mesh_slot& slot = m_mesh_slots[id];        // after allocate: compaction may have moved this slot's ranges
        slot.source = mesh;
        slot.source_revision = mesh->revision;     // also on failure: don't retry every frame
        slot.source_geometry = geometry;
        if (!alloc) {
            LOG(warn, "refresh_mesh: no room to re-upload mesh slot {}, keeping the previous geometry", id);
            return false;
        }

        // In-flight frames still draw from the old ranges: free them once the GPU is done. The new data goes to fresh
        // ranges, and the mesh table entry is rewritten last, so any frame sees either the old or the new mesh
        m_retire.retire([this,
            vertex_offset = slot.vertex_offset, vertex_count = slot.vertex_count,
            index_offset = slot.index_offset, index_count = slot.index_count,
            submesh_offset = slot.submesh_offset, submesh_count = slot.submesh_count * slot.lod_count,
            cluster_offset = slot.cluster_offset, cluster_count = slot.cluster_count]() {
            pool_free(m_vertex_pool, vertex_offset, vertex_count);
            pool_free(m_index_pool, index_offset, index_count);
            pool_free(m_submesh_pool, submesh_offset, submesh_count);
            pool_free(m_cluster_pool, cluster_offset, cluster_count);
        });

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
        slot.local_min = mesh->bounds.min;
        slot.local_max = mesh->bounds.max;

        upload_mesh_slice(slot, *mesh);
        pack_mesh_into_ssbo(slot, *mesh);
        LOG(trace, "refresh_mesh: slot {} re-uploaded ({} LOD levels, {} clusters, {} indices)", id, slot.lod_count, slot.cluster_count,
            slot.index_count);
        return true;
    }

}

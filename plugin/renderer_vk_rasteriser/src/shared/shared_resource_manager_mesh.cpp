
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
        // v1: ref-counting is applied per scene at the scene renderer level. The shared manager does
        // not currently track per-mesh reference counts — the scene diffing logic handles retain/release.
        // If you want explicit ref counting (e.g. for scene renderers that go out of scope without
        // releasing everything), add a ref_count field to mesh_slot and implement here.
    }


    void shared_resource_manager::release_mesh(shared_mesh_id id) {

        if (id >= m_mesh_slots.size() || !m_mesh_slots[id].alive)
            return;

        if (--m_mesh_slots[id].ref_count > 0)
            return;

        m_device.waitIdle();
        mesh_slot& slot = m_mesh_slots[id];
        clear_mesh_ssbo_entry(slot);
        m_mesh_id_by_asset.erase(slot.asset);

        slot.alive = false;
        slot.asset = INVALID_HANDLE;
        slot.vertex_offset = slot.vertex_count = 0;
        slot.index_offset = slot.index_count = 0;
        m_free_slots.push_back(id);
    }


    bool shared_resource_manager::is_mesh_ready(shared_mesh_id id) const {
        return id < m_mesh_slots.size() && m_mesh_slots[id].alive;
    }


    const mesh_slot* shared_resource_manager::get_mesh(shared_mesh_id id) const {
        if (id >= m_mesh_slots.size() || !m_mesh_slots[id].alive)
            return nullptr;
        return &m_mesh_slots[id];
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void shared_resource_manager::create_mesh_buffers() {

        m_mesh_data_buffer = m_dev->create_buffer(MAX_MESHES * sizeof(gpu_mesh_data),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        m_submesh_data_buffer = m_dev->create_buffer(MAX_SUBMESHES * sizeof(gpu_submesh_data),
            vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        m_live_buffer_count++;

        std::memset(m_dev->map_buffer(m_mesh_data_buffer),    0, MAX_MESHES * sizeof(gpu_mesh_data));
        m_dev->unmap_buffer(m_mesh_data_buffer);
        std::memset(m_dev->map_buffer(m_submesh_data_buffer), 0, MAX_SUBMESHES * sizeof(gpu_submesh_data));
        m_dev->unmap_buffer(m_submesh_data_buffer);

        ASSERT(reserve_mesh_space(VERTEX_HEADROOM_MIN, INDEX_HEADROOM_MIN),
            "", "Failed to allocate initial mesh buffers")
    }


    void shared_resource_manager::destroy_mesh_buffers() {
        if (m_mesh_data_buffer.buffer)    { m_dev->destroy_buffer(m_mesh_data_buffer);    m_mesh_data_buffer = {}; }
        if (m_submesh_data_buffer.buffer) { m_dev->destroy_buffer(m_submesh_data_buffer); m_submesh_data_buffer = {}; }
    }


    mesh_slot* shared_resource_manager::find_slot(GLT::asset::handle handle) {
        for (auto& s : m_mesh_slots)
            if (s.alive && s.asset == handle) return &s;
        return nullptr;
    }


    const mesh_slot* shared_resource_manager::find_slot(GLT::asset::handle handle) const {
        return const_cast<shared_resource_manager*>(this)->find_slot(handle);
    }


    bool shared_resource_manager::reserve_mesh_space(u32 vertex_count, u32 index_count) {

        const u64 need_v = static_cast<u64>(m_vertex_used) + vertex_count;
        const u64 need_i = static_cast<u64>(m_index_used)  + index_count;
        const bool grow_v = need_v > m_vertex_capacity;
        const bool grow_i = need_i > m_index_capacity;
        if (!grow_v && !grow_i) return true;

        const u64 max_bytes = m_physical_device.getProperties().limits.maxStorageBufferRange;

        u64 new_v_capacity = m_vertex_capacity;
        u64 new_i_capacity = m_index_capacity;
        if (grow_v) {
            new_v_capacity = GLT::math::max<u64>(need_v,
                static_cast<u64>(m_vertex_capacity) + std::max<u64>(m_vertex_capacity / 2, VERTEX_HEADROOM_MIN));
            if (new_v_capacity * sizeof(GLT::asset::mesh::vertex) > max_bytes) {
                LOG(error, "vertex buffer growth exceeds device limit ({} bytes > {})",
                    new_v_capacity * sizeof(GLT::asset::mesh::vertex), max_bytes);
                return false;
            }
        }
        if (grow_i) {
            new_i_capacity = GLT::math::max<u64>(need_i,
                static_cast<u64>(m_index_capacity) + std::max<u64>(m_index_capacity / 2, INDEX_HEADROOM_MIN));
            if (new_i_capacity * sizeof(u32) > max_bytes) {
                LOG(error, "index buffer growth exceeds device limit ({} bytes > {})",
                    new_i_capacity * sizeof(u32), max_bytes);
                return false;
            }
        }

        const auto usage_v = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer;
        const auto usage_i = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndexBuffer;

        util::allocated_buffer new_vb{}, new_ib{};
        if (grow_v) {
            new_vb = m_dev->create_buffer(new_v_capacity * sizeof(GLT::asset::mesh::vertex), usage_v,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            if (!new_vb.buffer || !new_vb.allocation) return false;
        }
        if (grow_i) {
            new_ib = m_dev->create_buffer(new_i_capacity * sizeof(u32), usage_i,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            if (!new_ib.buffer || !new_ib.allocation) { if (new_vb.buffer) m_dev->destroy_buffer(new_vb); return false; }
        }

        if (grow_v && m_vertex_used > 0 && m_vertex_buffer.buffer) {
            const void* src = m_dev->map_buffer(m_vertex_buffer);
            void*       dst = m_dev->map_buffer(new_vb);
            std::memcpy(dst, src, m_vertex_used * sizeof(GLT::asset::mesh::vertex));
            m_dev->unmap_buffer(new_vb);
            m_dev->unmap_buffer(m_vertex_buffer);
        }
        if (grow_i && m_index_used > 0 && m_index_buffer.buffer) {
            const void* src = m_dev->map_buffer(m_index_buffer);
            void*       dst = m_dev->map_buffer(new_ib);
            std::memcpy(dst, src, m_index_used * sizeof(u32));
            m_dev->unmap_buffer(new_ib);
            m_dev->unmap_buffer(m_index_buffer);
        }

        m_device.waitIdle();
        if (grow_v) { std::swap(m_vertex_buffer, new_vb); m_vertex_capacity = static_cast<u32>(new_v_capacity); if (new_vb.buffer) m_dev->destroy_buffer(new_vb); }
        if (grow_i) { std::swap(m_index_buffer,  new_ib); m_index_capacity = static_cast<u32>(new_i_capacity);  if (new_ib.buffer) m_dev->destroy_buffer(new_ib); }
        return true;
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
        for (const auto& v : mesh.vertices) { local_min = glm::min(local_min, v.position); local_max = glm::max(local_max, v.position); }

        const u32 submesh_count = static_cast<u32>(std::min<size_t>(mesh.submeshes.size(), MAX_SUBMESHES_PER_MESH));
        VALIDATE(m_submesh_used + submesh_count <= MAX_SUBMESHES, return, "", "Submesh SSBO capacity exceeded")

        std::vector<u32> material_slots(submesh_count);
        for (u32 i = 0; i < submesh_count; ++i)
            material_slots[i] = acquire_material(mesh.submeshes[i].material);
        slot.material_slots = material_slots;

        const u32 first = m_submesh_used;
        m_submesh_used += submesh_count;

        auto* sub = static_cast<gpu_submesh_data*>(m_dev->map_buffer(m_submesh_data_buffer));
        for (u32 i = 0; i < submesh_count; ++i) {
            sub[first + i].first_index = mesh.submeshes[i].first_index;
            sub[first + i].index_count = mesh.submeshes[i].index_count;
        }
        m_dev->unmap_buffer(m_submesh_data_buffer);

        gpu_mesh_data md{};
        md.local_min = local_min;
        md.local_max = local_max;
        md.first_submesh = first;
        md.submesh_count = submesh_count;
        md.vertex_offset = slot.vertex_offset;
        md.index_offset = slot.index_offset;

        auto* m = static_cast<gpu_mesh_data*>(m_dev->map_buffer(m_mesh_data_buffer));
        m[mesh_index] = md;
        m_dev->unmap_buffer(m_mesh_data_buffer);
    }


    void shared_resource_manager::clear_mesh_ssbo_entry(mesh_slot& slot) {
        for (u32 ms : slot.material_slots) release_material(ms);
        slot.material_slots.clear();

        const u32 mesh_index = static_cast<u32>(&slot - m_mesh_slots.data());
        if (mesh_index >= MAX_MESHES) return;

        gpu_mesh_data empty{};
        auto* m = static_cast<gpu_mesh_data*>(m_dev->map_buffer(m_mesh_data_buffer));
        m[mesh_index] = empty;
        m_dev->unmap_buffer(m_mesh_data_buffer);
    }


    shared_mesh_id shared_resource_manager::load_mesh_internal(GLT::asset::handle handle) {

        auto registry = GLT::asset::registry::get_ref();
        if (!registry)
            return INVALID_SHARED_MESH;
        if (find_slot(handle))
            return INVALID_SHARED_MESH;   // already loaded
        if (m_failed_mesh_loads.contains(handle))
            return INVALID_SHARED_MESH;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(handle);
        if (!mesh) { m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH; }
        if (mesh->vertices.size() > UINT32_MAX || mesh->indices.size() > UINT32_MAX) {
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }

        const u32 vertex_count = static_cast<u32>(mesh->vertices.size());
        const u32 index_count = static_cast<u32>(mesh->indices.size());
        const u32 submesh_count = static_cast<u32>(std::min<size_t>(mesh->submeshes.size(), MAX_SUBMESHES_PER_MESH));

        if (m_free_slots.empty() && m_mesh_slots.size() >= MAX_MESHES) {
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }
        if (m_submesh_used + submesh_count > MAX_SUBMESHES) {
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }
        if (!reserve_mesh_space(vertex_count, index_count)) {
            m_failed_mesh_loads.insert(handle);
            return INVALID_SHARED_MESH;
        }

        u32 slot_idx;
        if (!m_free_slots.empty()) {
            slot_idx = m_free_slots.back(); m_free_slots.pop_back();
        } else {
            slot_idx = static_cast<u32>(m_mesh_slots.size());
            m_mesh_slots.emplace_back();
        }

        mesh_slot& slot = m_mesh_slots[slot_idx];
        slot.asset = handle;
        slot.vertex_offset = m_vertex_used;
        slot.vertex_count = vertex_count;
        slot.index_offset = m_index_used;
        slot.index_count = index_count;
        slot.alive = true;
        slot.local_min = mesh->bounds.min;
        slot.local_max = mesh->bounds.max;
        slot.ref_count = 1;

        upload_mesh_slice(slot, *mesh);
        pack_mesh_into_ssbo(slot, *mesh);

        m_vertex_used += slot.vertex_count;
        m_index_used  += slot.index_count;
        m_mesh_id_by_asset[handle] = slot_idx;
        return slot_idx;
    }

}

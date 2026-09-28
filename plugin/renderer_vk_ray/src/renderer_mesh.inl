#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer_vk_ray {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    bool renderer::load_mesh(const std::filesystem::path& content_relative_path) {

        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "No asset registry");

        auto handle = registry->load(content_relative_path);
        VALIDATE(handle.has_value(), return false, "", "Failed to load mesh [{}]", content_relative_path.generic_string());

        return load_mesh(*handle);
    }


    bool renderer::load_mesh(const GLT::asset::handle handle) {

        auto registry = GLT::asset::registry::get_ref();
        ASSERT(registry, "", "No asset registry");

        // Reuse the shared lookup so "loaded" means the same thing everywhere
        if (find_slot(handle))
        return true;

        auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(handle);
        VALIDATE(mesh, return false, "", "Asset [{}] is not a mesh_asset", registry->info(handle).name);
       
        // // reserve buffer space. May grow the shared buffers
        // u32 live_submeshes = 0;
        // for (const auto& sm : mesh->submeshes)
        //     if (sm.index_count >= 3)
        //         live_submeshes++;

        // DEBUG-ONLY: -------------------------------------------------------------------------------------------------
        #define SANITY_CHECK_ON_THE_DECODED_MESH 0
        #if SANITY_CHECK_ON_THE_DECODED_MESH
            {
                LOG(info, "count supplied to reserve_mesh_space(): [{}], count in mesh asset [{}]", live_submeshes, mesh->submeshes.size())

                const size_t n_idx = mesh->indices.size();
                const size_t n_vtx = mesh->vertices.size();

                u32 global_max_idx = 0;
                for (u32 i : mesh->indices)
                    global_max_idx = std::max(global_max_idx, i);

                LOG(info, "mesh: {} verts, {} indices, max index in array = {}", n_vtx, n_idx, global_max_idx);

                if (global_max_idx >= n_vtx)
                    LOG(fatal, "  !! mesh index {} >= vertex count {} - asset is corrupt", global_max_idx, n_vtx);

                for (size_t si = 0; si < mesh->submeshes.size(); ++si) {
                    const auto& sm = mesh->submeshes[si];

                    const u64 end = u64(sm.first_index) + u64(sm.index_count);
                    if (end > n_idx) {
                        LOG(fatal, "  submesh[{}]: first={} count={} -> end={} OVERFLOWS index array size {}",
                            si, sm.first_index, sm.index_count, end, n_idx);
                        continue;
                    }

                    u32 sub_max_idx = 0;
                    for (u32 k = 0; k < sm.index_count; ++k)
                        sub_max_idx = std::max(sub_max_idx, mesh->indices[sm.first_index + k]);

                    LOG(info, "  submesh[{}]: first={} count={} max_vertex_idx={}{}",
                        si, sm.first_index, sm.index_count, sub_max_idx,
                        (sub_max_idx >= n_vtx ? "  <-- OOB VERTEX" : ""));
                }
            }
        #endif

        const bool grew = reserve_mesh_space(static_cast<u32>(mesh->vertices.size()), static_cast<u32>(mesh->indices.size()), 0);
        
        // If buffers moved, every existing BLAS references a dead address - rebuild them all once, right here, not per-mesh
        // This is the only path that touches other meshes
        if (grew)
            rebuild_all_blases();

        u32 slot_idx;
        if (!m_free_slots.empty()) {                                // claim a slot
        
            slot_idx = m_free_slots.back();
            m_free_slots.pop_back();
        
        } else {
        
            slot_idx = static_cast<u32>(m_mesh_slots.size());
            m_mesh_slots.emplace_back();
        }

        mesh_slot& slot = m_mesh_slots[slot_idx];
        slot.asset = handle;
        slot.vertex_offset = m_vertex_used;
        slot.vertex_count = static_cast<u32>(mesh->vertices.size());
        slot.index_offset = m_index_used;
        slot.index_count = static_cast<u32>(mesh->indices.size());
        // slot.material_offset = m_material_used;
        // slot.material_count = live_submeshes;
        slot.alive = true;

        upload_mesh_slice(slot, *mesh);                             // Upload just this mesh's slice
        m_vertex_used += slot.vertex_count;
        m_index_used += slot.index_count;
        // m_material_used += slot.material_count;

        build_blas_for_slot(slot);                                  // Build exactly one BLAS for this mesh
        // m_tlas_dirty = true;                                        // TLAS contents changed; the handle did not

        return true;
    }


    void renderer::unload_mesh(GLT::asset::handle handle) {

        u32 slot_index = std::numeric_limits<u32>::max();
        for (u32 index = 0; index < m_mesh_slots.size(); index++) {
            if (m_mesh_slots[index].alive && m_mesh_slots[index].asset == handle) {
                slot_index = index;
                break;
            }
        }

        VALIDATE(slot_index != std::numeric_limits<u32>::max(), return, "", "handle not loaded");

        mesh_slot& slot = m_mesh_slots[slot_index];
        slot.alive = false;

        m_device.waitIdle();
        m_vr_dev->destroy_blas(slot.blas);
        
        // Reset everything. A dead slot must not carry residual identity
        slot.blas = {};
        slot.asset = INVALID_HANDLE;
        slot.vertex_offset = 0;
        slot.vertex_count = 0;
        slot.index_offset = 0;
        slot.index_count = 0;
        slot.material_offset = 0;
        slot.material_count = 0;

        m_free_slots.push_back(slot_index);
        // m_tlas_dirty = true;
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    // mesh handling ---------------------------------------------------------------------------------------------------

    bool renderer::reserve_mesh_space(u32 v, u32 i, u32 m) {

        const u32 need_v = m_vertex_used + v;
        const u32 need_i = m_index_used + i;
        const u32 need_m = m_material_used + m;

        bool grew = false;

        if (need_v > m_vertex_capacity) {
            m_vertex_capacity = std::max(need_v, m_vertex_capacity + std::max(m_vertex_capacity / 2, VERTEX_HEADROOM_MIN));
            grew = true;
        }
        if (need_i > m_index_capacity) {
            m_index_capacity  = std::max(need_i, m_index_capacity + std::max(m_index_capacity / 2, INDEX_HEADROOM_MIN));
            grew = true;
        }
        if (need_m > m_material_capacity) {
            m_material_capacity = std::max(need_m, m_material_capacity + std::max(m_material_capacity / 2, MATERIAL_HEADROOM_MIN));
            grew = true;
        }
        if (!grew)
            return false;

        // Grow: create new buffers, copy old, then swap. Because device addresses change, every BLAS that reads them is invalid - caller must rebuild them.
        const auto as_input = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer;

        auto new_vb = m_vr_dev->create_buffer(m_vertex_capacity * sizeof(GLT::asset::mesh::vertex), as_input,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        auto new_ib = m_vr_dev->create_buffer(m_index_capacity * sizeof(u32), as_input,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        auto new_mb = m_vr_dev->create_buffer(m_material_capacity * sizeof(gpu_material), vk::BufferUsageFlagBits::eStorageBuffer,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

        std::memset(m_vr_dev->map_buffer(new_vb), 0, m_vertex_capacity * sizeof(GLT::asset::mesh::vertex));
        std::memset(m_vr_dev->map_buffer(new_ib), 0, m_index_capacity  * sizeof(u32));
        std::memset(m_vr_dev->map_buffer(new_mb), 0, m_material_capacity * sizeof(gpu_material));
        m_vr_dev->unmap_buffer(new_vb);
        m_vr_dev->unmap_buffer(new_ib);
        m_vr_dev->unmap_buffer(new_mb);

        // Copy the used region from old to new. Only touch a buffer if it has data -
        // an unpopulated slot (e.g. right after the initial pre-reservation) was never mapped, so calling unmap on it would trip VMA's assertion.
        if (m_vertex_used) {
            const void* src = m_vr_dev->map_buffer(m_vertex_buffer);
            void* dst = m_vr_dev->map_buffer(new_vb);
            std::memcpy(dst, src, m_vertex_used * sizeof(GLT::asset::mesh::vertex));
            m_vr_dev->unmap_buffer(m_vertex_buffer);
            m_vr_dev->unmap_buffer(new_vb);
        }

        if (m_index_used) {
            const void* src = m_vr_dev->map_buffer(m_index_buffer);
            void* dst = m_vr_dev->map_buffer(new_ib);
            std::memcpy(dst, src, m_index_used * sizeof(u32));
            m_vr_dev->unmap_buffer(m_index_buffer);
            m_vr_dev->unmap_buffer(new_ib);
        }

        if (m_material_used) {
            const void* src = m_vr_dev->map_buffer(m_material_buffer);
            void* dst = m_vr_dev->map_buffer(new_mb);
            std::memcpy(dst, src, m_material_used * sizeof(gpu_material));
            m_vr_dev->unmap_buffer(m_material_buffer);
            m_vr_dev->unmap_buffer(new_mb);
        }

        std::swap(m_vertex_buffer, new_vb);
        std::swap(m_index_buffer, new_ib);
        std::swap(m_material_buffer, new_mb);

        m_device.waitIdle();

        // After the swap, new_* hold the *old* buffers (possibly default-constructed).
        if (new_vb.buffer) m_vr_dev->destroy_buffer(new_vb);
        if (new_ib.buffer) m_vr_dev->destroy_buffer(new_ib);
        if (new_mb.buffer) m_vr_dev->destroy_buffer(new_mb);

        update_descriptor_set();
        return true;
    }


    void renderer::build_blas_for_slot(mesh_slot& slot) {

        auto* mesh = GLT::asset::registry::get_ref()->data_as<GLT::asset::mesh::mesh_asset>(slot.asset);
        ASSERT(mesh, "", "slot has no mesh");

        vr::blas_create_info bci{};
        bci.flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace;

        const vk::DeviceAddress base_vertex_addr = m_vertex_buffer.dev_address + slot.vertex_offset * sizeof(GLT::asset::mesh::vertex);
        const vk::DeviceAddress base_index_addr = m_index_buffer.dev_address  + slot.index_offset  * sizeof(u32);

        for (const auto& sm : mesh->submeshes) {
            if (sm.index_count < 3)
                continue;

            vr::geometry_data gd{};
            gd.vertex_format = vk::Format::eR32G32B32Sfloat;
            gd.stride = sizeof(GLT::asset::mesh::vertex);
            gd.index_format = vk::IndexType::eUint32;
            gd.primitive_count = sm.index_count / 3;
            gd.data_addresses.vertex_dev_address = base_vertex_addr;
            gd.data_addresses.index_dev_address  = base_index_addr + sm.first_index * sizeof(u32);
            bci.geometries.push_back(gd);
        }

        auto [handle, build_info] = m_vr_dev->create_blas(bci);
        slot.blas = handle;

        std::vector<vr::blas_build_info> infos = { build_info };
        auto scratch = m_vr_dev->create_scratch_buffer_from_build_infos(infos);
        auto cmd = m_device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(m_graphics_pool, vk::CommandBufferLevel::ePrimary, 1))[0];
        
        cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        m_vr_dev->build_blas(infos, cmd);
        m_vr_dev->add_acceleration_build_barrier(cmd);
        cmd.end();

        auto si = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&cmd);
        m_queues.graphics_queue.submit(si, nullptr);
        m_device.waitIdle();

        m_vr_dev->destroy_buffer(scratch);
        m_device.freeCommandBuffers(m_graphics_pool, cmd);
    }


    void renderer::upload_mesh_slice(mesh_slot& slot, GLT::asset::mesh::mesh_asset& mesh) {

        // Vertices
        {
            auto* dst = static_cast<GLT::asset::mesh::vertex*>(m_vr_dev->map_buffer(m_vertex_buffer));
            std::memcpy(dst + slot.vertex_offset, mesh.vertices.data(), mesh.vertices.size() * sizeof(GLT::asset::mesh::vertex));
            m_vr_dev->unmap_buffer(m_vertex_buffer);
        }

        // Indices
        {
            auto* dst = static_cast<u32*>(m_vr_dev->map_buffer(m_index_buffer));
            std::memcpy(dst + slot.index_offset, mesh.indices.data(), mesh.indices.size() * sizeof(u32));
            m_vr_dev->unmap_buffer(m_index_buffer);
        }
    }


    void renderer::rebuild_all_blases() {

        // Called after the shared vertex/index/material buffers moved. Every BLAS
        // baked a device address into its build; they are all stale now.
        m_device.waitIdle();

        for (auto& slot : m_mesh_slots) {

            if (!slot.alive)
                continue;

            if (slot.blas.buffer.buffer)          // guard against a default-constructed handle
                m_vr_dev->destroy_blas(slot.blas);
                
            slot.blas = {};
            build_blas_for_slot(slot);
        }
    }

}

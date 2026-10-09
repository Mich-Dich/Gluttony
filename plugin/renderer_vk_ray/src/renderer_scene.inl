#pragma once


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_ray {

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

    void renderer::submit_scene(std::span<const GLT::asset::mesh::instance> instances) {

        m_scene_instances.assign(instances.begin(), instances.end());
        m_scene_meshes.clear();
        m_scene_materials.clear();
        m_scene_meshes.reserve(instances.size());

        auto registry = GLT::asset::registry::get_ref();

        // Collect every mesh AND every material the scene actually references.
        for (const auto& instance : instances) {

            if (instance.mesh == INVALID_HANDLE)                                continue;
            m_scene_meshes.insert(instance.mesh);

            if (instance.material_override != INVALID_HANDLE)
                m_scene_materials.insert(instance.material_override);

            if (auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(instance.mesh)) {
                for (auto mat : mesh->material_handles)
                    if (mat != INVALID_HANDLE)
                        m_scene_materials.insert(mat);
            }
        }

        // Schedule loads ----------------------------------------------------------

        for (auto mat : m_scene_materials) {

            if (find_material_slot(mat))                                        continue;
            std::erase(m_pending_material_unloads, mat);

            if (std::ranges::contains(m_pending_material_loads, mat))           continue;
            m_pending_material_loads.push_back(mat);
        }

        for (auto mesh : m_scene_meshes) {                                      // needs loading

            if (find_slot(mesh))                                                continue;
            std::erase(m_pending_unloads, mesh);                                // The scene now wants it, so any pending unload is moot

            if (std::ranges::contains(m_pending_loads, mesh))                   continue;
            m_pending_loads.push_back(mesh);
        }

        // Schedule unloads --------------------------------------------------------

        for (auto& s : m_material_slots) {

            if (!s.alive)                                                       continue;
            if (m_scene_materials.contains(s.asset))                            continue;
            if (m_retained_materials.contains(s.asset))                         continue;
            std::erase(m_pending_material_loads, s.asset);

            if (std::ranges::contains(m_pending_material_unloads, s.asset))     continue;
            m_pending_material_unloads.push_back(s.asset);
        }

        for (auto& slot : m_mesh_slots) {                                       // needs unloading

            if (!slot.alive)                                                    continue;
            if (m_scene_meshes.contains(slot.asset))                            continue;
            if (m_retained_meshes.contains(slot.asset))                         continue;
            std::erase(m_pending_loads, slot.asset);                            // Visible again this frame -> a stale pending load shouldn't resurrect it

            if (std::ranges::contains(m_pending_unloads, slot.asset))           continue;
            m_pending_unloads.push_back(slot.asset);
        }
    }


    void renderer::retain_mesh(GLT::asset::handle mesh) {

        if (mesh == INVALID_HANDLE)
            return;

        m_retained_meshes.insert(mesh);
        std::erase(m_pending_unloads, mesh);        // Cancel a pending unload if there is one.

        if (!find_slot(mesh))
            m_pending_loads.push_back(mesh);
    }


    void renderer::release_mesh(GLT::asset::handle mesh) {

        if (mesh == INVALID_HANDLE)
            return;

        m_retained_meshes.erase(mesh);

        if (!m_scene_meshes.contains(mesh) && find_slot(mesh))
            m_pending_unloads.push_back(mesh);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    renderer::mesh_slot* renderer::find_slot(GLT::asset::handle h) noexcept {

        for (auto& s : m_mesh_slots)
            if (s.alive && s.asset == h)
                return &s;

        return nullptr;
    }


    const renderer::mesh_slot* renderer::find_slot(GLT::asset::handle h) const noexcept {

        for (const auto& s : m_mesh_slots)
            if (s.alive && s.asset == h)
                return &s;

        return nullptr;
    }


    void renderer::process_pending_meshes() {

        const bool any_work =
            !m_pending_material_unloads.empty() || !m_pending_unloads.empty() ||
            !m_pending_material_loads.empty()   || !m_pending_loads.empty();

        for (auto mat : m_pending_material_unloads)
            unload_material(mat);
        for (auto mesh : m_pending_unloads)
            unload_mesh(mesh);

        m_pending_material_unloads.clear();
        m_pending_unloads.clear();

        for (auto mesh : m_pending_loads) {
            if (find_slot(mesh))
                continue;
            load_mesh(mesh);
        }
        for (auto mat : m_pending_material_loads) {
            if (find_material_slot(mat))
                continue;
            load_material(mat);
        }
        m_pending_loads.clear();
        m_pending_material_loads.clear();

        // A topology change invalidates every pixel's history. This is the ONLY
        // time we need a global reset now — camera motion is handled by reprojection
        if (any_work)
            m_temporal_valid = false;
    }


    void renderer::rebuild_tlas_from_scene() {

        auto registry = GLT::asset::registry::get_ref();

        m_tlas_instances.clear();
        m_tlas_instances.reserve(std::min<size_t>(m_scene_instances.size(), TLAS_MAX_INSTANCES));

        // Rebuild the geometry buffer from scratch. The previous frame's content is dead.
        std::vector<gpu_geometry> geom_scratch;
        geom_scratch.reserve(m_scene_instances.size() * 4);     // rough guess; grows if needed

        for (const auto& inst : m_scene_instances) {

            if (m_tlas_instances.size() >= TLAS_MAX_INSTANCES)
                break;

            const mesh_slot* mesh_slot = find_slot(inst.mesh);
            if (!mesh_slot || !mesh_slot->alive)
                continue;

            auto* mesh = registry->data_as<GLT::asset::mesh::mesh_asset>(inst.mesh);
            if (!mesh)
                continue;

            // Instance-level material override, or per-submesh from the mesh
            const GLT::asset::handle override_mat = inst.material_override;
            if (override_mat != INVALID_HANDLE) {
                // ensure the override is resident (scene didn't necessarily list it before)
                // — it did, because submit_scene inserted it into m_scene_materials
            }

            const u32 geom_base = static_cast<u32>(geom_scratch.size());

            // One gpu_geometry per submesh that produced a BLAS geometry. Must match
            // build_blas_for_slot()'s filter (index_count >= 3).
            for (const auto& sm : mesh->submeshes) {

                if (sm.index_count < 3)
                    continue;

                GLT::asset::handle mat_handle = override_mat;
                if (mat_handle == INVALID_HANDLE) {
                    if (sm.material_slot < mesh->material_handles.size())
                        mat_handle = mesh->material_handles[sm.material_slot];
                }

                const material_slot* ms = find_material_slot(mat_handle);
                const u32 mat_index = ms ? ms->gpu_index : 0;   // 0xFFFFFFFF = no material, shader must handle

                gpu_geometry g{};
                g.vertex_base = mesh_slot->vertex_offset;
                g.index_base = mesh_slot->index_offset + sm.first_index;
                g.material_index = mat_index;
                g._pad = 0;
                geom_scratch.push_back(g);
            }

            // glm::mat4 -> VkTransformMatrixKHR
            VkTransformMatrixKHR xform{};
            const glm::mat4& m = inst.transform;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c)
                    xform.matrix[r][c] = m[c][r];

            m_tlas_instances.push_back(
                vk::AccelerationStructureInstanceKHR()
                    .setTransform(xform)
                    .setInstanceCustomIndex(geom_base)
                    .setAccelerationStructureReference(mesh_slot->blas.buffer.dev_address)
                    .setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable)
                    .setMask(0xFF)
                    .setInstanceShaderBindingTableRecordOffset(0));
        }

        // Upload the geometry buffer
        if (!geom_scratch.empty()) {
            const u32 need = static_cast<u32>(geom_scratch.size());
            reserve_geometry_space(need);                            // may grow + update_descriptor_set

            auto* dst = static_cast<gpu_geometry*>(m_vr_dev->map_buffer(m_geometry_buffer));
            std::memcpy(dst, geom_scratch.data(), need * sizeof(gpu_geometry));
            m_vr_dev->unmap_buffer(m_geometry_buffer);

            m_geometry_used = need;
        } else {
            m_geometry_used = 0;
        }

        if (!m_tlas_instances.empty())
            m_vr_dev->update_buffer(m_tlas_instance_buffer, m_tlas_instances.data(),
                sizeof(vk::AccelerationStructureInstanceKHR) * m_tlas_instances.size());

        auto scratch = m_vr_dev->create_scratch_buffer_from_build_info(m_tlas_build_info);
        auto cmd = m_device.allocateCommandBuffers(
            vk::CommandBufferAllocateInfo(m_graphics_pool, vk::CommandBufferLevel::ePrimary, 1))[0];

        cmd.begin(vk::CommandBufferBeginInfo().setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
        m_vr_dev->build_tlas(m_tlas_build_info, m_tlas_instance_buffer,
            static_cast<u32>(m_tlas_instances.size()), cmd);
        cmd.end();

        auto si = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&cmd);
        m_queues.graphics_queue.submit(si, nullptr);
        m_device.waitIdle();

        m_vr_dev->destroy_buffer(scratch);
        m_device.freeCommandBuffers(m_graphics_pool, cmd);
    }

}

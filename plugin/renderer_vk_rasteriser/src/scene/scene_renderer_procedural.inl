#pragma once


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

    // FUNCTION IMPLEMENTATION =========================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    GLT::render::procedural_mesh_id scene_renderer::create_procedural_mesh(GLT::render::mesh_update_class update_class) {

        u32 slot_idx;
        if (!m_free_procedural_slots.empty()) {

            slot_idx = m_free_procedural_slots.back();
            m_free_procedural_slots.pop_back();

        } else {

            slot_idx = static_cast<u32>(m_procedural_meshes.size());
            if (slot_idx >= MAX_PROCEDURAL_MESHES) {
                LOG(warn, "create_procedural_mesh: table full ({} slots)", MAX_PROCEDURAL_MESHES);
                return GLT::render::INVALID_PROCEDURAL_MESH;
            }
            m_procedural_meshes.emplace_back();
        }

        auto& slot = m_procedural_meshes[slot_idx];
        slot = {};
        slot.update_class = static_cast<u8>(update_class);
        slot.alive = true;
        slot.finalized = false;
        slot.material_slot = DEFAULT_MATERIAL_SLOT;

        return slot_idx + 1;   // 1-indexed; 0 is INVALID_PROCEDURAL_MESH
    }


    std::expected<void, GLT::render::procedural_mesh_error> scene_renderer::update_procedural_mesh(GLT::render::procedural_mesh_id id,
        std::span<const GLT::asset::mesh::vertex> vertices, std::span<const u32> indices, GLT::asset::handle material) {

        using PME = GLT::render::procedural_mesh_error;
        using ME = GLT::render::mesh_update_class;
        using Code = PME::code;

        if (id == GLT::render::INVALID_PROCEDURAL_MESH || id > m_procedural_meshes.size())
            return std::unexpected(PME{ Code::not_found, "invalid procedural mesh id" });

        auto& slot = m_procedural_meshes[id - 1];
        if (!slot.alive)
            return std::unexpected(PME{ Code::not_found, "procedural mesh slot is not alive" });

        const auto update_class = static_cast<ME>(slot.update_class);
        if (update_class == ME::streaming)
            return std::unexpected(PME{ Code::not_supported, "streaming procedural meshes are not implemented in v1" });

        if (update_class == ME::immutable && slot.finalized)
            return std::unexpected(PME{ Code::already_finalized, "immutable procedural mesh already uploaded once" });

        if (vertices.empty() || indices.empty())
            return std::unexpected(PME{ Code::invalid_input, "empty vertices/indices" });

        const u64 vb_bytes = static_cast<u64>(vertices.size()) * sizeof(GLT::asset::mesh::vertex);
        const u64 ib_bytes = static_cast<u64>(indices.size())  * sizeof(u32);

        // Validate index range. Cheap on CPU; catches preview bugs at the source.
        for (u32 idx : indices) {
            if (idx >= vertices.size())
                return std::unexpected(PME{ Code::invalid_input, std::format("index {} >= vertex count {}", idx, vertices.size()) });
        }

        m_plugin.get_vk_device().waitIdle();

        // Vertex buffer -----------------------------------------------------------------------------------------------

        const bool grow_v = !slot.vertex_buffer.buffer || vb_bytes > slot.vertex_buffer.size;
        if (grow_v) {

            if (slot.vertex_buffer.buffer)
                m_plugin.get_dev()->destroy_buffer(slot.vertex_buffer);
            slot.vertex_buffer = m_plugin.get_dev()->create_buffer(vb_bytes,
                vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer, 
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            if (!slot.vertex_buffer.buffer || !slot.vertex_buffer.allocation)
                return std::unexpected(PME{ Code::out_of_memory, "vertex buffer alloc failed" });
        }
        m_plugin.get_dev()->update_buffer(slot.vertex_buffer, vertices.data(), vb_bytes);

        // Index buffer ------------------------------------------------------------------------------------------------

        const bool grow_i = !slot.index_buffer.buffer || ib_bytes > slot.index_buffer.size;
        if (grow_i) {

            if (slot.index_buffer.buffer)
                m_plugin.get_dev()->destroy_buffer(slot.index_buffer);

            slot.index_buffer = m_plugin.get_dev()->create_buffer(ib_bytes,
                vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eStorageBuffer,
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

            if (!slot.index_buffer.buffer || !slot.index_buffer.allocation)
                return std::unexpected(PME{ Code::out_of_memory, "index buffer alloc failed" });
        }
        m_plugin.get_dev()->update_buffer(slot.index_buffer, indices.data(), ib_bytes);

        // AABB --------------------------------------------------------------------------------------------------------

        glm::vec3 mn(std::numeric_limits<f32>::max());
        glm::vec3 mx(std::numeric_limits<f32>::lowest());
        for (const auto& v : vertices) {
            mn = glm::min(mn, v.position);
            mx = glm::max(mx, v.position);
        }
        slot.local_min = mn;
        slot.local_max = mx;

        slot.vertex_count = static_cast<u32>(vertices.size());
        slot.index_count = static_cast<u32>(indices.size());

        // Material ----------------------------------------------------------------------------------------------------

        if (slot.material_slot != DEFAULT_MATERIAL_SLOT)
            m_shared->release_material(slot.material_slot);
        slot.material_slot = m_shared->acquire_material(material);

        slot.finalized = true;
        return {};
    }


    void scene_renderer::destroy_procedural_mesh(GLT::render::procedural_mesh_id id) {

        if (id == GLT::render::INVALID_PROCEDURAL_MESH || id > m_procedural_meshes.size())
            return;

        auto& slot = m_procedural_meshes[id - 1];
        if (!slot.alive)
            return;

        m_plugin.get_vk_device().waitIdle();

        if (slot.vertex_buffer.buffer)
            m_plugin.get_dev()->destroy_buffer(slot.vertex_buffer);
        if (slot.index_buffer.buffer)
            m_plugin.get_dev()->destroy_buffer(slot.index_buffer);
        if (slot.material_slot != DEFAULT_MATERIAL_SLOT)
            m_shared->release_material(slot.material_slot);

        slot = {};
        m_free_procedural_slots.push_back(id - 1);
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}


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

    void renderer::submit_scene(std::span<const GLT::asset::mesh::instance> instances) {

        m_scene_instances.assign(instances.begin(), instances.end());
        m_scene_meshes.clear();
        m_scene_meshes.reserve(instances.size());

        for (const auto& instance : instances) {
            if (instance.mesh == INVALID_HANDLE)
                continue;
            m_scene_meshes.insert(instance.mesh);
        }

        // Schedule loads
        for (auto mesh : m_scene_meshes) {

            if (find_slot(mesh))
                continue;

            std::erase(m_pending_unloads, mesh);
            if (std::ranges::contains(m_pending_loads, mesh))
                continue;

            m_pending_loads.push_back(mesh);
        }

        // Schedule unloads
        for (auto& slot : m_mesh_slots) {

            if (!slot.alive)
                continue;
            if (m_scene_meshes.contains(slot.asset))
                continue;
            if (m_retained_meshes.contains(slot.asset))
                continue;

            std::erase(m_pending_loads, slot.asset);
            if (std::ranges::contains(m_pending_unloads, slot.asset))
                continue;

            m_pending_unloads.push_back(slot.asset);
        }
    }


    void renderer::retain_mesh(GLT::asset::handle mesh) {

        if (mesh == INVALID_HANDLE)
            return;

        m_retained_meshes.insert(mesh);
        std::erase(m_pending_unloads, mesh);

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

    void renderer::process_pending_meshes() {

        for (auto mesh : m_pending_unloads)
            unload_mesh(mesh);
        m_pending_unloads.clear();

        for (auto mesh : m_pending_loads) {
            if (find_slot(mesh))
                continue;
            load_mesh(mesh);
        }
        m_pending_loads.clear();
    }

}

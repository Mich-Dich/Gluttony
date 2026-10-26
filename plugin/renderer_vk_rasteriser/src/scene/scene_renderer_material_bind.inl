
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

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void scene_renderer::write_scene_material_descriptors() {

        // Every frame, rewrite the per-frame descriptor for every material slot this scene references. Cheap (descriptor writes
        // are memcpy-into-mapped-CPU-buffer) and always correct: `shared` may have swapped a material's params_ubo during
        // poll_material_changes(), invalidating the previous descriptor contents
        for (u32 slot : m_scene_materials)
            m_shared->write_material_descriptor(m_material_desc_buffers[m_current_frame], slot);
    }

}

#pragma once

#include <asset/material.h>
#include <asset/mesh.h>
#include <asset/texture.h>

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

    GLT::render::preview_id renderer::create(const GLT::render::preview_desc& desc, const glm::uvec2& size) {

        // TODO: implement
        return {};
    }


    void renderer::destroy(GLT::render::preview_id id) {

        // TODO: implement
    }


    void renderer::set_active(GLT::render::preview_id id, bool active) {

        // TODO: implement
    }


    void renderer::set_desc(GLT::render::preview_id id, const GLT::render::preview_desc& desc) {

        // TODO: implement
    }


    void renderer::set_settings(GLT::render::preview_id id, const GLT::render::preview_settings& settings) {

        // TODO: implement
    }


    void renderer::set_camera(GLT::render::preview_id id, const GLT::render::preview_camera& camera) {

        // TODO: implement
    }


    void renderer::set_material_overrides(GLT::render::preview_id id, const GLT::render::preview_material_overrides& overrides) {

        // TODO: implement
    }


    void renderer::set_auto_rotate(GLT::render::preview_id id, bool enabled, f32 speed_rad_per_sec) {

        // TODO: implement
    }


    void renderer::set_highlight_submeshes(GLT::render::preview_id id, std::span<const u32> submesh_indices) {

        // TODO: implement
    }


    void* renderer::get_render_target(GLT::render::preview_id id) const {

        const auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.target)
            return nullptr;
        return static_cast<void*>(it->second.target->get_descriptor_set());
    }


    glm::uvec2 renderer::get_size(GLT::render::preview_id id) const {

        const auto it = m_previews.find(id);
        if (it == m_previews.end() || !it->second.target)
            return {0, 0};
        return it->second.target->get_size();
    }


    void renderer::set_size(GLT::render::preview_id id, const glm::uvec2& size) {

        // TODO: implement
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

    void renderer::create_preview_resources() {

        // TODO: implement
    }


    void renderer::destroy_preview_resources() {

        // TODO: implement
    }


    void renderer::add_preview_passes() {

        // TODO: implement
    }


    void renderer::reset_preview_images_layout() {

        // TODO: implement
    }

}


#pragma once

#include "modes.h"



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

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void renderer::on_load() {

        GLT::render::image::factory_table table{};

        table.default_fn = []() -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer_vk_ray::image>();
        };
        table.size_fn = [](const glm::uvec3& size) -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer_vk_ray::image>(size);
        };
        table.data_fn = [](const void* data, const u32 width, const u32 height, const bool mipmapped = false) -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer_vk_ray::image>(data, width, height, mipmapped);
        };
        table.path_fn = [](const std::filesystem::path& path, bool mipmapped) -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer_vk_ray::image>(path, mipmapped);
        };

        GLT::render::image::register_factory(table);
    }


    void renderer::on_unload() {

        GLT::render::image::register_factory({});   // clear it - see note below
    }


    std::span<const GLT::render::render_mode_info> renderer::supported_modes() const { return modes::mode_table; }


    void renderer::set_render_mode(u32 mode_id) {

        // Validate; ignore unknown ids
        for (const auto& mode : modes::mode_table)
            if (mode.id == mode_id) {
                m_active_mode = mode_id;
                return;
            }

        LOG(warn, "Renderer: unknown render mode id [{}]", mode_id);
    }


    u32 renderer::get_render_mode() const { return m_active_mode; }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}

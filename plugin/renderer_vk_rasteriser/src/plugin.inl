
#pragma once

#include <reflection/registry.h>

#include "modes.h"



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

    // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

    // TEMPLATE CLASS PUBLIC ===========================================================================================

    void renderer::on_load() {

        GLT::render::image::factory_table table{};

        table.default_fn = []() -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>();
        };
        table.size_fn = [](const glm::uvec3& size) -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(size);
        };
        table.data_fn = [](const void* data, const u32 width, const u32 height, const bool mipmapped = false) -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(data, width, height, mipmapped);
        };
        table.path_fn = [](const std::filesystem::path& path, bool mipmapped) -> std::unique_ptr<GLT::render::image> {
            return std::make_unique<GLT::renderer::vk_rasterizer::image>(path, mipmapped);
        };

        GLT::render::image::register_factory(table);

        // register the visual settings
        GLT::reflect::register_type<settings::visual>();
        GLT::reflect::finalize();
    }


    void renderer::on_unload() {

        GLT::render::image::register_factory({});   // clear it - see note below
    }

    // TEMPLATE CLASS PROTECTED ========================================================================================

    // TEMPLATE CLASS PRIVATE ==========================================================================================

}

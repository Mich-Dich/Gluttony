
#include "util/pch.h"
#include "image.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    image::factory_table                            image::s_factory{};

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    image::image() {}


    image::image(const glm::uvec3 size) {}


    image::image(const std::filesystem::path& image_path, const bool mipmapped) {}

        
    image::image(const void* data, const u32 width, const u32 height, const bool mipmapped) {}


    image::~image() {};

    // CLASS PUBLIC ====================================================================================================

    void image::register_factory(const factory_table& table) { s_factory = table; }


    std::unique_ptr<image> image::create_instance() {

        if (s_factory.default_fn)
            return s_factory.default_fn();
        return std::unique_ptr<image>(new image());
    }


    std::unique_ptr<image> image::create_instance(const glm::uvec3& size) {
        
        if (s_factory.size_fn)
            return s_factory.size_fn(size);
        return std::unique_ptr<image>(new image(size));
    }


    std::unique_ptr<image> image::create_instance(const void* data, const u32 width, const u32 height, const bool mipmapped) {
        
        if (s_factory.data_fn)
            return s_factory.data_fn(data, width, height, mipmapped);
        return std::unique_ptr<image>(new image(data, width, height, mipmapped));
    }


    std::unique_ptr<image> image::create_instance(const std::filesystem::path& path, bool mipmapped) {
        
        if (s_factory.path_fn)
            return s_factory.path_fn(path, mipmapped);
        return std::unique_ptr<image>(new image(path, mipmapped));
    }


    glm::uvec2 image::get_size()                    { return {}; }


    void* image::get_descriptor_set()               { return {}; }


    void* image::load(const std::filesystem::path& path, u32& outWidth, u32& outHeight) { return {}; }

    
    void image::resize(const glm::uvec3& new_size, const GLT::render::image_format format, const bool mipmapped) {}

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}


#include "util/pch.h"
#include "image.h"


// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    image::factory_table                                        image::s_factory{};

    #if defined(DEBUG)

        image::debug_stats image::s_debug_stats{};


        const image::debug_stats& image::get_debug_stats()      { return s_debug_stats; }


        u32 image::get_live_count()                             { return s_debug_stats.live_count.load(std::memory_order_relaxed); }


        u64 image::get_live_vram_bytes()                        { return s_debug_stats.live_bytes.load(std::memory_order_relaxed); }


        std::string image::get_debug_report() {

            const auto& s = s_debug_stats;
            constexpr double MB = 1024.0 * 1024.0;

            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "Images   live: %u   peak: %u   created: %llu   destroyed: %llu\n"
                "VRAM     live: %.2f MB   peak: %.2f MB   total alloc: %.2f MB   freed: %.2f MB",
                s.live_count.load(std::memory_order_relaxed),
                s.peak_count.load(std::memory_order_relaxed),
                (unsigned long long)s.total_created.load(std::memory_order_relaxed),
                (unsigned long long)s.total_destroyed.load(std::memory_order_relaxed),
                s.live_bytes.load(std::memory_order_relaxed) / MB,
                s.peak_bytes.load(std::memory_order_relaxed) / MB,
                s.total_bytes_allocated.load(std::memory_order_relaxed) / MB,
                s.total_bytes_freed.load(std::memory_order_relaxed) / MB);

            return std::string(buf);
        }


        void image::track_allocation(u64 bytes) {
            
            auto& s = s_debug_stats;
            const u32 new_count = s.live_count.fetch_add(1, std::memory_order_relaxed) + 1;
            const u64 new_bytes = s.live_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
            s.total_created.fetch_add(1, std::memory_order_relaxed);
            s.total_bytes_allocated.fetch_add(bytes, std::memory_order_relaxed);

            // Update peaks with CAS loops (only matters if you create images off-thread).
            u32 peak = s.peak_count.load(std::memory_order_relaxed);
            while (new_count > peak &&
                !s.peak_count.compare_exchange_weak(peak, new_count, std::memory_order_relaxed)) {}

            u64 peak_b = s.peak_bytes.load(std::memory_order_relaxed);
            while (new_bytes > peak_b &&
                !s.peak_bytes.compare_exchange_weak(peak_b, new_bytes, std::memory_order_relaxed)) {}
        }

        void image::track_deallocation(u64 bytes) {

            auto& s = s_debug_stats;
            s.live_count.fetch_sub(1, std::memory_order_relaxed);
            s.live_bytes.fetch_sub(bytes, std::memory_order_relaxed);
            s.total_destroyed.fetch_add(1, std::memory_order_relaxed);
            s.total_bytes_freed.fetch_add(bytes, std::memory_order_relaxed);
        }

    #endif

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    bool is_image_extension(const std::string& ext) {

        return ext == ".png"  || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".bmp"  || ext == ".tga" || ext == ".hdr"  ||
               ext == ".psd"  || ext == ".gif" || ext == ".pic"  || ext == ".pnm";
    }

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


    void image::reupload(const void* data) {}

    
    void image::update_region(const void* data, const u32 x, const u32 y, const u32 width, const u32 height, const u32 mip_level) {}

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}

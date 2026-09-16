
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    enum class image_format : u8 {

        None = 0,
        RGB,
        RGBA,
        RGBA16F,
        RGBA32F,
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    bool is_image_extension(const std::string& ext);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class image {
    public:

        image();
        image(const glm::uvec3 size);
        image(const void* data, const u32 width, const u32 height, const bool mipmapped = false);
        image(const std::filesystem::path& image_path, const bool mipmapped = false);

        virtual ~image();

        [[nodiscard]] virtual glm::uvec2 get_size();


        [[nodiscard]] virtual void* get_descriptor_set();


        [[nodiscard]] virtual void* load(const std::filesystem::path& path, u32& outWidth, u32& outHeight);


        virtual void resize(const glm::uvec3& new_size, const GLT::render::image_format format = GLT::render::image_format::RGBA,
            const bool mipmapped = false);


        virtual void reupload(const void* data);


        // Upload a sub-rectangle of pixel data directly into an existing image.
        // `data` must be tightly packed (row pitch == width * bytes_per_pixel).
        // Only level 0 is intended for the common case; if the image is mipmapped
        // and mip_level == 0, the mip chain is regenerated for you.
        virtual void update_region(const void* data, const u32 x, const u32 y, const u32 width, const u32 height, const u32 mip_level = 0);


        static std::unordered_set<std::string> get_supported_file_extensions() {

            return {".jpg", ".jpeg", ".jpe", ".png", ".tga", ".bmp", ".psd", ".hdr", ".pic", ".ppm", ".pgm", ".pnm"};
        }

        // --- pluggable creation ------------------------------------------------------
        // A renderer plugin registers concrete factories for each constructor
        // overload on load, so create_unique_ref<image>(...) transparently
        // produces the active backend's derived type instead of this placeholder.
        using default_factory_fn = std::unique_ptr<image>(*)();
        using size_factory_fn = std::unique_ptr<image>(*)(const glm::uvec3&);
        using data_factory_fn = std::unique_ptr<image>(*)(const void*, const u32, const u32, const bool);
        using path_factory_fn = std::unique_ptr<image>(*)(const std::filesystem::path&, bool);

        struct factory_table {
            default_factory_fn                  default_fn = nullptr;
            size_factory_fn                     size_fn = nullptr;
            data_factory_fn                     data_fn = nullptr;
            path_factory_fn                     path_fn = nullptr;
        };

        // Called by the active renderer plugin's on_load()/on_unload(). Passing
        // an empty factory_table{} clears it - the plugin MUST do this on
        // on_unload(), see note below.
        static void register_factory(const factory_table& table);

        // Used by create_unique_ref<image>(...); builds the registered concrete
        // type if one exists, otherwise falls back to this placeholder impl.
        [[nodiscard]] static std::unique_ptr<image> create_instance();
        [[nodiscard]] static std::unique_ptr<image> create_instance(const glm::uvec3& size);
        [[nodiscard]] static std::unique_ptr<image> create_instance(const void* data, const u32 width, const u32 height, const bool mipmapped = false);
        [[nodiscard]] static std::unique_ptr<image> create_instance(const std::filesystem::path& path, bool mipmapped = false);


        #if defined(DEBUG)
    
            struct debug_stats {
                std::atomic<u32>  live_count{0};
                std::atomic<u32>  peak_count{0};
                std::atomic<u64>  live_bytes{0};
                std::atomic<u64>  peak_bytes{0};
                std::atomic<u64>  total_created{0};
                std::atomic<u64>  total_destroyed{0};
                std::atomic<u64>  total_bytes_allocated{0};
                std::atomic<u64>  total_bytes_freed{0};
            };

            [[nodiscard]] static const debug_stats&  get_debug_stats();
            [[nodiscard]] static u32                 get_live_count();
            [[nodiscard]] static u64                 get_live_vram_bytes();
            [[nodiscard]] static std::string         get_debug_report();

            // Backend hook: call AFTER a successful GPU allocation (with the real
            // byte size of the underlying allocation) and BEFORE destroying it.
            static void track_allocation(u64 bytes);
            static void track_deallocation(u64 bytes);
            
        #endif

    private:

        static factory_table                    s_factory;

        #if defined(DEBUG)
        
            static debug_stats                  s_debug_stats;
    
        #endif

    };

}

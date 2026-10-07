
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::render {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // @brief Pixel formats a [render::image] can be created with
    //
    // Kept small on purpose - backends that need more formats should extend this enum rather than accepting arbitrary
    // backend-specific values, so callers can reason about what's portable
    enum class image_format : u8 {

        None = 0,
        RGB,
        RGBA,
        RGBA16F,
        RGBA32F,
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // @brief Reports whether a file extension is one this renderer can load
    //
    // Accepts the extension with the leading dot (e.g. ".png"). Comparison is exact and case-sensitive; callers should
    // lowercase the extension first if they might see mixed-case filenames
    //
    // @param ext  Extension to test, including the dot
    // @return true if the extension is a supported image format
    bool is_image_extension(const std::string& ext);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    // @brief Backend-agnostic GPU image handle
    //
    // [image] is a placeholder base class: the concrete implementation is supplied at runtime by whichever renderer plugin
    // is loaded. Every constructor and virtual here is a stub, and users are expected to create instances through one of
    // the [create_instance()] overloads (or [create_unique_ref<image>(...)], which forwards to them), never via [new image(...)]
    //
    // The plugin registers a [factory_table] from its [on_load()]. That table's function pointers are what [create_instance()]
    // dispatches to, so callers get the active backend's derived type without ever naming it
    class image {
    public:

        // All four constructors below are placeholders. They exist so the class is instantiable (and for the plugin authors
        // to derive from); normal user code should go through [create_instance()] or [GLT::ref<GLT..render::image>()] instead
        image();
        image(const glm::uvec3 size);
        image(const void* data, const u32 width, const u32 height, const bool mipmapped = false);
        image(const std::filesystem::path& image_path, const bool mipmapped = false);

        virtual ~image();

        // @brief Returns the image's size in pixels
        // @return Size as (width, height). Zero-initialized by the placeholder
        [[nodiscard]] virtual glm::uvec2 get_size();


        // @brief Returns the backend's descriptor set for sampling this image
        //
        // Returned as [void*] because the concrete type depends on the active renderer (Vulkan descriptor set, GL texture
        // to ImGui, which accepts it opaquely
        //
        // @return Backend-specific handle, or nullptr on the placeholder
        [[nodiscard]] virtual void* get_descriptor_set();


        // @brief Loads pixel data from disk into this image
        //
        // @param path       Image file to read
        // @param outWidth   Set to the decoded image's width, in pixels
        // @param outHeight  Set to the decoded image's height, in pixels
        // @return Backend-specific handle (see [get_descriptor_set()]), or nullptr on failure
        [[nodiscard]] virtual void* load(const std::filesystem::path& path, u32& outWidth, u32& outHeight);


        // @brief Reallocates the image to a new size
        //
        // Contents are undefined after this call; callers that need to preserve data must re-upload it
        //
        // @param new_size   New dimensions
        // @param format     Pixel format for the new storage
        // @param mipmapped  Whether the new storage includes a mip chain
        virtual void resize(const glm::uvec3& new_size, const GLT::render::image_format format = GLT::render::image_format::RGBA,
            const bool mipmapped = false);


        // @brief Replaces the entire image contents
        // @param data  Tightly packed pixel data matching the image's current format and size
        virtual void reupload(const void* data);


        // Upload a sub-rectangle of pixel data directly into an existing image. [data] must be tightly packed
        // (row pitch == width * bytes_per_pixel). Only level 0 is intended for the common case; if the image is mipmapped
        // and mip_level == 0, the mip chain is regenerated for you
        virtual void update_region(const void* data, const u32 x, const u32 y, const u32 width, const u32 height, const u32 mip_level = 0);


        // @brief Reports the image file extensions this build is known to accept
        // @return Set of extensions, each including the leading dot
        static std::unordered_set<std::string> get_supported_file_extensions() {

            return {".jpg", ".jpeg", ".jpe", ".png", ".tga", ".bmp", ".psd", ".hdr", ".pic", ".ppm", ".pgm", ".pnm"};
        }

        // --- pluggable creation ------------------------------------------------------

        // A renderer plugin registers concrete factories for each constructor
        // overload on load, so create_unique_ref<image>(...) transparently
        // produces the active backend's derived type instead of this placeholder
        using default_factory_fn = std::unique_ptr<image>(*)();
        using size_factory_fn = std::unique_ptr<image>(*)(const glm::uvec3&);
        using data_factory_fn = std::unique_ptr<image>(*)(const void*, const u32, const u32, const bool);
        using path_factory_fn = std::unique_ptr<image>(*)(const std::filesystem::path&, bool);


        // @brief Set of function pointers the active backend installs to
        //        construct its concrete image type
        //
        // A null entry means "no factory for that construction path"; the corresponding [create_instance()] overload then
        // falls back to the placeholder [image] implementation
        struct factory_table {
            default_factory_fn                  default_fn = nullptr;
            size_factory_fn                     size_fn = nullptr;
            data_factory_fn                     data_fn = nullptr;
            path_factory_fn                     path_fn = nullptr;
        };


        // Called by the active renderer plugin's on_load()/on_unload(). Passing an empty factory_table{} clears it
        // the plugin MUST do this on on_unload(), see note below
        static void register_factory(const factory_table& table);

        // Used by create_unique_ref<image>(...); builds the registered concrete type if one exists, otherwise falls
        // back to this placeholder impl


        // @brief Constructs a default (uninitialized) image
        // @return Owning pointer to the newly-constructed image
        [[nodiscard]] static std::unique_ptr<image> create_instance();


        // @brief Constructs an image with the given dimensions
        // @param size  Initial size in pixels
        // @return Owning pointer to the newly-constructed image
        [[nodiscard]] static std::unique_ptr<image> create_instance(const glm::uvec3& size);


        // @brief Constructs an image from a raw pixel buffer
        // @param data       Tightly packed pixel data; must match the backend's expected format
        // @param width      Image width, in pixels
        // @param height     Image height, in pixels
        // @param mipmapped  Whether the image should allocate a mip chain
        // @return Owning pointer to the newly-constructed image
        [[nodiscard]] static std::unique_ptr<image> create_instance(const void* data, const u32 width, const u32 height, const bool mipmapped = false);


        // @brief Constructs an image by loading it from disk
        // @param path       Image file to load
        // @param mipmapped  Whether the image should allocate a mip chain
        // @return Owning pointer to the newly-constructed image
        [[nodiscard]] static std::unique_ptr<image> create_instance(const std::filesystem::path& path, bool mipmapped = false);


        #if defined(DEBUG)
    
            // @brief Live statistics for every image ever constructed in this process
            //
            // All counters are atomic so backends that create images on worker threads don't need to synchronize externally
            // [live_*] track the current population; [peak_*] are the high-water marks; [total_*] accumulate over the 
            // process's lifetime
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

            // @brief Returns the process-wide image stats block
            // @return Const reference to the stats; valid for the process lifetime
            [[nodiscard]] static const debug_stats&  get_debug_stats();

            // @brief Returns the number of images currently alive
            [[nodiscard]] static u32 get_live_count();

            // @brief Returns the total bytes of GPU memory currently held by images
            [[nodiscard]] static u64 get_live_vram_bytes();

            // @brief Formats a human-readable summary of the debug stats
            // @return Multi-line string suitable for logging or an overlay
            [[nodiscard]] static std::string get_debug_report();

            // Backend hook: call AFTER a successful GPU allocation (with the real byte size of the underlying allocation)
            // and BEFORE destroying it
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

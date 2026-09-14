
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

    private:

        static factory_table                    s_factory;

    };

}

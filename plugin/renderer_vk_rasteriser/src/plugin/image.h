
#pragma once



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer {
    class renderer;
    class shared_resource_manager;
}

namespace GLT::renderer::vk_rasterizer {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

    class image : public GLT::render::image {
    public:

        image();

        image(const glm::uvec3 size);

        image(const std::filesystem::path& image_path, const bool mipmapped = false);

        image(const void* data, const u32 width, const u32 height, const bool mipmapped = false);

        ~image();


        SETTER(VmaAllocation, allocation,                       m_allocated_image.allocation);

        DEFAULT_GETTER_REF(util::allocated_image,               allocated_image);
        DEFAULT_SETTER(util::allocated_image,                   allocated_image);
        DEFAULT_GETTER_REF(util::accessible_image,              accessible_image);
        DEFAULT_SETTER(util::accessible_image,                  accessible_image);

        FORCE_INLINE_R glm::uvec2 get_size() override;


        [[nodiscard]] void* get_descriptor_set() override;


        [[nodiscard]] void* load(const std::filesystem::path& path, u32& out_width, u32& out_height) override;


        void resize(const glm::uvec3& new_size, const GLT::render::image_format format = GLT::render::image_format::RGBA,
            const bool mipmapped = false) override;


        void reupload(const void* data) override;


        void update_region(const void* data, const u32 x, const u32 y, const u32 width, const u32 height, const u32 mip_level = 0) override;

    private:

	    void allocate_memory(const void* data, const glm::uvec3 size,
            const GLT::render::image_format format = GLT::render::image_format::RGBA, const bool mipmapped = true);


        void assign_data(const void* data, const glm::uvec3 size, const GLT::render::image_format format, const u32 mip_levels);


        void release();


        util::allocated_image                                   m_allocated_image{};
        util::accessible_image                                  m_accessible_image{};
        GLT::ref<GLT::renderer::vk_rasterizer::renderer>        m_renderer{};
        GLT::render::image_format                               m_format = GLT::render::image_format::RGBA;
        u32                                                     m_mip_levels = 1;
        shared_resource_manager*                                m_shared{};

        #if defined(DEBUG)

            u64                                                 m_vram_bytes = 0;      // tracked size, used to decrement on release
        
        #endif
    };

}
